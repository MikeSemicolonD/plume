//
// plume
//

// Software acceleration structures for the WebGPU backend, which has no ray tracing API. Every BLAS and TLAS is a range of the device's AS heap
// (one storage buffer) in this layout; the GPU builder (plume_webgpu_as.cpp) writes it and RS64SoftRayQuery.hlsli walks it. Plain C++ so tests can check it natively.

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace plume {
    namespace webgpu_as {
        // A child word with this bit set is a leaf: its low bits index the triangle array.
        constexpr uint32_t kLeafBit = 0x80000000u;
        constexpr uint32_t kBlasOpaque = 1u;
        constexpr uint32_t kInstanceCullDisable = 1u;

        struct BlasHeader {
            uint32_t triCount;
            uint32_t nodeCount;
            uint32_t flags;
            uint32_t pad;
        };

        // Each internal node holds both children's boxes (one fetch per visit).
        struct Node {
            float lmin[3];
            uint32_t left;
            float lmax[3];
            uint32_t right;
            float rmin[3];
            uint32_t pad0;
            float rmax[3];
            uint32_t pad1;
        };

        // Triangles in tree order; primitiveIndex is the triangle's place in the source index buffer.
        struct Tri {
            float v0[3];
            uint32_t primitiveIndex;
            float v1[3];
            uint32_t pad0;
            float v2[3];
            uint32_t pad1;
        };

        // blasOffset: bytes from the start of the TLAS (the bound range) to the instance's BLAS.
        struct Instance {
            float worldToObject[3][4];
            uint32_t mask;
            uint32_t instanceID;
            uint32_t flags;
            uint32_t blasOffset;
        };

        struct TlasHeader {
            uint32_t instanceCount;
            uint32_t pad[3];
        };

        static_assert(sizeof(BlasHeader) == 16, "BlasHeader layout");
        static_assert(sizeof(Node) == 64, "Node layout");
        static_assert(sizeof(Tri) == 48, "Tri layout");
        static_assert(sizeof(Instance) == 64, "Instance layout");
        static_assert(sizeof(TlasHeader) == 16, "TlasHeader layout");

        constexpr uint64_t kAlign = 256;

        inline uint64_t alignUp(uint64_t v, uint64_t a) {
            return (v + a - 1) / a * a;
        }

        inline uint32_t nodeCountFor(uint32_t triCount) {
            return (triCount > 1) ? (triCount - 1) : 0;
        }

        inline uint64_t blasTriOffset(uint32_t triCount) {
            return sizeof(BlasHeader) + uint64_t(nodeCountFor(triCount)) * sizeof(Node);
        }

        inline uint64_t blasSize(uint32_t triCount) {
            return alignUp(std::max<uint64_t>(blasTriOffset(triCount) + uint64_t(triCount) * sizeof(Tri), 1), kAlign);
        }

        inline uint32_t sortCount(uint32_t triCount) {
            uint32_t p = 2;
            while (p < triCount) {
                p <<= 1;
            }

            return p;
        }

        // Scratch layout: keys, values (padded to a power of two), parents (2N), per-node atomic bounds (6 words each), scene bounds (8 words).
        inline uint64_t scratchKeysOffset(uint32_t) { return 0; }
        inline uint64_t scratchValuesOffset(uint32_t n) { return uint64_t(sortCount(n)) * 4; }
        inline uint64_t scratchParentsOffset(uint32_t n) { return scratchValuesOffset(n) + uint64_t(sortCount(n)) * 4; }
        inline uint64_t scratchBoundsOffset(uint32_t n) { return scratchParentsOffset(n) + uint64_t(std::max(n, 1u)) * 2 * 4; }
        inline uint64_t scratchSceneOffset(uint32_t n) { return scratchBoundsOffset(n) + uint64_t(std::max(nodeCountFor(n), 1u)) * 6 * 4; }

        inline uint64_t blasScratchSize(uint32_t triCount) {
            return alignUp(scratchSceneOffset(triCount) + 8 * 4, kAlign);
        }

        inline uint64_t tlasSize(uint32_t instanceCount) {
            return alignUp(sizeof(TlasHeader) + uint64_t(instanceCount) * sizeof(Instance), kAlign);
        }

        inline bool affineInverse(const float m[3][4], float out[3][4]) {
            const float a = m[0][0], b = m[0][1], c = m[0][2];
            const float d = m[1][0], e = m[1][1], f = m[1][2];
            const float g = m[2][0], h = m[2][1], i = m[2][2];
            const float det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
            if (!(std::fabs(det) > 1e-12f)) {
                return false;
            }

            const float s = 1.0f / det;
            const float r[3][3] = {
                { (e * i - f * h) * s, (c * h - b * i) * s, (b * f - c * e) * s },
                { (f * g - d * i) * s, (a * i - c * g) * s, (c * d - a * f) * s },
                { (d * h - e * g) * s, (b * g - a * h) * s, (a * e - b * d) * s }
            };

            for (int row = 0; row < 3; row++) {
                for (int col = 0; col < 3; col++) {
                    out[row][col] = r[row][col];
                }

                out[row][3] = -(r[row][0] * m[0][3] + r[row][1] * m[1][3] + r[row][2] * m[2][3]);
            }

            return true;
        }

        struct InstanceIn {
            float objectToWorld[3][4];
            uint32_t mask;
            uint32_t instanceID;
            bool cullDisable;
            uint32_t blasOffset;
        };

        // TlasHeader + one Instance each; a singular transform packs a zero mask, so no ray sees the instance.
        inline std::vector<uint8_t> packInstances(const InstanceIn *in, uint32_t count) {
            std::vector<uint8_t> out(sizeof(TlasHeader) + size_t(count) * sizeof(Instance), 0);
            TlasHeader header = {};
            header.instanceCount = count;
            std::memcpy(out.data(), &header, sizeof(header));
            for (uint32_t k = 0; k < count; k++) {
                Instance inst = {};
                const bool invertible = affineInverse(in[k].objectToWorld, inst.worldToObject);
                inst.mask = invertible ? in[k].mask : 0;
                inst.instanceID = in[k].instanceID;
                inst.flags = in[k].cullDisable ? kInstanceCullDisable : 0;
                inst.blasOffset = in[k].blasOffset;
                std::memcpy(out.data() + sizeof(TlasHeader) + size_t(k) * sizeof(Instance), &inst, sizeof(inst));
            }

            return out;
        }

        struct RefHit {
            float t = 0.0f;
            float u = 0.0f;
            float v = 0.0f;
            uint32_t prim = 0;
            bool frontFace = false;
        };

        // Moller-Trumbore. Front face is DXR's default (clockwise seen from the ray origin): det < 0 with det = dot(e1, cross(dir, e2)).
        inline bool intersectTri(const float v0[3], const float v1[3], const float v2[3], const float org[3], const float dir[3], float tmin, float tmax, bool cullBack, RefHit *hit) {
            const float e1[3] = { v1[0] - v0[0], v1[1] - v0[1], v1[2] - v0[2] };
            const float e2[3] = { v2[0] - v0[0], v2[1] - v0[1], v2[2] - v0[2] };
            const float p[3] = { dir[1] * e2[2] - dir[2] * e2[1], dir[2] * e2[0] - dir[0] * e2[2], dir[0] * e2[1] - dir[1] * e2[0] };
            const float det = e1[0] * p[0] + e1[1] * p[1] + e1[2] * p[2];
            if (det == 0.0f) {
                return false;
            }

            const bool front = det < 0.0f;
            if (cullBack && !front) {
                return false;
            }

            const float inv = 1.0f / det;
            const float s[3] = { org[0] - v0[0], org[1] - v0[1], org[2] - v0[2] };
            const float u = (s[0] * p[0] + s[1] * p[1] + s[2] * p[2]) * inv;
            if ((u < 0.0f) || (u > 1.0f)) {
                return false;
            }

            const float q[3] = { s[1] * e1[2] - s[2] * e1[1], s[2] * e1[0] - s[0] * e1[2], s[0] * e1[1] - s[1] * e1[0] };
            const float v = (dir[0] * q[0] + dir[1] * q[1] + dir[2] * q[2]) * inv;
            if ((v < 0.0f) || ((u + v) > 1.0f)) {
                return false;
            }

            const float t = (e2[0] * q[0] + e2[1] * q[1] + e2[2] * q[2]) * inv;
            if ((t < tmin) || (t > tmax)) {
                return false;
            }

            hit->t = t;
            hit->u = u;
            hit->v = v;
            hit->frontFace = front;
            return true;
        }

        // Brute force over an indexed float3 mesh: the reference the BVH paths are checked against.
        inline bool traceReference(const std::vector<float> &pos, const std::vector<uint32_t> &idx, const float org[3], const float dir[3], float tmin, float tmax, bool cullBack, RefHit *hit) {
            bool found = false;
            float best = tmax;
            for (size_t t = 0; (t + 2) < idx.size(); t += 3) {
                RefHit h;
                if (intersectTri(&pos[idx[t] * 3], &pos[idx[t + 1] * 3], &pos[idx[t + 2] * 3], org, dir, tmin, best, cullBack, &h)) {
                    h.prim = uint32_t(t / 3);
                    best = h.t;
                    *hit = h;
                    found = true;
                }
            }

            return found;
        }

        inline bool slab(const float lo[3], const float hi[3], const float org[3], const float inv[3], float tmin, float tmax) {
            for (int a = 0; a < 3; a++) {
                float t0 = (lo[a] - org[a]) * inv[a];
                float t1 = (hi[a] - org[a]) * inv[a];
                if (t0 > t1) {
                    std::swap(t0, t1);
                }

                tmin = std::max(tmin, t0);
                tmax = std::min(tmax, t1);
            }

            return tmin <= tmax;
        }

        // Closest hit walking a packed BLAS on the CPU (checks the GPU builder's output).
        inline bool traceBvh(const uint8_t *blas, const float org[3], const float dir[3], float tmin, float tmax, bool cullBack, RefHit *hit) {
            BlasHeader header;
            std::memcpy(&header, blas, sizeof(header));
            if (header.triCount == 0) {
                return false;
            }

            const Node *nodes = reinterpret_cast<const Node *>(blas + sizeof(BlasHeader));
            const Tri *tris = reinterpret_cast<const Tri *>(blas + blasTriOffset(header.triCount));
            float inv[3];
            for (int a = 0; a < 3; a++) {
                const float d = (std::fabs(dir[a]) < 1e-20f) ? std::copysign(1e-20f, dir[a]) : dir[a];
                inv[a] = 1.0f / d;
            }

            uint32_t stack[64];
            int top = 0;
            stack[top++] = (header.nodeCount == 0) ? kLeafBit : 0u;
            bool found = false;
            float best = tmax;
            while (top > 0) {
                const uint32_t word = stack[--top];
                if ((word & kLeafBit) != 0) {
                    const Tri &tri = tris[word & ~kLeafBit];
                    RefHit h;
                    if (intersectTri(tri.v0, tri.v1, tri.v2, org, dir, tmin, best, cullBack, &h)) {
                        h.prim = tri.primitiveIndex;
                        best = h.t;
                        *hit = h;
                        found = true;
                    }

                    continue;
                }

                const Node &n = nodes[word];
                if (slab(n.lmin, n.lmax, org, inv, tmin, best) && (top < 64)) {
                    stack[top++] = n.left;
                }

                if (slab(n.rmin, n.rmax, org, inv, tmin, best) && (top < 64)) {
                    stack[top++] = n.right;
                }
            }

            return found;
        }
    };
};
