//
// plume
//

// The WebGPU BLAS builder: an LBVH (Morton codes, bitonic sort, Karras 2012 hierarchy, bottom-up bounds by atomics) in WGSL, encoded on
// the main thread into one compute pass. Output layout: plume_webgpu_as.h.

#include "plume_webgpu.h"
#include "plume_webgpu_as.h"

#include <cstring>
#include <vector>

namespace plume {
    namespace {
        // One record per dispatch, each at a 256-byte dynamic offset. Word offsets: idx/vtx into their buffers, dst/tri into the AS heap,
        // keys..scene into the scratch buffer.
        struct Params {
            uint32_t n;
            uint32_t p;
            uint32_t idxW;
            uint32_t vtxW;
            uint32_t strideW;
            uint32_t dstW;
            uint32_t triW;
            uint32_t opaque;
            uint32_t keysW;
            uint32_t valsW;
            uint32_t parW;
            uint32_t bndW;
            uint32_t sceneW;
            uint32_t k;
            uint32_t j;
            uint32_t pad;
        };

        static_assert(sizeof(Params) == 64, "Params layout");

        constexpr uint32_t kRecordBytes = 256;
        constexpr uint32_t kGroup = 256;
        constexpr uint32_t kBlock = 1024;

        const char *kBuilderWGSL = R"(
struct Params {
    n: u32, p: u32, idxW: u32, vtxW: u32, strideW: u32, dstW: u32, triW: u32, opaque: u32,
    keysW: u32, valsW: u32, parW: u32, bndW: u32, sceneW: u32, k: u32, j: u32, pad: u32,
}

@group(0) @binding(0) var<uniform> P: Params;
@group(0) @binding(1) var<storage, read> I: array<u32>;
@group(0) @binding(2) var<storage, read> V: array<u32>;
@group(0) @binding(3) var<storage, read_write> S: array<atomic<u32>>;
@group(0) @binding(4) var<storage, read_write> H: array<u32>;

const NONE: u32 = 0xFFFFFFFFu;
const LEAF: u32 = 0x80000000u;

// Order-preserving float to uint: larger float, larger uint.
fn enc(f: f32) -> u32 {
    let b = bitcast<u32>(f);
    return select(b ^ 0x80000000u, ~b, (b & 0x80000000u) != 0u);
}

fn dec(u: u32) -> f32 {
    return bitcast<f32>(select(~u, u ^ 0x80000000u, (u & 0x80000000u) != 0u));
}

fn corner(t: u32, k: u32) -> vec3f {
    let v = I[P.idxW + t * 3u + k];
    let w = P.vtxW + v * P.strideW;
    return vec3f(bitcast<f32>(V[w]), bitcast<f32>(V[w + 1u]), bitcast<f32>(V[w + 2u]));
}

fn ld(i: u32) -> u32 {
    return atomicLoad(&S[i]);
}

fn st(i: u32, v: u32) {
    atomicStore(&S[i], v);
}

// Scratch bounds (6 words: min xyz, max xyz) start empty; the root has no parent.
@compute @workgroup_size(256)
fn k_clear(@builtin(global_invocation_id) id: vec3u) {
    let nodes = max(P.n, 2u) - 1u;
    if (id.x < nodes * 6u) {
        st(P.bndW + id.x, select(0u, NONE, (id.x % 6u) < 3u));
    }

    if (id.x < 6u) {
        st(P.sceneW + id.x, select(0u, NONE, id.x < 3u));
    }

    if (id.x == 0u) {
        st(P.parW, NONE);
    }
}

var<workgroup> wlo: array<atomic<u32>, 3>;
var<workgroup> whi: array<atomic<u32>, 3>;

@compute @workgroup_size(256)
fn k_centroid(@builtin(global_invocation_id) id: vec3u, @builtin(local_invocation_index) li: u32) {
    if (li < 3u) {
        atomicStore(&wlo[li], NONE);
        atomicStore(&whi[li], 0u);
    }

    workgroupBarrier();
    if (id.x < P.n) {
        let c = (corner(id.x, 0u) + corner(id.x, 1u) + corner(id.x, 2u)) / 3.0;
        for (var a = 0u; a < 3u; a++) {
            atomicMin(&wlo[a], enc(c[a]));
            atomicMax(&whi[a], enc(c[a]));
        }
    }

    workgroupBarrier();
    if (li < 3u) {
        atomicMin(&S[P.sceneW + li], atomicLoad(&wlo[li]));
        atomicMax(&S[P.sceneW + 3u + li], atomicLoad(&whi[li]));
    }
}

fn spread(x: u32) -> u32 {
    var v = x & 0x3FFu;
    v = (v | (v << 16u)) & 0x030000FFu;
    v = (v | (v << 8u)) & 0x0300F00Fu;
    v = (v | (v << 4u)) & 0x030C30C3u;
    v = (v | (v << 2u)) & 0x09249249u;
    return v;
}

// Real codes fit in 30 bits, so the padding (NONE) sorts after them.
@compute @workgroup_size(256)
fn k_morton(@builtin(global_invocation_id) id: vec3u) {
    if (id.x >= P.p) {
        return;
    }

    var key = NONE;
    if (id.x < P.n) {
        let lo = vec3f(dec(ld(P.sceneW)), dec(ld(P.sceneW + 1u)), dec(ld(P.sceneW + 2u)));
        let hi = vec3f(dec(ld(P.sceneW + 3u)), dec(ld(P.sceneW + 4u)), dec(ld(P.sceneW + 5u)));
        let c = (corner(id.x, 0u) + corner(id.x, 1u) + corner(id.x, 2u)) / 3.0;
        let q = clamp((c - lo) / max(hi - lo, vec3f(1e-20)), vec3f(0.0), vec3f(1.0)) * 1023.0;
        let u = vec3u(q);
        key = (spread(u.x) << 2u) | (spread(u.y) << 1u) | spread(u.z);
    }

    st(P.keysW + id.x, key);
    st(P.valsW + id.x, id.x);
}

var<workgroup> sk: array<u32, 1024>;
var<workgroup> sv: array<u32, 1024>;

fn local_step(len: u32, base: u32, kk: u32, jj: u32, li: u32) {
    for (var t = li; t < len / 2u; t += 256u) {
        let i = 2u * jj * (t / jj) + (t % jj);
        let l = i + jj;
        let up = ((base + i) & kk) == 0u;
        let a = sk[i];
        let b = sk[l];
        if ((a > b) == up && a != b) {
            sk[i] = b;
            sk[l] = a;
            let va = sv[i];
            sv[i] = sv[l];
            sv[l] = va;
        }
    }

    workgroupBarrier();
}

// k == 0: sort each block of min(p, 1024) from scratch. Otherwise: the strides below 1024 of bitonic stage k.
@compute @workgroup_size(256)
fn k_sort_local(@builtin(workgroup_id) wg: vec3u, @builtin(local_invocation_index) li: u32) {
    let len = min(P.p, 1024u);
    let base = wg.x * len;
    for (var t = li; t < len; t += 256u) {
        sk[t] = ld(P.keysW + base + t);
        sv[t] = ld(P.valsW + base + t);
    }

    workgroupBarrier();
    if (P.k == 0u) {
        for (var kk = 2u; kk <= len; kk <<= 1u) {
            for (var jj = kk >> 1u; jj > 0u; jj >>= 1u) {
                local_step(len, base, kk, jj, li);
            }
        }
    } else {
        for (var jj = len >> 1u; jj > 0u; jj >>= 1u) {
            local_step(len, base, P.k, jj, li);
        }
    }

    for (var t = li; t < len; t += 256u) {
        st(P.keysW + base + t, sk[t]);
        st(P.valsW + base + t, sv[t]);
    }
}

@compute @workgroup_size(256)
fn k_sort_global(@builtin(global_invocation_id) id: vec3u) {
    let t = id.x;
    if (t >= P.p / 2u) {
        return;
    }

    let i = 2u * P.j * (t / P.j) + (t % P.j);
    let l = i + P.j;
    let up = (i & P.k) == 0u;
    let a = ld(P.keysW + i);
    let b = ld(P.keysW + l);
    if ((a > b) == up && a != b) {
        st(P.keysW + i, b);
        st(P.keysW + l, a);
        let va = ld(P.valsW + i);
        st(P.valsW + i, ld(P.valsW + l));
        st(P.valsW + l, va);
    }
}

// Common prefix length of the sorted codes at i and j, with the index breaking ties; -1 outside [0, n).
fn delta(i: i32, j: i32) -> i32 {
    if (j < 0 || j >= i32(P.n)) {
        return -1;
    }

    let a = ld(P.keysW + u32(i));
    let b = ld(P.keysW + u32(j));
    if (a == b) {
        return 32 + i32(countLeadingZeros(u32(i) ^ u32(j)));
    }

    return i32(countLeadingZeros(a ^ b));
}

fn node_word(i: u32) -> u32 {
    return P.dstW + 4u + i * 16u;
}

// Parents: internal node c at parW + c, leaf c at parW + n + c.
@compute @workgroup_size(256)
fn k_hierarchy(@builtin(global_invocation_id) id: vec3u) {
    let n = i32(P.n);
    let i = i32(id.x);
    if (i >= n - 1) {
        return;
    }

    let d = select(-1, 1, delta(i, i + 1) - delta(i, i - 1) >= 0);
    let dmin = delta(i, i - d);
    var lmax = 2;
    while (delta(i, i + lmax * d) > dmin) {
        lmax *= 2;
    }

    var l = 0;
    for (var t = lmax / 2; t >= 1; t /= 2) {
        if (delta(i, i + (l + t) * d) > dmin) {
            l += t;
        }
    }

    let j = i + l * d;
    let dnode = delta(i, j);
    var s = 0;
    var dv = 2;
    var t = (l + dv - 1) / dv;
    loop {
        if (delta(i, i + (s + t) * d) > dnode) {
            s += t;
        }

        if (t <= 1) {
            break;
        }

        dv *= 2;
        t = (l + dv - 1) / dv;
    }

    let gamma = u32(i + s * d + min(d, 0));
    let leftLeaf = u32(min(i, j)) == gamma;
    let rightLeaf = u32(max(i, j)) == gamma + 1u;
    let w = node_word(u32(i));
    H[w + 3u] = select(gamma, LEAF | gamma, leftLeaf);
    H[w + 7u] = select(gamma + 1u, LEAF | (gamma + 1u), rightLeaf);
    st(select(P.parW + gamma, P.parW + P.n + gamma, leftLeaf), u32(i));
    st(select(P.parW + gamma + 1u, P.parW + P.n + gamma + 1u, rightLeaf), u32(i));
}

fn tri_word(t: u32) -> u32 {
    return P.triW + t * 12u;
}

fn store3(w: u32, v: vec3f) {
    H[w] = bitcast<u32>(v.x);
    H[w + 1u] = bitcast<u32>(v.y);
    H[w + 2u] = bitcast<u32>(v.z);
}

fn load3(w: u32) -> vec3f {
    return vec3f(bitcast<f32>(H[w]), bitcast<f32>(H[w + 1u]), bitcast<f32>(H[w + 2u]));
}

// Leaves in sorted order; a one-triangle BLAS has no sort and keeps its triangle.
@compute @workgroup_size(256)
fn k_leaves(@builtin(global_invocation_id) id: vec3u) {
    if (id.x >= P.n) {
        return;
    }

    let prim = select(ld(P.valsW + id.x), id.x, P.n == 1u);
    let w = tri_word(id.x);
    store3(w, corner(prim, 0u));
    H[w + 3u] = prim;
    store3(w + 4u, corner(prim, 1u));
    H[w + 7u] = 0u;
    store3(w + 8u, corner(prim, 2u));
    H[w + 11u] = 0u;
}

fn leaf_lo(t: u32) -> vec3f {
    let w = tri_word(t);
    return min(load3(w), min(load3(w + 4u), load3(w + 8u)));
}

fn leaf_hi(t: u32) -> vec3f {
    let w = tri_word(t);
    return max(load3(w), max(load3(w + 4u), load3(w + 8u)));
}

// Each leaf widens every ancestor's bounds: only atomics touch shared words, so no ordering between threads is needed.
@compute @workgroup_size(256)
fn k_refit(@builtin(global_invocation_id) id: vec3u) {
    if (id.x >= P.n || P.n < 2u) {
        return;
    }

    let lo = leaf_lo(id.x);
    let hi = leaf_hi(id.x);
    var node = ld(P.parW + P.n + id.x);
    var guard = 0u;
    while (node != NONE && guard < P.n) {
        let b = P.bndW + node * 6u;
        for (var a = 0u; a < 3u; a++) {
            atomicMin(&S[b + a], enc(lo[a]));
            atomicMax(&S[b + 3u + a], enc(hi[a]));
        }

        node = ld(P.parW + node);
        guard++;
    }
}

fn child_box(c: u32) -> array<vec3f, 2> {
    if ((c & LEAF) != 0u) {
        let t = c & ~LEAF;
        return array<vec3f, 2>(leaf_lo(t), leaf_hi(t));
    }

    let b = P.bndW + c * 6u;
    return array<vec3f, 2>(vec3f(dec(ld(b)), dec(ld(b + 1u)), dec(ld(b + 2u))), vec3f(dec(ld(b + 3u)), dec(ld(b + 4u)), dec(ld(b + 5u))));
}

@compute @workgroup_size(256)
fn k_finalize(@builtin(global_invocation_id) id: vec3u) {
    if (P.n < 2u || id.x >= P.n - 1u) {
        return;
    }

    let w = node_word(id.x);
    let lb = child_box(H[w + 3u]);
    let rb = child_box(H[w + 7u]);
    store3(w, lb[0]);
    store3(w + 4u, lb[1]);
    store3(w + 8u, rb[0]);
    H[w + 11u] = 0u;
    store3(w + 12u, rb[1]);
    H[w + 15u] = 0u;
}

@compute @workgroup_size(1)
fn k_header() {
    H[P.dstW] = P.n;
    H[P.dstW + 1u] = max(P.n, 1u) - 1u;
    H[P.dstW + 2u] = P.opaque;
    H[P.dstW + 3u] = 0u;
}
)";

        const char *kEntries[] = { "k_clear", "k_centroid", "k_morton", "k_sort_local", "k_sort_global", "k_hierarchy", "k_leaves", "k_refit", "k_finalize", "k_header" };
        enum Kernel { Clear, Centroid, Morton, SortLocal, SortGlobal, Hierarchy, Leaves, Refit, Finalize, Header, KernelCount };

        struct Builder {
            WGPUBindGroupLayout groupLayout = nullptr;
            WGPUPipelineLayout layout = nullptr;
            WGPUComputePipeline pipelines[KernelCount] = {};
            WGPUBuffer dummy = nullptr;
        };

        WGPUBindGroupLayoutEntry storageEntry(uint32_t binding, WGPUBufferBindingType type) {
            WGPUBindGroupLayoutEntry entry = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
            entry.binding = binding;
            entry.visibility = WGPUShaderStage_Compute;
            entry.buffer.type = type;
            return entry;
        }

        // Main thread, first build: the module, its explicit layout and one pipeline per kernel.
        Builder &builder(WebGPUDevice *device) {
            if (device->asBuilder != nullptr) {
                return *static_cast<Builder *>(device->asBuilder.get());
            }

            auto created = std::make_shared<Builder>();
            WGPUShaderSourceWGSL source = WGPU_SHADER_SOURCE_WGSL_INIT;
            source.code = { kBuilderWGSL, WGPU_STRLEN };
            WGPUShaderModuleDescriptor moduleDesc = WGPU_SHADER_MODULE_DESCRIPTOR_INIT;
            moduleDesc.nextInChain = &source.chain;
            WGPUShaderModule module = wgpuDeviceCreateShaderModule(device->device, &moduleDesc);

            WGPUBindGroupLayoutEntry entries[5] = {
                storageEntry(0, WGPUBufferBindingType_Uniform),
                storageEntry(1, WGPUBufferBindingType_ReadOnlyStorage),
                storageEntry(2, WGPUBufferBindingType_ReadOnlyStorage),
                storageEntry(3, WGPUBufferBindingType_Storage),
                storageEntry(4, WGPUBufferBindingType_Storage)
            };

            entries[0].buffer.hasDynamicOffset = true;
            entries[0].buffer.minBindingSize = sizeof(Params);
            WGPUBindGroupLayoutDescriptor groupDesc = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
            groupDesc.entryCount = 5;
            groupDesc.entries = entries;
            created->groupLayout = wgpuDeviceCreateBindGroupLayout(device->device, &groupDesc);

            WGPUPipelineLayoutDescriptor layoutDesc = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
            layoutDesc.bindGroupLayoutCount = 1;
            layoutDesc.bindGroupLayouts = &created->groupLayout;
            created->layout = wgpuDeviceCreatePipelineLayout(device->device, &layoutDesc);

            for (int k = 0; k < KernelCount; k++) {
                WGPUComputePipelineDescriptor pipelineDesc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
                pipelineDesc.layout = created->layout;
                pipelineDesc.compute.module = module;
                pipelineDesc.compute.entryPoint = { kEntries[k], WGPU_STRLEN };
                created->pipelines[k] = wgpuDeviceCreateComputePipeline(device->device, &pipelineDesc);
            }

            wgpuShaderModuleRelease(module);

            // Bound in place of a missing index or vertex buffer (an empty BLAS).
            WGPUBufferDescriptor dummyDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
            dummyDesc.size = 16;
            dummyDesc.usage = WGPUBufferUsage_Storage;
            created->dummy = wgpuDeviceCreateBuffer(device->device, &dummyDesc);
            device->asBuilder = created;
            return *created;
        }

        uint32_t groups(uint64_t threads) {
            return uint32_t((threads + kGroup - 1) / kGroup);
        }
    }

    void webgpuEncodeBlasBuild(WebGPUDevice *device, WGPUCommandEncoder encoder, const WebGPUBlasBuildData &data, uint64_t dstOffset, const WebGPUBuffer *scratch, uint64_t scratchOffset) {
        Builder &b = builder(device);
        const uint32_t n = data.triCount;
        const uint32_t p = webgpu_as::sortCount(n);
        Params base = {};
        base.n = n;
        base.p = p;
        base.idxW = uint32_t(data.indexOffset / 4);
        base.vtxW = uint32_t(data.vertexOffset / 4);
        base.strideW = data.vertexStride / 4;
        base.dstW = uint32_t(dstOffset / 4);
        base.triW = uint32_t((dstOffset + webgpu_as::blasTriOffset(n)) / 4);
        base.opaque = data.opaque;
        const uint64_t scratchBase = scratchOffset;
        base.keysW = uint32_t((scratchBase + webgpu_as::scratchKeysOffset(n)) / 4);
        base.valsW = uint32_t((scratchBase + webgpu_as::scratchValuesOffset(n)) / 4);
        base.parW = uint32_t((scratchBase + webgpu_as::scratchParentsOffset(n)) / 4);
        base.bndW = uint32_t((scratchBase + webgpu_as::scratchBoundsOffset(n)) / 4);
        base.sceneW = uint32_t((scratchBase + webgpu_as::scratchSceneOffset(n)) / 4);

        struct Dispatch {
            Kernel kernel;
            uint32_t groups;
            uint32_t k;
            uint32_t j;
        };

        std::vector<Dispatch> dispatches;
        if (n >= 2) {
            const uint32_t nodes = n - 1;
            dispatches.push_back({ Clear, groups(std::max<uint64_t>(uint64_t(nodes) * 6, 6)), 0, 0 });
            dispatches.push_back({ Centroid, groups(n), 0, 0 });
            dispatches.push_back({ Morton, groups(p), 0, 0 });
            const uint32_t len = std::min(p, kBlock);
            dispatches.push_back({ SortLocal, p / len, 0, 0 });
            for (uint32_t k = kBlock * 2; k <= p; k <<= 1) {
                for (uint32_t j = k >> 1; j >= kBlock; j >>= 1) {
                    dispatches.push_back({ SortGlobal, groups(p / 2), k, j });
                }

                dispatches.push_back({ SortLocal, p / kBlock, k, 0 });
            }

            dispatches.push_back({ Hierarchy, groups(nodes), 0, 0 });
        }

        if (n >= 1) {
            dispatches.push_back({ Leaves, groups(n), 0, 0 });
        }

        if (n >= 2) {
            dispatches.push_back({ Refit, groups(n), 0, 0 });
            dispatches.push_back({ Finalize, groups(n - 1), 0, 0 });
        }

        dispatches.push_back({ Header, 1, 0, 0 });

        WGPUBufferDescriptor paramsDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
        paramsDesc.size = uint64_t(dispatches.size()) * kRecordBytes;
        paramsDesc.usage = WGPUBufferUsage_Uniform;
        paramsDesc.mappedAtCreation = true;
        WGPUBuffer params = wgpuDeviceCreateBuffer(device->device, &paramsDesc);
        uint8_t *mapped = static_cast<uint8_t *>(wgpuBufferGetMappedRange(params, 0, paramsDesc.size));
        for (size_t d = 0; d < dispatches.size(); d++) {
            Params record = base;
            record.k = dispatches[d].k;
            record.j = dispatches[d].j;
            memcpy(mapped + d * kRecordBytes, &record, sizeof(record));
        }

        wgpuBufferUnmap(params);

        const bool inputs = (n > 0) && (data.indexBuffer != nullptr) && (data.vertexBuffer != nullptr);
        WGPUBindGroupEntry entries[5] = { WGPU_BIND_GROUP_ENTRY_INIT, WGPU_BIND_GROUP_ENTRY_INIT, WGPU_BIND_GROUP_ENTRY_INIT, WGPU_BIND_GROUP_ENTRY_INIT, WGPU_BIND_GROUP_ENTRY_INIT };
        for (uint32_t e = 0; e < 5; e++) {
            entries[e].binding = e;
        }

        entries[0].buffer = params;
        entries[0].size = sizeof(Params);
        entries[1].buffer = inputs ? data.indexBuffer->buffer : b.dummy;
        entries[2].buffer = inputs ? data.vertexBuffer->buffer : b.dummy;
        entries[3].buffer = (scratch != nullptr) ? scratch->buffer : b.dummy;
        entries[4].buffer = device->asHeap->buffer;
        WGPUBindGroupDescriptor groupDesc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
        groupDesc.layout = b.groupLayout;
        groupDesc.entryCount = 5;
        groupDesc.entries = entries;
        WGPUBindGroup group = wgpuDeviceCreateBindGroup(device->device, &groupDesc);

        WGPUComputePassDescriptor passDesc = WGPU_COMPUTE_PASS_DESCRIPTOR_INIT;
        WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, &passDesc);
        for (size_t d = 0; d < dispatches.size(); d++) {
            const uint32_t offset = uint32_t(d * kRecordBytes);
            wgpuComputePassEncoderSetPipeline(pass, b.pipelines[dispatches[d].kernel]);
            wgpuComputePassEncoderSetBindGroup(pass, 0, group, 1, &offset);
            wgpuComputePassEncoderDispatchWorkgroups(pass, std::max(dispatches[d].groups, 1u), 1, 1);
        }

        wgpuComputePassEncoderEnd(pass);
        wgpuComputePassEncoderRelease(pass);
        wgpuBindGroupRelease(group);
        wgpuBufferRelease(params);
    }
};
