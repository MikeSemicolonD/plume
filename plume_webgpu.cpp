//
// plume
//
// Copyright (c) 2024 renderbag and contributors. All rights reserved.
// Licensed under the MIT license. See LICENSE file for details.
//

#include "plume_webgpu.h"

#include <algorithm>
#include <cassert>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <regex>

#include <emscripten/html5.h>
#include <emscripten/proxying.h>
#include <emscripten/threading.h>

namespace plume {
    // Threading.

    static void runJob(void *arg) {
        (*static_cast<const std::function<void()> *>(arg))();
    }

    void webgpuRunOnMain(const std::function<void()> &job) {
        if (emscripten_is_main_runtime_thread()) {
            job();
            return;
        }

        emscripten_proxy_sync(emscripten_proxy_get_system_queue(), emscripten_main_runtime_thread_id(), runJob, const_cast<std::function<void()> *>(&job));
    }

    static void runAsyncJob(em_proxying_ctx *ctx, void *arg) {
        (*static_cast<const std::function<void(std::function<void()>)> *>(arg))([ctx]() { emscripten_proxy_finish(ctx); });
    }

    void webgpuRunOnMainAsync(const std::function<void(std::function<void()> done)> &job) {
        if (emscripten_is_main_runtime_thread()) {
            fprintf(stderr, "[webgpu] an asynchronous WebGPU call was made on the main browser thread, which can't wait for it\n");
            abort();
        }

        emscripten_proxy_sync_with_ctx(emscripten_proxy_get_system_queue(), emscripten_main_runtime_thread_id(), runAsyncJob, const_cast<std::function<void(std::function<void()>)> *>(&job));
    }

    // Conversions.

    static WGPUStringView toView(const char *text) {
        return { text, WGPU_STRLEN };
    }

    static WGPUStringView toView(const std::string &text) {
        return { text.data(), text.size() };
    }

    static std::string toString(WGPUStringView view) {
        if (view.data == nullptr) {
            return {};
        }

        return std::string(view.data, (view.length == WGPU_STRLEN) ? strlen(view.data) : view.length);
    }

    static WGPUTextureFormat toWgpu(RenderFormat format) {
        switch (format) {
        case RenderFormat::R32G32B32A32_TYPELESS:
        case RenderFormat::R32G32B32A32_FLOAT:
            return WGPUTextureFormat_RGBA32Float;
        case RenderFormat::R32G32B32A32_UINT:
            return WGPUTextureFormat_RGBA32Uint;
        case RenderFormat::R32G32B32A32_SINT:
            return WGPUTextureFormat_RGBA32Sint;
        case RenderFormat::R16G16B16A16_TYPELESS:
        case RenderFormat::R16G16B16A16_FLOAT:
            return WGPUTextureFormat_RGBA16Float;
        case RenderFormat::R16G16B16A16_UNORM:
            return WGPUTextureFormat_RGBA16Unorm;
        case RenderFormat::R16G16B16A16_UINT:
            return WGPUTextureFormat_RGBA16Uint;
        case RenderFormat::R16G16B16A16_SNORM:
            return WGPUTextureFormat_RGBA16Snorm;
        case RenderFormat::R16G16B16A16_SINT:
            return WGPUTextureFormat_RGBA16Sint;
        case RenderFormat::R32G32_TYPELESS:
        case RenderFormat::R32G32_FLOAT:
            return WGPUTextureFormat_RG32Float;
        case RenderFormat::R32G32_UINT:
            return WGPUTextureFormat_RG32Uint;
        case RenderFormat::R32G32_SINT:
            return WGPUTextureFormat_RG32Sint;
        case RenderFormat::R8G8B8A8_TYPELESS:
        case RenderFormat::R8G8B8A8_UNORM:
            return WGPUTextureFormat_RGBA8Unorm;
        case RenderFormat::R8G8B8A8_UINT:
            return WGPUTextureFormat_RGBA8Uint;
        case RenderFormat::R8G8B8A8_SNORM:
            return WGPUTextureFormat_RGBA8Snorm;
        case RenderFormat::R8G8B8A8_SINT:
            return WGPUTextureFormat_RGBA8Sint;
        case RenderFormat::B8G8R8A8_UNORM:
            return WGPUTextureFormat_BGRA8Unorm;
        case RenderFormat::R16G16_TYPELESS:
        case RenderFormat::R16G16_FLOAT:
            return WGPUTextureFormat_RG16Float;
        case RenderFormat::R16G16_UNORM:
            return WGPUTextureFormat_RG16Unorm;
        case RenderFormat::R16G16_UINT:
            return WGPUTextureFormat_RG16Uint;
        case RenderFormat::R16G16_SNORM:
            return WGPUTextureFormat_RG16Snorm;
        case RenderFormat::R16G16_SINT:
            return WGPUTextureFormat_RG16Sint;
        case RenderFormat::R32_TYPELESS:
        case RenderFormat::R32_FLOAT:
            return WGPUTextureFormat_R32Float;
        case RenderFormat::D32_FLOAT:
            return WGPUTextureFormat_Depth32Float;
        case RenderFormat::D32_FLOAT_S8_UINT:
            return WGPUTextureFormat_Depth32FloatStencil8;
        case RenderFormat::R32_UINT:
            return WGPUTextureFormat_R32Uint;
        case RenderFormat::R32_SINT:
            return WGPUTextureFormat_R32Sint;
        case RenderFormat::R8G8_TYPELESS:
        case RenderFormat::R8G8_UNORM:
            return WGPUTextureFormat_RG8Unorm;
        case RenderFormat::R8G8_UINT:
            return WGPUTextureFormat_RG8Uint;
        case RenderFormat::R8G8_SNORM:
            return WGPUTextureFormat_RG8Snorm;
        case RenderFormat::R8G8_SINT:
            return WGPUTextureFormat_RG8Sint;
        case RenderFormat::R16_TYPELESS:
        case RenderFormat::R16_FLOAT:
            return WGPUTextureFormat_R16Float;
        case RenderFormat::D16_UNORM:
            return WGPUTextureFormat_Depth16Unorm;
        case RenderFormat::R16_UNORM:
            return WGPUTextureFormat_R16Unorm;
        case RenderFormat::R16_UINT:
            return WGPUTextureFormat_R16Uint;
        case RenderFormat::R16_SNORM:
            return WGPUTextureFormat_R16Snorm;
        case RenderFormat::R16_SINT:
            return WGPUTextureFormat_R16Sint;
        case RenderFormat::R8_TYPELESS:
        case RenderFormat::R8_UNORM:
            return WGPUTextureFormat_R8Unorm;
        case RenderFormat::R8_UINT:
            return WGPUTextureFormat_R8Uint;
        case RenderFormat::R8_SNORM:
            return WGPUTextureFormat_R8Snorm;
        case RenderFormat::R8_SINT:
            return WGPUTextureFormat_R8Sint;
        case RenderFormat::BC1_TYPELESS:
        case RenderFormat::BC1_UNORM:
            return WGPUTextureFormat_BC1RGBAUnorm;
        case RenderFormat::BC1_UNORM_SRGB:
            return WGPUTextureFormat_BC1RGBAUnormSrgb;
        case RenderFormat::BC2_TYPELESS:
        case RenderFormat::BC2_UNORM:
            return WGPUTextureFormat_BC2RGBAUnorm;
        case RenderFormat::BC2_UNORM_SRGB:
            return WGPUTextureFormat_BC2RGBAUnormSrgb;
        case RenderFormat::BC3_TYPELESS:
        case RenderFormat::BC3_UNORM:
            return WGPUTextureFormat_BC3RGBAUnorm;
        case RenderFormat::BC3_UNORM_SRGB:
            return WGPUTextureFormat_BC3RGBAUnormSrgb;
        case RenderFormat::BC4_TYPELESS:
        case RenderFormat::BC4_UNORM:
            return WGPUTextureFormat_BC4RUnorm;
        case RenderFormat::BC4_SNORM:
            return WGPUTextureFormat_BC4RSnorm;
        case RenderFormat::BC5_TYPELESS:
        case RenderFormat::BC5_UNORM:
            return WGPUTextureFormat_BC5RGUnorm;
        case RenderFormat::BC5_SNORM:
            return WGPUTextureFormat_BC5RGSnorm;
        case RenderFormat::BC6H_TYPELESS:
        case RenderFormat::BC6H_UF16:
            return WGPUTextureFormat_BC6HRGBUfloat;
        case RenderFormat::BC6H_SF16:
            return WGPUTextureFormat_BC6HRGBFloat;
        case RenderFormat::BC7_TYPELESS:
        case RenderFormat::BC7_UNORM:
            return WGPUTextureFormat_BC7RGBAUnorm;
        case RenderFormat::BC7_UNORM_SRGB:
            return WGPUTextureFormat_BC7RGBAUnormSrgb;
        default:
            return WGPUTextureFormat_Undefined;
        }
    }

    // Bytes per texel, or per 4x4 block for compressed formats.
    static uint32_t blockBytes(RenderFormat format, uint32_t &blockSize) {
        blockSize = 1;
        switch (format) {
        case RenderFormat::BC1_TYPELESS:
        case RenderFormat::BC1_UNORM:
        case RenderFormat::BC1_UNORM_SRGB:
        case RenderFormat::BC4_TYPELESS:
        case RenderFormat::BC4_UNORM:
        case RenderFormat::BC4_SNORM:
            blockSize = 4;
            return 8;
        case RenderFormat::BC2_TYPELESS:
        case RenderFormat::BC2_UNORM:
        case RenderFormat::BC2_UNORM_SRGB:
        case RenderFormat::BC3_TYPELESS:
        case RenderFormat::BC3_UNORM:
        case RenderFormat::BC3_UNORM_SRGB:
        case RenderFormat::BC5_TYPELESS:
        case RenderFormat::BC5_UNORM:
        case RenderFormat::BC5_SNORM:
        case RenderFormat::BC6H_TYPELESS:
        case RenderFormat::BC6H_UF16:
        case RenderFormat::BC6H_SF16:
        case RenderFormat::BC7_TYPELESS:
        case RenderFormat::BC7_UNORM:
        case RenderFormat::BC7_UNORM_SRGB:
            blockSize = 4;
            return 16;
        default:
            return RenderFormatSize(format);
        }
    }

    static bool isDepthFormat(WGPUTextureFormat format) {
        return (format == WGPUTextureFormat_Depth16Unorm) || (format == WGPUTextureFormat_Depth32Float) || (format == WGPUTextureFormat_Depth24Plus) || (format == WGPUTextureFormat_Depth24PlusStencil8) || (format == WGPUTextureFormat_Depth32FloatStencil8);
    }

    static bool hasStencil(WGPUTextureFormat format) {
        return (format == WGPUTextureFormat_Depth24PlusStencil8) || (format == WGPUTextureFormat_Depth32FloatStencil8);
    }

    static WGPUVertexFormat toVertexFormat(RenderFormat format) {
        switch (format) {
        case RenderFormat::R32G32B32A32_FLOAT:
            return WGPUVertexFormat_Float32x4;
        case RenderFormat::R32G32B32A32_UINT:
            return WGPUVertexFormat_Uint32x4;
        case RenderFormat::R32G32B32A32_SINT:
            return WGPUVertexFormat_Sint32x4;
        case RenderFormat::R32G32B32_FLOAT:
            return WGPUVertexFormat_Float32x3;
        case RenderFormat::R32G32B32_UINT:
            return WGPUVertexFormat_Uint32x3;
        case RenderFormat::R32G32B32_SINT:
            return WGPUVertexFormat_Sint32x3;
        case RenderFormat::R16G16B16A16_FLOAT:
            return WGPUVertexFormat_Float16x4;
        case RenderFormat::R16G16B16A16_UNORM:
            return WGPUVertexFormat_Unorm16x4;
        case RenderFormat::R16G16B16A16_UINT:
            return WGPUVertexFormat_Uint16x4;
        case RenderFormat::R16G16B16A16_SNORM:
            return WGPUVertexFormat_Snorm16x4;
        case RenderFormat::R16G16B16A16_SINT:
            return WGPUVertexFormat_Sint16x4;
        case RenderFormat::R32G32_FLOAT:
            return WGPUVertexFormat_Float32x2;
        case RenderFormat::R32G32_UINT:
            return WGPUVertexFormat_Uint32x2;
        case RenderFormat::R32G32_SINT:
            return WGPUVertexFormat_Sint32x2;
        case RenderFormat::R8G8B8A8_UNORM:
            return WGPUVertexFormat_Unorm8x4;
        case RenderFormat::R8G8B8A8_UINT:
            return WGPUVertexFormat_Uint8x4;
        case RenderFormat::R8G8B8A8_SNORM:
            return WGPUVertexFormat_Snorm8x4;
        case RenderFormat::R8G8B8A8_SINT:
            return WGPUVertexFormat_Sint8x4;
        case RenderFormat::B8G8R8A8_UNORM:
            return WGPUVertexFormat_Unorm8x4BGRA;
        case RenderFormat::R16G16_FLOAT:
            return WGPUVertexFormat_Float16x2;
        case RenderFormat::R16G16_UNORM:
            return WGPUVertexFormat_Unorm16x2;
        case RenderFormat::R16G16_UINT:
            return WGPUVertexFormat_Uint16x2;
        case RenderFormat::R16G16_SNORM:
            return WGPUVertexFormat_Snorm16x2;
        case RenderFormat::R16G16_SINT:
            return WGPUVertexFormat_Sint16x2;
        case RenderFormat::R32_FLOAT:
            return WGPUVertexFormat_Float32;
        case RenderFormat::R32_UINT:
            return WGPUVertexFormat_Uint32;
        case RenderFormat::R32_SINT:
            return WGPUVertexFormat_Sint32;
        case RenderFormat::R8G8_UNORM:
            return WGPUVertexFormat_Unorm8x2;
        case RenderFormat::R8G8_UINT:
            return WGPUVertexFormat_Uint8x2;
        case RenderFormat::R8G8_SNORM:
            return WGPUVertexFormat_Snorm8x2;
        case RenderFormat::R8G8_SINT:
            return WGPUVertexFormat_Sint8x2;
        case RenderFormat::R16_FLOAT:
            return WGPUVertexFormat_Float16;
        case RenderFormat::R16_UNORM:
            return WGPUVertexFormat_Unorm16;
        case RenderFormat::R16_UINT:
            return WGPUVertexFormat_Uint16;
        case RenderFormat::R16_SNORM:
            return WGPUVertexFormat_Snorm16;
        case RenderFormat::R16_SINT:
            return WGPUVertexFormat_Sint16;
        case RenderFormat::R8_UNORM:
            return WGPUVertexFormat_Unorm8;
        case RenderFormat::R8_UINT:
            return WGPUVertexFormat_Uint8;
        case RenderFormat::R8_SNORM:
            return WGPUVertexFormat_Snorm8;
        case RenderFormat::R8_SINT:
            return WGPUVertexFormat_Sint8;
        default:
            return WGPUVertexFormat_Force32;
        }
    }

    static WGPUAddressMode toWgpu(RenderTextureAddressMode mode) {
        switch (mode) {
        case RenderTextureAddressMode::WRAP:
            return WGPUAddressMode_Repeat;
        case RenderTextureAddressMode::MIRROR:
        case RenderTextureAddressMode::MIRROR_ONCE:
            return WGPUAddressMode_MirrorRepeat;
        default:
            return WGPUAddressMode_ClampToEdge;
        }
    }

    static WGPUCompareFunction toWgpu(RenderComparisonFunction function) {
        switch (function) {
        case RenderComparisonFunction::NEVER:
            return WGPUCompareFunction_Never;
        case RenderComparisonFunction::LESS:
            return WGPUCompareFunction_Less;
        case RenderComparisonFunction::EQUAL:
            return WGPUCompareFunction_Equal;
        case RenderComparisonFunction::LESS_EQUAL:
            return WGPUCompareFunction_LessEqual;
        case RenderComparisonFunction::GREATER:
            return WGPUCompareFunction_Greater;
        case RenderComparisonFunction::NOT_EQUAL:
            return WGPUCompareFunction_NotEqual;
        case RenderComparisonFunction::GREATER_EQUAL:
            return WGPUCompareFunction_GreaterEqual;
        default:
            return WGPUCompareFunction_Always;
        }
    }

    static WGPUStencilOperation toWgpu(RenderStencilOp op) {
        switch (op) {
        case RenderStencilOp::ZERO:
            return WGPUStencilOperation_Zero;
        case RenderStencilOp::REPLACE:
            return WGPUStencilOperation_Replace;
        case RenderStencilOp::INCREMENT_AND_CLAMP:
            return WGPUStencilOperation_IncrementClamp;
        case RenderStencilOp::DECREMENT_AND_CLAMP:
            return WGPUStencilOperation_DecrementClamp;
        case RenderStencilOp::INVERT:
            return WGPUStencilOperation_Invert;
        case RenderStencilOp::INCREMENT_AND_WRAP:
            return WGPUStencilOperation_IncrementWrap;
        case RenderStencilOp::DECREMENT_AND_WRAP:
            return WGPUStencilOperation_DecrementWrap;
        default:
            return WGPUStencilOperation_Keep;
        }
    }

    static WGPUBlendFactor toWgpu(RenderBlend blend) {
        switch (blend) {
        case RenderBlend::ZERO:
            return WGPUBlendFactor_Zero;
        case RenderBlend::SRC_COLOR:
            return WGPUBlendFactor_Src;
        case RenderBlend::INV_SRC_COLOR:
            return WGPUBlendFactor_OneMinusSrc;
        case RenderBlend::SRC_ALPHA:
            return WGPUBlendFactor_SrcAlpha;
        case RenderBlend::INV_SRC_ALPHA:
            return WGPUBlendFactor_OneMinusSrcAlpha;
        case RenderBlend::DEST_ALPHA:
            return WGPUBlendFactor_DstAlpha;
        case RenderBlend::INV_DEST_ALPHA:
            return WGPUBlendFactor_OneMinusDstAlpha;
        case RenderBlend::DEST_COLOR:
            return WGPUBlendFactor_Dst;
        case RenderBlend::INV_DEST_COLOR:
            return WGPUBlendFactor_OneMinusDst;
        case RenderBlend::SRC_ALPHA_SAT:
            return WGPUBlendFactor_SrcAlphaSaturated;
        case RenderBlend::BLEND_FACTOR:
            return WGPUBlendFactor_Constant;
        case RenderBlend::INV_BLEND_FACTOR:
            return WGPUBlendFactor_OneMinusConstant;
        case RenderBlend::SRC1_COLOR:
            return WGPUBlendFactor_Src1;
        case RenderBlend::INV_SRC1_COLOR:
            return WGPUBlendFactor_OneMinusSrc1;
        case RenderBlend::SRC1_ALPHA:
            return WGPUBlendFactor_Src1Alpha;
        case RenderBlend::INV_SRC1_ALPHA:
            return WGPUBlendFactor_OneMinusSrc1Alpha;
        default:
            return WGPUBlendFactor_One;
        }
    }

    static WGPUBlendOperation toWgpu(RenderBlendOperation op) {
        switch (op) {
        case RenderBlendOperation::SUBTRACT:
            return WGPUBlendOperation_Subtract;
        case RenderBlendOperation::REV_SUBTRACT:
            return WGPUBlendOperation_ReverseSubtract;
        case RenderBlendOperation::MIN:
            return WGPUBlendOperation_Min;
        case RenderBlendOperation::MAX:
            return WGPUBlendOperation_Max;
        default:
            return WGPUBlendOperation_Add;
        }
    }

    static WGPUPrimitiveTopology toWgpu(RenderPrimitiveTopology topology) {
        switch (topology) {
        case RenderPrimitiveTopology::POINT_LIST:
            return WGPUPrimitiveTopology_PointList;
        case RenderPrimitiveTopology::LINE_LIST:
            return WGPUPrimitiveTopology_LineList;
        case RenderPrimitiveTopology::LINE_STRIP:
            return WGPUPrimitiveTopology_LineStrip;
        case RenderPrimitiveTopology::TRIANGLE_STRIP:
            return WGPUPrimitiveTopology_TriangleStrip;
        default:
            return WGPUPrimitiveTopology_TriangleList;
        }
    }

    static WGPUTextureViewDimension toWgpu(RenderTextureViewDimension dimension) {
        switch (dimension) {
        case RenderTextureViewDimension::TEXTURE_1D:
            return WGPUTextureViewDimension_1D;
        case RenderTextureViewDimension::TEXTURE_2D:
            return WGPUTextureViewDimension_2D;
        case RenderTextureViewDimension::TEXTURE_3D:
            return WGPUTextureViewDimension_3D;
        case RenderTextureViewDimension::TEXTURE_CUBE:
            return WGPUTextureViewDimension_Cube;
        default:
            return WGPUTextureViewDimension_Undefined;
        }
    }

    // WGSL reflection: the resources a converted shader declares.

    static WGPUTextureFormat wgslStorageFormat(const std::string &name) {
        static const std::pair<const char *, WGPUTextureFormat> formats[] = {
            { "rgba8unorm", WGPUTextureFormat_RGBA8Unorm }, { "rgba8snorm", WGPUTextureFormat_RGBA8Snorm }, { "rgba8uint", WGPUTextureFormat_RGBA8Uint }, { "rgba8sint", WGPUTextureFormat_RGBA8Sint },
            { "rgba16uint", WGPUTextureFormat_RGBA16Uint }, { "rgba16sint", WGPUTextureFormat_RGBA16Sint }, { "rgba16float", WGPUTextureFormat_RGBA16Float }, { "rgba16unorm", WGPUTextureFormat_RGBA16Unorm },
            { "r32uint", WGPUTextureFormat_R32Uint }, { "r32sint", WGPUTextureFormat_R32Sint }, { "r32float", WGPUTextureFormat_R32Float },
            { "rg32uint", WGPUTextureFormat_RG32Uint }, { "rg32sint", WGPUTextureFormat_RG32Sint }, { "rg32float", WGPUTextureFormat_RG32Float },
            { "rgba32uint", WGPUTextureFormat_RGBA32Uint }, { "rgba32sint", WGPUTextureFormat_RGBA32Sint }, { "rgba32float", WGPUTextureFormat_RGBA32Float },
            { "bgra8unorm", WGPUTextureFormat_BGRA8Unorm }, { "r8unorm", WGPUTextureFormat_R8Unorm }, { "r8uint", WGPUTextureFormat_R8Uint }, { "r16float", WGPUTextureFormat_R16Float },
            { "rg8unorm", WGPUTextureFormat_RG8Unorm }, { "rg16float", WGPUTextureFormat_RG16Float }, { "r16uint", WGPUTextureFormat_R16Uint }
        };

        for (const auto &format : formats) {
            if (name == format.first) {
                return format.second;
            }
        }

        return WGPUTextureFormat_Undefined;
    }

    static WGPUTextureViewDimension wgslDimension(const std::string &type) {
        if (type.find("_cube_array") != std::string::npos) {
            return WGPUTextureViewDimension_CubeArray;
        }
        else if (type.find("_cube") != std::string::npos) {
            return WGPUTextureViewDimension_Cube;
        }
        else if (type.find("_2d_array") != std::string::npos) {
            return WGPUTextureViewDimension_2DArray;
        }
        else if (type.find("_3d") != std::string::npos) {
            return WGPUTextureViewDimension_3D;
        }
        else if (type.find("_1d") != std::string::npos) {
            return WGPUTextureViewDimension_1D;
        }
        else {
            return WGPUTextureViewDimension_2D;
        }
    }

    static bool wgslIsSampled(const std::string &wgsl, const std::string &name) {
        const std::regex sampled("texture(Sample[A-Za-z]*|Gather[A-Za-z]*)\\((\\s*[0-9]+[ui]?\\s*,)?\\s*" + name + "\\s*,");
        return std::regex_search(wgsl, sampled);
    }

    static std::vector<WebGPUBinding> reflectBindings(const std::string &wgsl, WGPUShaderStage stage) {
        static const std::regex declaration("@group\\(([0-9]+)u?\\)\\s*@binding\\(([0-9]+)u?\\)\\s*var(<([^>]*)>)?\\s+([A-Za-z_0-9]+)\\s*:\\s*([^;]+);");
        std::vector<WebGPUBinding> bindings;
        for (std::sregex_iterator it(wgsl.begin(), wgsl.end(), declaration), end; it != end; ++it) {
            const std::smatch &match = *it;
            const std::string qualifier = match[4].str();
            const std::string name = match[5].str();
            const std::string type = match[6].str();
            WebGPUBinding binding;
            binding.group = uint32_t(std::stoul(match[1].str()));
            binding.entry.binding = uint32_t(std::stoul(match[2].str()));
            binding.entry.visibility = stage;

            if (qualifier == "uniform") {
                binding.entry.buffer.type = WGPUBufferBindingType_Uniform;
                binding.entry.buffer.hasDynamicOffset = (binding.group == WebGPUPushConstantSet) && (binding.entry.binding == WebGPUPushConstantBinding);
            }
            else if (qualifier.rfind("storage", 0) == 0) {
                binding.entry.buffer.type = (qualifier.find("read_write") != std::string::npos) ? WGPUBufferBindingType_Storage : WGPUBufferBindingType_ReadOnlyStorage;
            }
            else if (type == "sampler") {
                binding.entry.sampler.type = WGPUSamplerBindingType_Filtering;
            }
            else if (type == "sampler_comparison") {
                binding.entry.sampler.type = WGPUSamplerBindingType_Comparison;
            }
            else if (type.rfind("texture_storage_", 0) == 0) {
                const size_t open = type.find('<');
                const size_t comma = type.find(',', open);
                const std::string format = type.substr(open + 1, comma - open - 1);
                const std::string access = type.substr(comma + 1);
                binding.entry.storageTexture.format = wgslStorageFormat(format);
                binding.entry.storageTexture.viewDimension = wgslDimension(type);
                if (access.find("read_write") != std::string::npos) {
                    binding.entry.storageTexture.access = WGPUStorageTextureAccess_ReadWrite;
                }
                else if (access.find("read") != std::string::npos) {
                    binding.entry.storageTexture.access = WGPUStorageTextureAccess_ReadOnly;
                }
                else {
                    binding.entry.storageTexture.access = WGPUStorageTextureAccess_WriteOnly;
                }
            }
            else if (type.rfind("texture_depth", 0) == 0) {
                binding.entry.texture.sampleType = WGPUTextureSampleType_Depth;
                binding.entry.texture.viewDimension = wgslDimension(type);
                binding.entry.texture.multisampled = (type.find("multisampled") != std::string::npos);
            }
            else if (type.rfind("texture_", 0) == 0) {
                binding.entry.texture.viewDimension = wgslDimension(type);
                binding.entry.texture.multisampled = (type.find("multisampled") != std::string::npos);
                if (type.find("<u32>") != std::string::npos) {
                    binding.entry.texture.sampleType = WGPUTextureSampleType_Uint;
                }
                else if (type.find("<i32>") != std::string::npos) {
                    binding.entry.texture.sampleType = WGPUTextureSampleType_Sint;
                }
                else if (!binding.entry.texture.multisampled && wgslIsSampled(wgsl, name)) {
                    binding.entry.texture.sampleType = WGPUTextureSampleType_Float;
                }
                else {
                    binding.entry.texture.sampleType = WGPUTextureSampleType_UnfilterableFloat;
                }
            }
            else {
                continue;
            }

            bindings.push_back(binding);
        }

        return bindings;
    }

    static uint64_t fnv1a(const void *data, size_t size, uint64_t hash = 14695981039346656037ULL) {
        const uint8_t *bytes = static_cast<const uint8_t *>(data);
        for (size_t i = 0; i < size; i++) {
            hash = (hash ^ bytes[i]) * 1099511628211ULL;
        }

        return hash;
    }

    static void releaseOnMain(const std::function<void()> &release) {
        webgpuRunOnMain(release);
    }

    // WebGPUBuffer

    WebGPUBuffer::WebGPUBuffer(WebGPUDevice *device, const RenderBufferDesc &desc) {
        this->device = device;
        this->desc = desc;
        readback = (desc.heapType == RenderHeapType::READBACK);
        allocatedSize = std::max<uint64_t>((desc.size + 3) & ~uint64_t(3), 4);
        shadow.resize(allocatedSize);

        WGPUBufferDescriptor bufferDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
        bufferDesc.size = allocatedSize;
        if (readback) {
            bufferDesc.usage = WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst;
        }
        else {
            bufferDesc.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_CopySrc | WGPUBufferUsage_Storage | WGPUBufferUsage_Uniform | WGPUBufferUsage_Vertex | WGPUBufferUsage_Index;
        }

        webgpuRunOnMain([&]() {
            buffer = wgpuDeviceCreateBuffer(device->device, &bufferDesc);
        });
    }

    WebGPUBuffer::~WebGPUBuffer() {
        device->forgetBuffer(this);
        releaseOnMain([&]() {
            if (buffer != nullptr) {
                wgpuBufferDestroy(buffer);
                wgpuBufferRelease(buffer);
            }
        });
    }

    void *WebGPUBuffer::map(uint32_t subresource, const RenderRange *readRange) {
        if (!readback) {
            return shadow.data();
        }

        webgpuRunOnMainAsync([&](std::function<void()> done) {
            struct State {
                WebGPUBuffer *buffer;
                std::function<void()> done;
            };

            WGPUBufferMapCallbackInfo callbackInfo = WGPU_BUFFER_MAP_CALLBACK_INFO_INIT;
            callbackInfo.mode = WGPUCallbackMode_AllowSpontaneous;
            callbackInfo.userdata1 = new State{ this, done };
            callbackInfo.callback = [](WGPUMapAsyncStatus status, WGPUStringView message, void *userdata1, void *) {
                State *state = static_cast<State *>(userdata1);
                WebGPUBuffer *self = state->buffer;
                if (status == WGPUMapAsyncStatus_Success) {
                    const void *mapped = wgpuBufferGetConstMappedRange(self->buffer, 0, self->allocatedSize);
                    memcpy(self->shadow.data(), mapped, self->allocatedSize);
                    wgpuBufferUnmap(self->buffer);
                }
                else {
                    self->device->reportError("readback map failed: " + toString(message));
                }

                state->done();
                delete state;
            };

            wgpuBufferMapAsync(buffer, WGPUMapMode_Read, 0, allocatedSize, callbackInfo);
        });

        return shadow.data();
    }

    void WebGPUBuffer::unmap(uint32_t subresource, const RenderRange *writtenRange) {
        // shortcut: uploads the whole buffer at the next submission, ignoring writtenRange; upload ranges if big buffers make it slow.
        if (!readback) {
            device->markDirty(this);
        }
    }

    std::unique_ptr<RenderBufferFormattedView> WebGPUBuffer::createBufferFormattedView(RenderFormat format) {
        auto view = std::make_unique<WebGPUBufferFormattedView>();
        view->format = format;
        return view;
    }

    void WebGPUBuffer::setName(const std::string &name) {
    }

    uint64_t WebGPUBuffer::getDeviceAddress() const {
        return 0;
    }

    // WebGPUTexture

    WebGPUTexture::WebGPUTexture(WebGPUDevice *device, const RenderTextureDesc &desc) {
        this->device = device;
        this->desc = desc;
        format = toWgpu(desc.format);

        const uint32_t sampleCount = std::max<uint32_t>(desc.multisampling.sampleCount, 1);
        WGPUTextureDescriptor textureDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        textureDesc.format = format;
        textureDesc.mipLevelCount = std::max<uint32_t>(desc.mipLevels, 1);
        textureDesc.sampleCount = sampleCount;
        textureDesc.size.width = std::max<uint32_t>(desc.width, 1);
        textureDesc.size.height = std::max<uint32_t>(desc.height, 1);
        switch (desc.dimension) {
        case RenderTextureDimension::TEXTURE_1D:
            textureDesc.dimension = WGPUTextureDimension_1D;
            textureDesc.size.height = 1;
            textureDesc.size.depthOrArrayLayers = 1;
            break;
        case RenderTextureDimension::TEXTURE_3D:
            textureDesc.dimension = WGPUTextureDimension_3D;
            textureDesc.size.depthOrArrayLayers = std::max<uint32_t>(desc.depth, 1);
            break;
        default:
            textureDesc.dimension = WGPUTextureDimension_2D;
            textureDesc.size.depthOrArrayLayers = std::max<uint32_t>(desc.arraySize, 1);
            break;
        }

        textureDesc.usage = WGPUTextureUsage_TextureBinding;
        if (sampleCount == 1) {
            textureDesc.usage |= WGPUTextureUsage_CopySrc | WGPUTextureUsage_CopyDst;
        }

        if (desc.flags & (RenderTextureFlag::RENDER_TARGET | RenderTextureFlag::DEPTH_TARGET)) {
            textureDesc.usage |= WGPUTextureUsage_RenderAttachment;
        }

        if ((desc.flags & (RenderTextureFlag::STORAGE | RenderTextureFlag::UNORDERED_ACCESS)) && (sampleCount == 1)) {
            textureDesc.usage |= WGPUTextureUsage_StorageBinding;
        }

        webgpuRunOnMain([&]() {
            texture = wgpuDeviceCreateTexture(device->device, &textureDesc);
        });
    }

    WebGPUTexture::~WebGPUTexture() {
        if (device == nullptr) {
            return;
        }

        releaseOnMain([&]() {
            if (defaultView != nullptr) {
                wgpuTextureViewRelease(defaultView);
            }

            if (owned && (texture != nullptr)) {
                wgpuTextureDestroy(texture);
                wgpuTextureRelease(texture);
            }
        });
    }

    std::unique_ptr<RenderTextureView> WebGPUTexture::createTextureView(const RenderTextureViewDesc &desc) const {
        return std::make_unique<WebGPUTextureView>(this, desc);
    }

    void WebGPUTexture::setName(const std::string &name) {
    }

    WGPUTextureView WebGPUTexture::getDefaultView() const {
        if (defaultView == nullptr) {
            webgpuRunOnMain([&]() {
                if (defaultView != nullptr) {
                    return;
                }

                WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
                viewDesc.aspect = hasStencil(format) ? WGPUTextureAspect_DepthOnly : WGPUTextureAspect_All;
                defaultView = wgpuTextureCreateView(texture, &viewDesc);
            });
        }

        return defaultView;
    }

    // WebGPUTextureView

    WebGPUTextureView::WebGPUTextureView(const WebGPUTexture *texture, const RenderTextureViewDesc &desc) {
        this->texture = texture;

        WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
        viewDesc.format = texture->format;
        viewDesc.dimension = toWgpu(desc.dimension);
        viewDesc.baseMipLevel = desc.mipSlice;
        viewDesc.mipLevelCount = (desc.mipLevels == UINT32_MAX) ? WGPU_MIP_LEVEL_COUNT_UNDEFINED : desc.mipLevels;
        viewDesc.baseArrayLayer = desc.arrayIndex;
        viewDesc.arrayLayerCount = (desc.arraySize == UINT32_MAX) ? WGPU_ARRAY_LAYER_COUNT_UNDEFINED : desc.arraySize;
        viewDesc.aspect = hasStencil(texture->format) ? WGPUTextureAspect_DepthOnly : WGPUTextureAspect_All;
        webgpuRunOnMain([&]() {
            view = wgpuTextureCreateView(texture->texture, &viewDesc);
        });
    }

    WebGPUTextureView::~WebGPUTextureView() {
        releaseOnMain([&]() {
            if (view != nullptr) {
                wgpuTextureViewRelease(view);
            }
        });
    }

    // WebGPUShader

    WebGPUShader::WebGPUShader(const void *data, uint64_t size, const char *entryPointName) {
        spirv.resize(size / sizeof(uint32_t));
        memcpy(spirv.data(), data, spirv.size() * sizeof(uint32_t));
        this->entryPointName = (entryPointName != nullptr) ? entryPointName : "main";
    }

    void WebGPUShader::setName(const std::string &name) {
    }

    // WebGPUSampler

    WebGPUSampler::WebGPUSampler(WebGPUDevice *device, const RenderSamplerDesc &desc) {
        WGPUSamplerDescriptor samplerDesc = WGPU_SAMPLER_DESCRIPTOR_INIT;
        samplerDesc.addressModeU = toWgpu(desc.addressU);
        samplerDesc.addressModeV = toWgpu(desc.addressV);
        samplerDesc.addressModeW = toWgpu(desc.addressW);
        samplerDesc.magFilter = (desc.magFilter == RenderFilter::LINEAR) ? WGPUFilterMode_Linear : WGPUFilterMode_Nearest;
        samplerDesc.minFilter = (desc.minFilter == RenderFilter::LINEAR) ? WGPUFilterMode_Linear : WGPUFilterMode_Nearest;
        samplerDesc.mipmapFilter = (desc.mipmapMode == RenderMipmapMode::LINEAR) ? WGPUMipmapFilterMode_Linear : WGPUMipmapFilterMode_Nearest;
        samplerDesc.lodMinClamp = desc.minLOD;
        samplerDesc.lodMaxClamp = std::min(desc.maxLOD, 32.0f);
        if (desc.comparisonEnabled) {
            samplerDesc.compare = toWgpu(desc.comparisonFunc);
        }

        // WebGPU allows anisotropy only with every filter linear.
        const bool allLinear = (samplerDesc.magFilter == WGPUFilterMode_Linear) && (samplerDesc.minFilter == WGPUFilterMode_Linear) && (samplerDesc.mipmapFilter == WGPUMipmapFilterMode_Linear);
        samplerDesc.maxAnisotropy = (desc.anisotropyEnabled && allLinear) ? uint16_t(std::clamp<uint32_t>(desc.maxAnisotropy, 1, 16)) : 1;
        webgpuRunOnMain([&]() {
            sampler = wgpuDeviceCreateSampler(device->device, &samplerDesc);
        });
    }

    WebGPUSampler::~WebGPUSampler() {
        releaseOnMain([&]() {
            if (sampler != nullptr) {
                wgpuSamplerRelease(sampler);
            }
        });
    }

    // WebGPUDescriptorSet

    WebGPUDescriptorSet::WebGPUDescriptorSet(WebGPUDevice *device, const RenderDescriptorSetDesc &desc) {
        this->device = device;

        uint32_t total = 0;
        for (uint32_t i = 0; i < desc.descriptorRangesCount; i++) {
            const RenderDescriptorRange &range = desc.descriptorRanges[i];
            uint32_t count = range.count;
            if (desc.lastRangeIsBoundless && (i == (desc.descriptorRangesCount - 1))) {
                count = desc.boundlessRangeSize;
            }

            ranges.push_back({ range.type, range.binding, count, total });
            total += count;
        }

        descriptors.resize(total);

        for (uint32_t i = 0; i < desc.descriptorRangesCount; i++) {
            const RenderDescriptorRange &range = desc.descriptorRanges[i];
            if (range.immutableSampler == nullptr) {
                continue;
            }

            for (uint32_t j = 0; j < range.count; j++) {
                const WebGPUSampler *sampler = static_cast<const WebGPUSampler *>(range.immutableSampler[j]);
                descriptors[ranges[i].first + j].sampler = (sampler != nullptr) ? sampler->sampler : nullptr;
            }
        }
    }

    WebGPUDescriptorSet::~WebGPUDescriptorSet() {
        releaseOnMain([&]() {
            for (auto &entry : groups) {
                wgpuBindGroupRelease(entry.second.group);
            }
        });
    }

    const WebGPUDescriptorSet::Range *WebGPUDescriptorSet::rangeOf(uint32_t descriptorIndex) const {
        for (const Range &range : ranges) {
            if ((descriptorIndex >= range.first) && (descriptorIndex < (range.first + range.count))) {
                return &range;
            }
        }

        return nullptr;
    }

    const WebGPUDescriptor *WebGPUDescriptorSet::find(uint32_t binding) const {
        for (const Range &range : ranges) {
            if ((binding >= range.binding) && (binding < (range.binding + range.count))) {
                return &descriptors[range.first + (binding - range.binding)];
            }
        }

        return nullptr;
    }

    void WebGPUDescriptorSet::setBuffer(uint32_t descriptorIndex, const RenderBuffer *buffer, uint64_t bufferSize, const RenderBufferStructuredView *bufferStructuredView, const RenderBufferFormattedView *bufferFormattedView) {
        assert(descriptorIndex < descriptors.size());

        WebGPUDescriptor &descriptor = descriptors[descriptorIndex];
        descriptor.buffer = static_cast<const WebGPUBuffer *>(buffer);
        descriptor.offset = (bufferStructuredView != nullptr) ? uint64_t(bufferStructuredView->firstElement) * bufferStructuredView->structureByteStride : 0;
        descriptor.size = bufferSize;
        generation++;
    }

    void WebGPUDescriptorSet::setTexture(uint32_t descriptorIndex, const RenderTexture *texture, RenderTextureLayout textureLayout, const RenderTextureView *textureView) {
        assert(descriptorIndex < descriptors.size());

        WebGPUDescriptor &descriptor = descriptors[descriptorIndex];
        if (textureView != nullptr) {
            descriptor.view = static_cast<const WebGPUTextureView *>(textureView)->view;
        }
        else if (texture != nullptr) {
            descriptor.view = static_cast<const WebGPUTexture *>(texture)->getDefaultView();
        }
        else {
            descriptor.view = nullptr;
        }

        generation++;
    }

    void WebGPUDescriptorSet::setSampler(uint32_t descriptorIndex, const RenderSampler *sampler) {
        assert(descriptorIndex < descriptors.size());

        descriptors[descriptorIndex].sampler = (sampler != nullptr) ? static_cast<const WebGPUSampler *>(sampler)->sampler : nullptr;
        generation++;
    }

    void WebGPUDescriptorSet::setAccelerationStructure(uint32_t descriptorIndex, const RenderAccelerationStructure *accelerationStructure) {
    }

    // WebGPUPipelineLayout

    WebGPUPipelineLayout::WebGPUPipelineLayout(WebGPUDevice *device, const RenderPipelineLayoutDesc &desc) {
        for (uint32_t i = 0; i < desc.pushConstantRangesCount; i++) {
            const RenderPushConstantRange &range = desc.pushConstantRanges[i];
            pushConstantRanges.push_back(range);
            pushConstantSize = std::max(pushConstantSize, range.offset + range.size);
        }

        for (uint32_t i = 0; i < desc.descriptorSetDescsCount; i++) {
            const RenderDescriptorSetDesc &setDesc = desc.descriptorSetDescs[i];
            for (uint32_t j = 0; j < setDesc.descriptorRangesCount; j++) {
                const RenderDescriptorRange &range = setDesc.descriptorRanges[j];
                if (range.immutableSampler == nullptr) {
                    continue;
                }

                for (uint32_t k = 0; k < range.count; k++) {
                    const WebGPUSampler *sampler = static_cast<const WebGPUSampler *>(range.immutableSampler[k]);
                    if (sampler != nullptr) {
                        immutableSamplers[(uint64_t(i) << 32) | (range.binding + k)] = sampler->sampler;
                    }
                }
            }
        }
    }

    // WebGPUPipeline

    WebGPUPipeline::WebGPUPipeline(Type type) {
        this->type = type;
    }

    WebGPUPipeline::~WebGPUPipeline() {
        releaseOnMain([&]() {
            if (compute != nullptr) {
                wgpuComputePipelineRelease(compute);
            }

            if (render != nullptr) {
                wgpuRenderPipelineRelease(render);
            }

            if (layout != nullptr) {
                wgpuPipelineLayoutRelease(layout);
            }

            for (WGPUBindGroupLayout groupLayout : groupLayouts) {
                wgpuBindGroupLayoutRelease(groupLayout);
            }
        });
    }

    void WebGPUPipeline::setName(const std::string &name) {
    }

    RenderPipelineProgram WebGPUPipeline::getProgram(const std::string &name) const {
        return RenderPipelineProgram();
    }

    // Builds the bind group layouts and the pipeline layout from the stages' reflected bindings. Main thread.
    static void buildLayouts(WebGPUDevice *device, WebGPUPipeline *pipeline, const std::vector<const WebGPUModule *> &modules) {
        std::map<uint32_t, std::map<uint32_t, WGPUBindGroupLayoutEntry>> groups;
        for (const WebGPUModule *module : modules) {
            for (const WebGPUBinding &binding : module->bindings) {
                auto &entries = groups[binding.group];
                auto it = entries.find(binding.entry.binding);
                if (it == entries.end()) {
                    entries[binding.entry.binding] = binding.entry;
                }
                else {
                    it->second.visibility |= binding.entry.visibility;
                }
            }
        }

        const uint32_t groupCount = groups.empty() ? 0 : (groups.rbegin()->first + 1);
        pipeline->groupEntries.resize(groupCount);
        for (uint32_t g = 0; g < groupCount; g++) {
            for (const auto &entry : groups[g]) {
                pipeline->groupEntries[g].push_back(entry.second);
            }

            WGPUBindGroupLayoutDescriptor groupDesc = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
            groupDesc.entryCount = pipeline->groupEntries[g].size();
            groupDesc.entries = pipeline->groupEntries[g].data();
            pipeline->groupLayouts.push_back(wgpuDeviceCreateBindGroupLayout(device->device, &groupDesc));
        }

        WGPUPipelineLayoutDescriptor layoutDesc = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
        layoutDesc.bindGroupLayoutCount = pipeline->groupLayouts.size();
        layoutDesc.bindGroupLayouts = pipeline->groupLayouts.data();
        pipeline->layout = wgpuDeviceCreatePipelineLayout(device->device, &layoutDesc);
    }

    // Runs create inside a validation error scope on the main thread and records any error on the pipeline.
    static void createWithErrorScope(WebGPUDevice *device, WebGPUPipeline *pipeline, const std::function<void()> &create) {
        webgpuRunOnMainAsync([&](std::function<void()> done) {
            wgpuDevicePushErrorScope(device->device, WGPUErrorFilter_Validation);
            create();

            struct State {
                WebGPUPipeline *pipeline;
                std::function<void()> done;
            };

            WGPUPopErrorScopeCallbackInfo callbackInfo = WGPU_POP_ERROR_SCOPE_CALLBACK_INFO_INIT;
            callbackInfo.mode = WGPUCallbackMode_AllowSpontaneous;
            callbackInfo.userdata1 = new State{ pipeline, done };
            callbackInfo.callback = [](WGPUPopErrorScopeStatus status, WGPUErrorType type, WGPUStringView message, void *userdata1, void *) {
                State *state = static_cast<State *>(userdata1);
                if ((status == WGPUPopErrorScopeStatus_Success) && (type != WGPUErrorType_NoError)) {
                    state->pipeline->valid = false;
                    if (state->pipeline->error.empty()) {
                        state->pipeline->error = toString(message);
                    }
                }

                state->done();
                delete state;
            };

            wgpuDevicePopErrorScope(device->device, callbackInfo);
        });

        if (!pipeline->valid) {
            fprintf(stderr, "[webgpu] pipeline creation failed: %s\n", pipeline->error.c_str());
        }
    }

    // WebGPUFramebuffer

    static WGPUTextureView createAttachmentView(const WebGPUTexture *texture) {
        WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
        viewDesc.dimension = WGPUTextureViewDimension_2D;
        viewDesc.mipLevelCount = 1;
        viewDesc.arrayLayerCount = 1;
        return wgpuTextureCreateView(texture->texture, &viewDesc);
    }

    WebGPUFramebuffer::WebGPUFramebuffer(WebGPUDevice *device, const RenderFramebufferDesc &desc) {
        depthReadOnly = desc.depthAttachmentReadOnly;
        webgpuRunOnMain([&]() {
            for (uint32_t i = 0; i < desc.colorAttachmentsCount; i++) {
                const WebGPUTexture *texture = static_cast<const WebGPUTexture *>(desc.colorAttachments[i]);
                WGPUTextureView view = createAttachmentView(texture);
                ownedViews.push_back(view);
                colorViews.push_back(view);
                colorFormats.push_back(texture->format);
                width = texture->desc.width;
                height = texture->desc.height;
                sampleCount = std::max<uint32_t>(texture->desc.multisampling.sampleCount, 1);
            }

            if (desc.depthAttachment != nullptr) {
                const WebGPUTexture *texture = static_cast<const WebGPUTexture *>(desc.depthAttachment);
                depthView = createAttachmentView(texture);
                ownedViews.push_back(depthView);
                depthFormat = texture->format;
                width = texture->desc.width;
                height = texture->desc.height;
                sampleCount = std::max<uint32_t>(texture->desc.multisampling.sampleCount, 1);
            }
        });
    }

    WebGPUFramebuffer::~WebGPUFramebuffer() {
        releaseOnMain([&]() {
            for (WGPUTextureView view : ownedViews) {
                wgpuTextureViewRelease(view);
            }
        });
    }

    uint32_t WebGPUFramebuffer::getWidth() const {
        return width;
    }

    uint32_t WebGPUFramebuffer::getHeight() const {
        return height;
    }

    // Replay state for one submission, on the main thread.

    struct WebGPUBindPoint {
        const WebGPUPipelineLayout *layout = nullptr;
        WebGPUDescriptorSet *sets[8] = {};
        uint8_t pushConstants[WebGPUPushConstantStride] = {};
        bool pushDirty = true;
        uint32_t pushOffset = 0;
        bool groupsDirty = true;
    };

    struct WebGPUVertexBinding {
        const WebGPUBuffer *buffer = nullptr;
        uint64_t offset = 0;
        uint64_t size = 0;
    };

    struct WebGPUContext {
        WebGPUDevice *device = nullptr;
        WGPUCommandEncoder encoder = nullptr;
        WGPURenderPassEncoder renderPass = nullptr;
        WGPUComputePassEncoder computePass = nullptr;
        const WebGPUFramebuffer *framebuffer = nullptr;
        std::vector<bool> colorClearPending;
        std::vector<WGPUColor> colorClearValues;
        bool depthClearPending = false;
        float depthClearValue = 1.0f;
        bool stencilClearPending = false;
        uint32_t stencilClearValue = 0;
        WebGPUBindPoint graphics;
        WebGPUBindPoint compute;
        const WebGPUPipeline *graphicsPipeline = nullptr;
        const WebGPUPipeline *computePipeline = nullptr;
        bool graphicsPipelineDirty = true;
        bool computePipelineDirty = true;
        WebGPUVertexBinding vertexBuffers[16];
        bool vertexBuffersDirty = true;
        WebGPUVertexBinding indexBuffer;
        WGPUIndexFormat indexFormat = WGPUIndexFormat_Uint32;
        bool indexBufferDirty = true;
        RenderViewport viewport;
        bool viewportSet = false;
        bool viewportDirty = true;
        RenderRect scissor;
        bool scissorSet = false;
        bool scissorDirty = true;
        std::vector<uint8_t> pushStaging;

        void endComputePass() {
            if (computePass != nullptr) {
                wgpuComputePassEncoderEnd(computePass);
                wgpuComputePassEncoderRelease(computePass);
                computePass = nullptr;
            }
        }

        bool hasPendingClears() const {
            return depthClearPending || stencilClearPending || std::find(colorClearPending.begin(), colorClearPending.end(), true) != colorClearPending.end();
        }

        // Ends any open pass; clears requested with no draw after them still run in an empty pass.
        void endPasses() {
            endComputePass();
            if ((renderPass == nullptr) && (framebuffer != nullptr) && hasPendingClears()) {
                beginRenderPass();
            }

            if (renderPass != nullptr) {
                wgpuRenderPassEncoderEnd(renderPass);
                wgpuRenderPassEncoderRelease(renderPass);
                renderPass = nullptr;
            }
        }

        void beginRenderPass() {
            endComputePass();

            std::vector<WGPURenderPassColorAttachment> colors(framebuffer->colorViews.size());
            for (size_t i = 0; i < colors.size(); i++) {
                colors[i] = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
                colors[i].view = framebuffer->colorViews[i];
                colors[i].depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
                colors[i].loadOp = colorClearPending[i] ? WGPULoadOp_Clear : WGPULoadOp_Load;
                colors[i].storeOp = WGPUStoreOp_Store;
                colors[i].clearValue = colorClearValues[i];
                colorClearPending[i] = false;
            }

            WGPURenderPassDepthStencilAttachment depth = WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;
            WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
            passDesc.colorAttachmentCount = colors.size();
            passDesc.colorAttachments = colors.data();
            if (framebuffer->depthView != nullptr) {
                depth.view = framebuffer->depthView;
                if (framebuffer->depthReadOnly) {
                    depth.depthReadOnly = true;
                }
                else {
                    depth.depthLoadOp = depthClearPending ? WGPULoadOp_Clear : WGPULoadOp_Load;
                    depth.depthStoreOp = WGPUStoreOp_Store;
                    depth.depthClearValue = depthClearValue;
                }

                if (hasStencil(framebuffer->depthFormat)) {
                    depth.stencilLoadOp = stencilClearPending ? WGPULoadOp_Clear : WGPULoadOp_Load;
                    depth.stencilStoreOp = WGPUStoreOp_Store;
                    depth.stencilClearValue = stencilClearValue;
                }

                passDesc.depthStencilAttachment = &depth;
            }

            depthClearPending = false;
            stencilClearPending = false;
            renderPass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
            graphicsPipelineDirty = true;
            graphics.groupsDirty = true;
            vertexBuffersDirty = true;
            indexBufferDirty = true;
            viewportDirty = true;
            scissorDirty = true;
        }

        void beginComputePass() {
            if (computePass != nullptr) {
                return;
            }

            endPasses();

            WGPUComputePassDescriptor passDesc = WGPU_COMPUTE_PASS_DESCRIPTOR_INIT;
            computePass = wgpuCommandEncoderBeginComputePass(encoder, &passDesc);
            computePipelineDirty = true;
            compute.groupsDirty = true;
        }

        uint32_t pushOffsetFor(WebGPUBindPoint &bindPoint) {
            if (bindPoint.pushDirty) {
                bindPoint.pushOffset = uint32_t(pushStaging.size());
                pushStaging.insert(pushStaging.end(), bindPoint.pushConstants, bindPoint.pushConstants + WebGPUPushConstantStride);
                bindPoint.pushDirty = false;
            }

            return bindPoint.pushOffset;
        }

        WGPUBindGroup bindGroupFor(const WebGPUPipeline *pipeline, const WebGPUBindPoint &bindPoint, uint32_t groupIndex) {
            const WGPUBindGroupLayout groupLayout = pipeline->groupLayouts[groupIndex];
            WebGPUDescriptorSet *set = (groupIndex < 8) ? bindPoint.sets[groupIndex] : nullptr;
            WebGPUDescriptorSet::CachedGroup &cached = (set != nullptr) ? set->groups[groupLayout] : device->setlessGroups[groupLayout];
            const uint64_t generation = (set != nullptr) ? set->generation.load() : 0;
            if ((cached.group != nullptr) && (cached.generation == generation) && (cached.ringGeneration == device->pushRingGeneration)) {
                return cached.group;
            }

            const std::vector<WGPUBindGroupLayoutEntry> &layoutEntries = pipeline->groupEntries[groupIndex];
            std::vector<WGPUBindGroupEntry> entries;
            for (const WGPUBindGroupLayoutEntry &layoutEntry : layoutEntries) {
                WGPUBindGroupEntry entry = WGPU_BIND_GROUP_ENTRY_INIT;
                entry.binding = layoutEntry.binding;
                if ((groupIndex == WebGPUPushConstantSet) && (layoutEntry.binding == WebGPUPushConstantBinding)) {
                    entry.buffer = device->pushRing;
                    entry.size = WebGPUPushConstantStride;
                    entries.push_back(entry);
                    continue;
                }

                const WebGPUDescriptor *descriptor = (set != nullptr) ? set->find(layoutEntry.binding) : nullptr;
                if (layoutEntry.buffer.type != WGPUBufferBindingType_BindingNotUsed) {
                    if ((descriptor != nullptr) && (descriptor->buffer != nullptr)) {
                        entry.buffer = descriptor->buffer->buffer;
                        entry.offset = descriptor->offset;
                        entry.size = (descriptor->size > 0) ? descriptor->size : WGPU_WHOLE_SIZE;
                    }
                }
                else if (layoutEntry.sampler.type != WGPUSamplerBindingType_BindingNotUsed) {
                    if ((descriptor != nullptr) && (descriptor->sampler != nullptr)) {
                        entry.sampler = descriptor->sampler;
                    }
                    else if (bindPoint.layout != nullptr) {
                        auto it = bindPoint.layout->immutableSamplers.find((uint64_t(groupIndex) << 32) | layoutEntry.binding);
                        if (it != bindPoint.layout->immutableSamplers.end()) {
                            entry.sampler = it->second;
                        }
                    }
                }
                else if (descriptor != nullptr) {
                    entry.textureView = descriptor->view;
                }

                if ((entry.buffer == nullptr) && (entry.sampler == nullptr) && (entry.textureView == nullptr)) {
                    device->reportError("set " + std::to_string(groupIndex) + " binding " + std::to_string(layoutEntry.binding) + " has nothing bound; the draw is skipped");
                    return nullptr;
                }

                entries.push_back(entry);
            }

            WGPUBindGroupDescriptor groupDesc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
            groupDesc.layout = groupLayout;
            groupDesc.entryCount = entries.size();
            groupDesc.entries = entries.data();
            if (cached.group != nullptr) {
                wgpuBindGroupRelease(cached.group);
            }

            cached.group = wgpuDeviceCreateBindGroup(device->device, &groupDesc);
            cached.generation = generation;
            cached.ringGeneration = device->pushRingGeneration;
            return cached.group;
        }

        // Binds every group the pipeline uses; false skips the draw or dispatch.
        template <typename SetBindGroup>
        bool bindGroups(const WebGPUPipeline *pipeline, WebGPUBindPoint &bindPoint, SetBindGroup setBindGroup) {
            for (uint32_t g = 0; g < pipeline->groupLayouts.size(); g++) {
                const bool usesPush = (g == WebGPUPushConstantSet) && std::any_of(pipeline->groupEntries[g].begin(), pipeline->groupEntries[g].end(), [](const WGPUBindGroupLayoutEntry &entry) {
                    return entry.buffer.hasDynamicOffset;
                });

                if (!bindPoint.groupsDirty && !(usesPush && bindPoint.pushDirty)) {
                    continue;
                }

                WGPUBindGroup group = bindGroupFor(pipeline, bindPoint, g);
                if (group == nullptr) {
                    return false;
                }

                if (usesPush) {
                    const uint32_t offset = pushOffsetFor(bindPoint);
                    setBindGroup(g, group, 1, &offset);
                }
                else {
                    setBindGroup(g, group, 0, nullptr);
                }
            }

            bindPoint.groupsDirty = false;
            return true;
        }

        bool flushGraphics() {
            if ((graphicsPipeline == nullptr) || !graphicsPipeline->valid || (framebuffer == nullptr)) {
                return false;
            }

            if (renderPass == nullptr) {
                beginRenderPass();
            }

            if (graphicsPipelineDirty) {
                wgpuRenderPassEncoderSetPipeline(renderPass, graphicsPipeline->render);
                wgpuRenderPassEncoderSetStencilReference(renderPass, graphicsPipeline->stencilReference);
                graphicsPipelineDirty = false;
                graphics.groupsDirty = true;
            }

            const bool bound = bindGroups(graphicsPipeline, graphics, [&](uint32_t g, WGPUBindGroup group, size_t offsetCount, const uint32_t *offsets) {
                wgpuRenderPassEncoderSetBindGroup(renderPass, g, group, offsetCount, offsets);
            });

            if (!bound) {
                return false;
            }

            if (vertexBuffersDirty) {
                for (uint32_t i = 0; i < 16; i++) {
                    if (vertexBuffers[i].buffer != nullptr) {
                        wgpuRenderPassEncoderSetVertexBuffer(renderPass, i, vertexBuffers[i].buffer->buffer, vertexBuffers[i].offset, (vertexBuffers[i].size > 0) ? vertexBuffers[i].size : WGPU_WHOLE_SIZE);
                    }
                }

                vertexBuffersDirty = false;
            }

            if (indexBufferDirty && (indexBuffer.buffer != nullptr)) {
                wgpuRenderPassEncoderSetIndexBuffer(renderPass, indexBuffer.buffer->buffer, indexFormat, indexBuffer.offset, (indexBuffer.size > 0) ? indexBuffer.size : WGPU_WHOLE_SIZE);
                indexBufferDirty = false;
            }

            if (viewportDirty && viewportSet) {
                wgpuRenderPassEncoderSetViewport(renderPass, viewport.x, viewport.y, viewport.width, viewport.height, viewport.minDepth, viewport.maxDepth);
                viewportDirty = false;
            }

            if (scissorDirty) {
                int32_t left = 0;
                int32_t top = 0;
                int32_t right = int32_t(framebuffer->width);
                int32_t bottom = int32_t(framebuffer->height);
                if (scissorSet) {
                    left = std::clamp(scissor.left, 0, right);
                    top = std::clamp(scissor.top, 0, bottom);
                    right = std::clamp(scissor.right, left, right);
                    bottom = std::clamp(scissor.bottom, top, bottom);
                }

                wgpuRenderPassEncoderSetScissorRect(renderPass, uint32_t(left), uint32_t(top), uint32_t(right - left), uint32_t(bottom - top));
                scissorDirty = false;
            }

            return true;
        }

        WGPURenderPipeline rectClearPipeline(int32_t colorIndex) {
            auto &rectClear = device->rectClear;
            if (rectClear.module == nullptr) {
                std::string wgsl =
                    "struct Clear { color : vec4<f32>, depth : f32 }\n"
                    "@group(0) @binding(0) var<uniform> clearValue : Clear;\n"
                    "@vertex fn vs(@builtin(vertex_index) i : u32) -> @builtin(position) vec4<f32> {\n"
                    "  let uv = vec2<f32>(f32((i << 1u) & 2u), f32(i & 2u));\n"
                    "  return vec4<f32>(uv * 2.0 - 1.0, clearValue.depth, 1.0);\n"
                    "}\n";

                for (uint32_t i = 0; i < 8; i++) {
                    wgsl += "@fragment fn fs" + std::to_string(i) + "() -> @location(" + std::to_string(i) + ") vec4<f32> { return clearValue.color; }\n";
                }

                WGPUShaderSourceWGSL source = WGPU_SHADER_SOURCE_WGSL_INIT;
                source.code = toView(wgsl);

                WGPUShaderModuleDescriptor moduleDesc = WGPU_SHADER_MODULE_DESCRIPTOR_INIT;
                moduleDesc.nextInChain = &source.chain;
                rectClear.module = wgpuDeviceCreateShaderModule(device->device, &moduleDesc);

                WGPUBindGroupLayoutEntry entry = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
                entry.binding = 0;
                entry.visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
                entry.buffer.type = WGPUBufferBindingType_Uniform;
                entry.buffer.hasDynamicOffset = true;

                WGPUBindGroupLayoutDescriptor groupDesc = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
                groupDesc.entryCount = 1;
                groupDesc.entries = &entry;
                rectClear.groupLayout = wgpuDeviceCreateBindGroupLayout(device->device, &groupDesc);

                WGPUPipelineLayoutDescriptor layoutDesc = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
                layoutDesc.bindGroupLayoutCount = 1;
                layoutDesc.bindGroupLayouts = &rectClear.groupLayout;
                rectClear.layout = wgpuDeviceCreatePipelineLayout(device->device, &layoutDesc);
            }

            std::string key = std::to_string(colorIndex) + ":" + std::to_string(framebuffer->depthFormat) + ":" + std::to_string(framebuffer->sampleCount);
            for (WGPUTextureFormat format : framebuffer->colorFormats) {
                key += ":" + std::to_string(format);
            }

            auto it = rectClear.pipelines.find(key);
            if (it != rectClear.pipelines.end()) {
                return it->second;
            }

            const std::string fragmentEntry = "fs" + std::to_string(std::max(colorIndex, 0));
            std::vector<WGPUColorTargetState> targets(framebuffer->colorFormats.size());
            for (size_t i = 0; i < targets.size(); i++) {
                targets[i] = WGPU_COLOR_TARGET_STATE_INIT;
                targets[i].format = framebuffer->colorFormats[i];
                targets[i].writeMask = (int32_t(i) == colorIndex) ? WGPUColorWriteMask_All : WGPUColorWriteMask_None;
            }

            WGPURenderPipelineDescriptor pipelineDesc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
            pipelineDesc.layout = rectClear.layout;
            pipelineDesc.vertex.module = rectClear.module;
            pipelineDesc.vertex.entryPoint = toView("vs");
            pipelineDesc.multisample.count = framebuffer->sampleCount;

            WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
            if (!targets.empty()) {
                fragment.module = rectClear.module;
                fragment.entryPoint = toView(fragmentEntry);
                fragment.targetCount = targets.size();
                fragment.targets = targets.data();
                pipelineDesc.fragment = &fragment;
            }

            WGPUDepthStencilState depthStencil = WGPU_DEPTH_STENCIL_STATE_INIT;
            if (framebuffer->depthView != nullptr) {
                depthStencil.format = framebuffer->depthFormat;
                depthStencil.depthWriteEnabled = (colorIndex < 0) ? WGPUOptionalBool_True : WGPUOptionalBool_False;
                depthStencil.depthCompare = WGPUCompareFunction_Always;
                depthStencil.stencilReadMask = 0;
                depthStencil.stencilWriteMask = 0;
                pipelineDesc.depthStencil = &depthStencil;
            }

            WGPURenderPipeline pipeline = wgpuDeviceCreateRenderPipeline(device->device, &pipelineDesc);
            rectClear.pipelines[key] = pipeline;
            return pipeline;
        }

        // Clears rects of one color attachment, or of depth when colorIndex is negative, by drawing scissored triangles.
        void clearRects(int32_t colorIndex, WGPUColor color, float depth, const std::vector<RenderRect> &rects) {
            if (renderPass == nullptr) {
                beginRenderPass();
            }

            WGPURenderPipeline pipeline = rectClearPipeline(colorIndex);
            auto &rectClear = device->rectClear;
            if ((rectClear.group.group == nullptr) || (rectClear.group.ringGeneration != device->pushRingGeneration)) {
                if (rectClear.group.group != nullptr) {
                    wgpuBindGroupRelease(rectClear.group.group);
                }

                WGPUBindGroupEntry entry = WGPU_BIND_GROUP_ENTRY_INIT;
                entry.binding = 0;
                entry.buffer = device->pushRing;
                entry.size = WebGPUPushConstantStride;

                WGPUBindGroupDescriptor groupDesc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
                groupDesc.layout = rectClear.groupLayout;
                groupDesc.entryCount = 1;
                groupDesc.entries = &entry;
                rectClear.group.group = wgpuDeviceCreateBindGroup(device->device, &groupDesc);
                rectClear.group.ringGeneration = device->pushRingGeneration;
            }

            uint8_t block[WebGPUPushConstantStride] = {};
            const float values[5] = { float(color.r), float(color.g), float(color.b), float(color.a), depth };
            memcpy(block, values, sizeof(values));

            const uint32_t offset = uint32_t(pushStaging.size());
            pushStaging.insert(pushStaging.end(), block, block + sizeof(block));

            wgpuRenderPassEncoderSetPipeline(renderPass, pipeline);
            wgpuRenderPassEncoderSetBindGroup(renderPass, 0, rectClear.group.group, 1, &offset);
            wgpuRenderPassEncoderSetViewport(renderPass, 0.0f, 0.0f, float(framebuffer->width), float(framebuffer->height), 0.0f, 1.0f);
            for (const RenderRect &rect : rects) {
                const int32_t left = std::clamp(rect.left, 0, int32_t(framebuffer->width));
                const int32_t top = std::clamp(rect.top, 0, int32_t(framebuffer->height));
                const int32_t right = std::clamp(rect.right, left, int32_t(framebuffer->width));
                const int32_t bottom = std::clamp(rect.bottom, top, int32_t(framebuffer->height));
                if ((right > left) && (bottom > top)) {
                    wgpuRenderPassEncoderSetScissorRect(renderPass, uint32_t(left), uint32_t(top), uint32_t(right - left), uint32_t(bottom - top));
                    wgpuRenderPassEncoderDraw(renderPass, 3, 1, 0, 0);
                }
            }

            graphicsPipelineDirty = true;
            graphics.groupsDirty = true;
            viewportDirty = true;
            scissorDirty = true;
        }

        bool flushCompute() {
            if ((computePipeline == nullptr) || !computePipeline->valid) {
                return false;
            }

            beginComputePass();
            if (computePipelineDirty) {
                wgpuComputePassEncoderSetPipeline(computePass, computePipeline->compute);
                computePipelineDirty = false;
                compute.groupsDirty = true;
            }

            return bindGroups(computePipeline, compute, [&](uint32_t g, WGPUBindGroup group, size_t offsetCount, const uint32_t *offsets) {
                wgpuComputePassEncoderSetBindGroup(computePass, g, group, offsetCount, offsets);
            });
        }
    };

    // WebGPUCommandList

    WebGPUCommandList::WebGPUCommandList(WebGPUDevice *device) {
        this->device = device;
    }

    void WebGPUCommandList::begin() {
        commands.clear();
        graphicsLayout = nullptr;
        computeLayout = nullptr;
        pushFlushes = 0;
    }

    void WebGPUCommandList::end() {
    }

    void WebGPUCommandList::barriers(RenderBarrierStages stages, const RenderBufferBarrier *bufferBarriers, uint32_t bufferBarriersCount, const RenderTextureBarrier *textureBarriers, uint32_t textureBarriersCount) {
        // WebGPU tracks resource usage itself.
    }

    void WebGPUCommandList::dispatch(uint32_t threadGroupCountX, uint32_t threadGroupCountY, uint32_t threadGroupCountZ) {
        pushFlushes++;
        commands.emplace_back([=](WebGPUContext &ctx) {
            if (ctx.flushCompute()) {
                wgpuComputePassEncoderDispatchWorkgroups(ctx.computePass, threadGroupCountX, threadGroupCountY, threadGroupCountZ);
            }
        });
    }

    void WebGPUCommandList::traceRays(uint32_t width, uint32_t height, uint32_t depth, RenderBufferReference shaderBindingTable, const RenderShaderBindingGroupsInfo &shaderBindingGroupsInfo) {
    }

    void WebGPUCommandList::drawInstanced(uint32_t vertexCountPerInstance, uint32_t instanceCount, uint32_t startVertexLocation, uint32_t startInstanceLocation) {
        pushFlushes++;
        commands.emplace_back([=](WebGPUContext &ctx) {
            if (ctx.flushGraphics()) {
                wgpuRenderPassEncoderDraw(ctx.renderPass, vertexCountPerInstance, instanceCount, startVertexLocation, startInstanceLocation);
            }
        });
    }

    void WebGPUCommandList::drawIndexedInstanced(uint32_t indexCountPerInstance, uint32_t instanceCount, uint32_t startIndexLocation, int32_t baseVertexLocation, uint32_t startInstanceLocation) {
        pushFlushes++;
        commands.emplace_back([=](WebGPUContext &ctx) {
            if (ctx.flushGraphics()) {
                wgpuRenderPassEncoderDrawIndexed(ctx.renderPass, indexCountPerInstance, instanceCount, startIndexLocation, baseVertexLocation, startInstanceLocation);
            }
        });
    }

    void WebGPUCommandList::setPipeline(const RenderPipeline *pipeline) {
        const WebGPUPipeline *webPipeline = static_cast<const WebGPUPipeline *>(pipeline);
        commands.emplace_back([=](WebGPUContext &ctx) {
            if (webPipeline->type == WebGPUPipeline::Type::Compute) {
                ctx.computePipeline = webPipeline;
                ctx.computePipelineDirty = true;
            }
            else {
                ctx.graphicsPipeline = webPipeline;
                ctx.graphicsPipelineDirty = true;
            }
        });
    }

    void WebGPUCommandList::setComputePipelineLayout(const RenderPipelineLayout *pipelineLayout) {
        computeLayout = static_cast<const WebGPUPipelineLayout *>(pipelineLayout);
        const WebGPUPipelineLayout *layout = computeLayout;
        commands.emplace_back([=](WebGPUContext &ctx) {
            ctx.compute.layout = layout;
            ctx.compute.groupsDirty = true;
        });
    }

    void WebGPUCommandList::setPushConstants(bool compute, uint32_t rangeIndex, const void *data, uint32_t offset, uint32_t size) {
        const WebGPUPipelineLayout *layout = compute ? computeLayout : graphicsLayout;
        assert(layout != nullptr);
        assert(rangeIndex < layout->pushConstantRanges.size());

        const RenderPushConstantRange &range = layout->pushConstantRanges[rangeIndex];
        const uint32_t dstOffset = range.offset + offset;
        const uint32_t byteCount = (size > 0) ? size : range.size;
        if ((dstOffset + byteCount) > WebGPUPushConstantStride) {
            device->reportError("push constants past " + std::to_string(WebGPUPushConstantStride) + " bytes are dropped");
            return;
        }

        std::vector<uint8_t> bytes(static_cast<const uint8_t *>(data), static_cast<const uint8_t *>(data) + byteCount);
        commands.emplace_back([=, bytes = std::move(bytes)](WebGPUContext &ctx) {
            WebGPUBindPoint &bindPoint = compute ? ctx.compute : ctx.graphics;
            memcpy(bindPoint.pushConstants + dstOffset, bytes.data(), bytes.size());
            bindPoint.pushDirty = true;
        });
    }

    void WebGPUCommandList::setComputePushConstants(uint32_t rangeIndex, const void *data, uint32_t offset, uint32_t size) {
        setPushConstants(true, rangeIndex, data, offset, size);
    }

    void WebGPUCommandList::setComputeDescriptorSet(RenderDescriptorSet *descriptorSet, uint32_t setIndex) {
        WebGPUDescriptorSet *set = static_cast<WebGPUDescriptorSet *>(descriptorSet);
        commands.emplace_back([=](WebGPUContext &ctx) {
            ctx.compute.sets[setIndex] = set;
            ctx.compute.groupsDirty = true;
        });
    }

    void WebGPUCommandList::setGraphicsPipelineLayout(const RenderPipelineLayout *pipelineLayout) {
        graphicsLayout = static_cast<const WebGPUPipelineLayout *>(pipelineLayout);
        const WebGPUPipelineLayout *layout = graphicsLayout;
        commands.emplace_back([=](WebGPUContext &ctx) {
            ctx.graphics.layout = layout;
            ctx.graphics.groupsDirty = true;
        });
    }

    void WebGPUCommandList::setGraphicsPushConstants(uint32_t rangeIndex, const void *data, uint32_t offset, uint32_t size) {
        setPushConstants(false, rangeIndex, data, offset, size);
    }

    void WebGPUCommandList::setGraphicsDescriptorSet(RenderDescriptorSet *descriptorSet, uint32_t setIndex) {
        WebGPUDescriptorSet *set = static_cast<WebGPUDescriptorSet *>(descriptorSet);
        commands.emplace_back([=](WebGPUContext &ctx) {
            ctx.graphics.sets[setIndex] = set;
            ctx.graphics.groupsDirty = true;
        });
    }

    void WebGPUCommandList::setGraphicsRootDescriptor(RenderBufferReference bufferReference, uint32_t rootDescriptorIndex) {
    }

    void WebGPUCommandList::setRaytracingPipelineLayout(const RenderPipelineLayout *pipelineLayout) {
    }

    void WebGPUCommandList::setRaytracingPushConstants(uint32_t rangeIndex, const void *data, uint32_t offset, uint32_t size) {
    }

    void WebGPUCommandList::setRaytracingDescriptorSet(RenderDescriptorSet *descriptorSet, uint32_t setIndex) {
    }

    void WebGPUCommandList::setIndexBuffer(const RenderIndexBufferView *view) {
        WebGPUVertexBinding binding;
        WGPUIndexFormat format = WGPUIndexFormat_Uint32;
        if (view != nullptr) {
            binding.buffer = static_cast<const WebGPUBuffer *>(view->buffer.ref);
            binding.offset = view->buffer.offset;
            binding.size = view->size;
            format = (view->format == RenderFormat::R16_UINT) ? WGPUIndexFormat_Uint16 : WGPUIndexFormat_Uint32;
        }

        commands.emplace_back([=](WebGPUContext &ctx) {
            ctx.indexBuffer = binding;
            ctx.indexFormat = format;
            ctx.indexBufferDirty = true;
        });
    }

    void WebGPUCommandList::setVertexBuffers(uint32_t startSlot, const RenderVertexBufferView *views, uint32_t viewCount, const RenderInputSlot *inputSlots) {
        std::vector<WebGPUVertexBinding> bindings(viewCount);
        for (uint32_t i = 0; i < viewCount; i++) {
            bindings[i].buffer = static_cast<const WebGPUBuffer *>(views[i].buffer.ref);
            bindings[i].offset = views[i].buffer.offset;
            bindings[i].size = views[i].size;
        }

        commands.emplace_back([=](WebGPUContext &ctx) {
            for (uint32_t i = 0; (i < bindings.size()) && ((startSlot + i) < 16); i++) {
                ctx.vertexBuffers[startSlot + i] = bindings[i];
            }

            ctx.vertexBuffersDirty = true;
        });
    }

    void WebGPUCommandList::setViewports(const RenderViewport *viewports, uint32_t count) {
        if (count == 0) {
            return;
        }

        const RenderViewport viewport = viewports[0];
        commands.emplace_back([=](WebGPUContext &ctx) {
            ctx.viewport = viewport;
            ctx.viewportSet = true;
            ctx.viewportDirty = true;
        });
    }

    void WebGPUCommandList::setScissors(const RenderRect *scissorRects, uint32_t count) {
        if (count == 0) {
            return;
        }

        const RenderRect scissor = scissorRects[0];
        commands.emplace_back([=](WebGPUContext &ctx) {
            ctx.scissor = scissor;
            ctx.scissorSet = true;
            ctx.scissorDirty = true;
        });
    }

    void WebGPUCommandList::setFramebuffer(const RenderFramebuffer *framebuffer) {
        const WebGPUFramebuffer *webFramebuffer = static_cast<const WebGPUFramebuffer *>(framebuffer);
        commands.emplace_back([=](WebGPUContext &ctx) {
            ctx.endPasses();
            ctx.framebuffer = webFramebuffer;
            const size_t colorCount = (webFramebuffer != nullptr) ? webFramebuffer->colorViews.size() : 0;
            ctx.colorClearPending.assign(colorCount, false);
            ctx.colorClearValues.assign(colorCount, WGPUColor{ 0.0, 0.0, 0.0, 0.0 });
            ctx.depthClearPending = false;
            ctx.stencilClearPending = false;
            ctx.viewportSet = false;
            ctx.scissorSet = false;
        });
    }

    void WebGPUCommandList::setDepthBias(float depthBias, float depthBiasClamp, float slopeScaledDepthBias) {
    }

    static bool coversFramebuffer(const WebGPUFramebuffer *framebuffer, const RenderRect *clearRects, uint32_t clearRectsCount) {
        if (clearRectsCount == 0) {
            return true;
        }

        const RenderRect &rect = clearRects[0];
        return (clearRectsCount == 1) && (rect.left <= 0) && (rect.top <= 0) && (rect.right >= int32_t(framebuffer->width)) && (rect.bottom >= int32_t(framebuffer->height));
    }

    void WebGPUCommandList::clearColor(uint32_t attachmentIndex, RenderColor colorValue, const RenderRect *clearRects, uint32_t clearRectsCount) {
        std::vector<RenderRect> rects(clearRects, clearRects + clearRectsCount);
        pushFlushes++;
        commands.emplace_back([=](WebGPUContext &ctx) {
            if ((ctx.framebuffer == nullptr) || (attachmentIndex >= ctx.colorClearPending.size())) {
                return;
            }

            if (!coversFramebuffer(ctx.framebuffer, rects.data(), uint32_t(rects.size()))) {
                ctx.clearRects(int32_t(attachmentIndex), { colorValue.r, colorValue.g, colorValue.b, colorValue.a }, 0.0f, rects);
                return;
            }

            ctx.endPasses();
            ctx.colorClearPending[attachmentIndex] = true;
            ctx.colorClearValues[attachmentIndex] = { colorValue.r, colorValue.g, colorValue.b, colorValue.a };
        });
    }

    void WebGPUCommandList::clearDepthStencil(bool clearDepth, bool clearStencil, float depthValue, uint32_t stencilValue, const RenderRect *clearRects, uint32_t clearRectsCount) {
        std::vector<RenderRect> rects(clearRects, clearRects + clearRectsCount);
        pushFlushes++;
        commands.emplace_back([=](WebGPUContext &ctx) {
            if ((ctx.framebuffer == nullptr) || (ctx.framebuffer->depthView == nullptr)) {
                return;
            }

            if (!coversFramebuffer(ctx.framebuffer, rects.data(), uint32_t(rects.size()))) {
                if (clearStencil) {
                    ctx.device->reportError("partial stencil clears are not supported");
                }

                if (clearDepth) {
                    ctx.clearRects(-1, { 0.0, 0.0, 0.0, 0.0 }, depthValue, rects);
                }

                return;
            }

            ctx.endPasses();
            if (clearDepth) {
                ctx.depthClearPending = true;
                ctx.depthClearValue = depthValue;
            }

            if (clearStencil) {
                ctx.stencilClearPending = true;
                ctx.stencilClearValue = stencilValue;
            }
        });
    }

    void WebGPUCommandList::copyBufferRegion(RenderBufferReference dstBuffer, RenderBufferReference srcBuffer, uint64_t size) {
        commands.emplace_back([=](WebGPUContext &ctx) {
            ctx.endPasses();
            wgpuCommandEncoderCopyBufferToBuffer(ctx.encoder, static_cast<const WebGPUBuffer *>(srcBuffer.ref)->buffer, srcBuffer.offset, static_cast<const WebGPUBuffer *>(dstBuffer.ref)->buffer, dstBuffer.offset, size);
        });
    }

    static WGPUTexelCopyBufferInfo footprintInfo(const RenderTextureCopyLocation &location) {
        uint32_t blockSize = 1;
        const uint32_t bytes = blockBytes(location.placedFootprint.format, blockSize);
        WGPUTexelCopyBufferInfo info = WGPU_TEXEL_COPY_BUFFER_INFO_INIT;
        info.buffer = static_cast<const WebGPUBuffer *>(location.buffer)->buffer;
        info.layout.offset = location.placedFootprint.offset;
        info.layout.bytesPerRow = ((location.placedFootprint.rowWidth + blockSize - 1) / blockSize) * bytes;
        info.layout.rowsPerImage = (location.placedFootprint.height + blockSize - 1) / blockSize;
        return info;
    }

    static WGPUTexelCopyTextureInfo subresourceInfo(const RenderTextureCopyLocation &location, uint32_t x, uint32_t y, uint32_t z) {
        const WebGPUTexture *texture = static_cast<const WebGPUTexture *>(location.texture);
        WGPUTexelCopyTextureInfo info = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
        info.texture = texture->texture;
        info.mipLevel = location.subresource.mipLevel;
        info.origin = { x, y, (texture->desc.dimension == RenderTextureDimension::TEXTURE_3D) ? z : location.subresource.arrayIndex };
        info.aspect = WGPUTextureAspect_All;
        return info;
    }

    void WebGPUCommandList::copyTextureRegion(const RenderTextureCopyLocation &dstLocation, const RenderTextureCopyLocation &srcLocation, uint32_t dstX, uint32_t dstY, uint32_t dstZ, const RenderBox *srcBox) {
        const bool hasBox = (srcBox != nullptr);
        const RenderBox box = hasBox ? *srcBox : RenderBox();
        commands.emplace_back([=](WebGPUContext &ctx) {
            ctx.endPasses();
            if ((dstLocation.type == RenderTextureCopyType::SUBRESOURCE) && (srcLocation.type == RenderTextureCopyType::PLACED_FOOTPRINT)) {
                const WGPUTexelCopyBufferInfo src = footprintInfo(srcLocation);
                const WGPUTexelCopyTextureInfo dst = subresourceInfo(dstLocation, dstX, dstY, dstZ);
                const WGPUExtent3D size = { srcLocation.placedFootprint.width, srcLocation.placedFootprint.height, std::max<uint32_t>(srcLocation.placedFootprint.depth, 1) };
                wgpuCommandEncoderCopyBufferToTexture(ctx.encoder, &src, &dst, &size);
            }
            else if ((dstLocation.type == RenderTextureCopyType::PLACED_FOOTPRINT) && (srcLocation.type == RenderTextureCopyType::SUBRESOURCE)) {
                const WGPUTexelCopyTextureInfo src = subresourceInfo(srcLocation, hasBox ? uint32_t(box.left) : 0, hasBox ? uint32_t(box.top) : 0, hasBox ? uint32_t(box.front) : 0);
                const WGPUTexelCopyBufferInfo dst = footprintInfo(dstLocation);
                const WGPUExtent3D size = hasBox ? WGPUExtent3D{ uint32_t(box.right - box.left), uint32_t(box.bottom - box.top), uint32_t(std::max(box.back - box.front, 1)) } : WGPUExtent3D{ dstLocation.placedFootprint.width, dstLocation.placedFootprint.height, std::max<uint32_t>(dstLocation.placedFootprint.depth, 1) };
                wgpuCommandEncoderCopyTextureToBuffer(ctx.encoder, &src, &dst, &size);
            }
            else if ((dstLocation.type == RenderTextureCopyType::SUBRESOURCE) && (srcLocation.type == RenderTextureCopyType::SUBRESOURCE)) {
                const WebGPUTexture *srcTexture = static_cast<const WebGPUTexture *>(srcLocation.texture);
                const WGPUTexelCopyTextureInfo src = subresourceInfo(srcLocation, hasBox ? uint32_t(box.left) : 0, hasBox ? uint32_t(box.top) : 0, hasBox ? uint32_t(box.front) : 0);
                const WGPUTexelCopyTextureInfo dst = subresourceInfo(dstLocation, dstX, dstY, dstZ);
                const uint32_t mip = srcLocation.subresource.mipLevel;
                const WGPUExtent3D size = hasBox ? WGPUExtent3D{ uint32_t(box.right - box.left), uint32_t(box.bottom - box.top), uint32_t(std::max(box.back - box.front, 1)) } : WGPUExtent3D{ std::max(srcTexture->desc.width >> mip, 1U), std::max(srcTexture->desc.height >> mip, 1U), 1 };
                wgpuCommandEncoderCopyTextureToTexture(ctx.encoder, &src, &dst, &size);
            }
        });
    }

    void WebGPUCommandList::copyBuffer(const RenderBuffer *dstBuffer, const RenderBuffer *srcBuffer) {
        const WebGPUBuffer *dst = static_cast<const WebGPUBuffer *>(dstBuffer);
        const WebGPUBuffer *src = static_cast<const WebGPUBuffer *>(srcBuffer);
        commands.emplace_back([=](WebGPUContext &ctx) {
            ctx.endPasses();
            wgpuCommandEncoderCopyBufferToBuffer(ctx.encoder, src->buffer, 0, dst->buffer, 0, std::min(src->allocatedSize, dst->allocatedSize));
        });
    }

    void WebGPUCommandList::copyTexture(const RenderTexture *dstTexture, const RenderTexture *srcTexture) {
        const WebGPUTexture *dst = static_cast<const WebGPUTexture *>(dstTexture);
        const WebGPUTexture *src = static_cast<const WebGPUTexture *>(srcTexture);
        commands.emplace_back([=](WebGPUContext &ctx) {
            ctx.endPasses();
            const uint32_t mipLevels = std::max<uint32_t>(src->desc.mipLevels, 1);
            for (uint32_t mip = 0; mip < mipLevels; mip++) {
                WGPUTexelCopyTextureInfo srcInfo = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
                WGPUTexelCopyTextureInfo dstInfo = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
                srcInfo.texture = src->texture;
                srcInfo.mipLevel = mip;
                dstInfo.texture = dst->texture;
                dstInfo.mipLevel = mip;

                const uint32_t layers = (src->desc.dimension == RenderTextureDimension::TEXTURE_3D) ? std::max(src->desc.depth >> mip, 1U) : std::max<uint32_t>(src->desc.arraySize, 1);
                const WGPUExtent3D size = { std::max(src->desc.width >> mip, 1U), (src->desc.dimension == RenderTextureDimension::TEXTURE_1D) ? 1 : std::max(src->desc.height >> mip, 1U), layers };
                wgpuCommandEncoderCopyTextureToTexture(ctx.encoder, &srcInfo, &dstInfo, &size);
            }
        });
    }

    void WebGPUCommandList::resolveTexture(const RenderTexture *dstTexture, const RenderTexture *srcTexture) {
        const WebGPUTexture *dst = static_cast<const WebGPUTexture *>(dstTexture);
        const WebGPUTexture *src = static_cast<const WebGPUTexture *>(srcTexture);
        commands.emplace_back([=](WebGPUContext &ctx) {
            ctx.endPasses();

            // WebGPU resolves at the end of a render pass.
            WGPUTextureView srcView = createAttachmentView(src);
            WGPUTextureView dstView = createAttachmentView(dst);
            WGPURenderPassColorAttachment color = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
            color.view = srcView;
            color.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
            color.resolveTarget = dstView;
            color.loadOp = WGPULoadOp_Load;
            color.storeOp = WGPUStoreOp_Store;

            WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
            passDesc.colorAttachmentCount = 1;
            passDesc.colorAttachments = &color;

            WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(ctx.encoder, &passDesc);
            wgpuRenderPassEncoderEnd(pass);
            wgpuRenderPassEncoderRelease(pass);
            wgpuTextureViewRelease(srcView);
            wgpuTextureViewRelease(dstView);
        });
    }

    void WebGPUCommandList::resolveTextureRegion(const RenderTexture *dstTexture, uint32_t dstX, uint32_t dstY, const RenderTexture *srcTexture, const RenderRect *srcRect, RenderResolveMode resolveMode) {
        if ((dstX != 0) || (dstY != 0) || (srcRect != nullptr)) {
            device->reportError("region resolves are not supported; resolving the whole texture");
        }

        resolveTexture(dstTexture, srcTexture);
    }

    void WebGPUCommandList::buildBottomLevelAS(const RenderAccelerationStructure *dstAccelerationStructure, RenderBufferReference scratchBuffer, const RenderBottomLevelASBuildInfo &buildInfo) {
    }

    void WebGPUCommandList::buildTopLevelAS(const RenderAccelerationStructure *dstAccelerationStructure, RenderBufferReference scratchBuffer, RenderBufferReference instancesBuffer, const RenderTopLevelASBuildInfo &buildInfo) {
    }

    void WebGPUCommandList::discardTexture(const RenderTexture *texture) {
    }

    void WebGPUCommandList::resetQueryPool(const RenderQueryPool *queryPool, uint32_t queryFirstIndex, uint32_t queryCount) {
    }

    void WebGPUCommandList::writeTimestamp(const RenderQueryPool *queryPool, uint32_t queryIndex) {
    }

    // WebGPUCommandQueue

    WebGPUCommandQueue::WebGPUCommandQueue(WebGPUDevice *device) {
        this->device = device;
    }

    std::unique_ptr<RenderCommandList> WebGPUCommandQueue::createCommandList() {
        return std::make_unique<WebGPUCommandList>(device);
    }

    std::unique_ptr<RenderSwapChain> WebGPUCommandQueue::createSwapChain(const RenderSwapChainDesc &desc) {
        auto swapChain = std::make_unique<WebGPUSwapChain>(device, desc);
        if (swapChain->surface == nullptr) {
            return nullptr;
        }

        return swapChain;
    }

    // WebGPUSwapChain

    // shortcut: always the page's #canvas (where Emscripten's SDL draws); take the canvas from the SDL window if a page ever has two.
    static const char *CanvasSelector = "#canvas";

    WebGPUSwapChain::WebGPUSwapChain(WebGPUDevice *device, const RenderSwapChainDesc &desc) {
        this->device = device;
        this->desc = desc;
        format = toWgpu(desc.format);
        if (format == WGPUTextureFormat_Undefined) {
            format = WGPUTextureFormat_BGRA8Unorm;
            this->desc.format = RenderFormat::B8G8R8A8_UNORM;
        }

        webgpuRunOnMain([&]() {
            WGPUEmscriptenSurfaceSourceCanvasHTMLSelector canvasSource = WGPU_EMSCRIPTEN_SURFACE_SOURCE_CANVAS_HTML_SELECTOR_INIT;
            canvasSource.selector = toView(CanvasSelector);

            WGPUSurfaceDescriptor surfaceDesc = WGPU_SURFACE_DESCRIPTOR_INIT;
            surfaceDesc.nextInChain = &canvasSource.chain;
            surface = wgpuInstanceCreateSurface(device->renderInterface->instance, &surfaceDesc);
        });

        if (surface == nullptr) {
            device->reportError("no WebGPU surface for the page's canvas");
            return;
        }

        resize();
    }

    WebGPUSwapChain::~WebGPUSwapChain() {
        textures.clear();
        releaseOnMain([&]() {
            if (surface != nullptr) {
                wgpuSurfaceUnconfigure(surface);
                wgpuSurfaceRelease(surface);
            }
        });
    }

    void WebGPUSwapChain::canvasSize(uint32_t &canvasWidth, uint32_t &canvasHeight) const {
        int w = 0;
        int h = 0;
        webgpuRunOnMain([&]() {
            emscripten_get_canvas_element_size(CanvasSelector, &w, &h);
        });

        canvasWidth = uint32_t(std::max(w, 0));
        canvasHeight = uint32_t(std::max(h, 0));
    }

    bool WebGPUSwapChain::present(uint32_t textureIndex, RenderCommandSemaphore **waitSemaphores, uint32_t waitSemaphoreCount) {
        if ((textureIndex >= textures.size()) || isEmpty()) {
            return false;
        }

        bool presented = false;
        const WebGPUTexture *source = static_cast<const WebGPUTexture *>(textures[textureIndex].get());
        webgpuRunOnMain([&]() {
            WGPUSurfaceTexture surfaceTexture = WGPU_SURFACE_TEXTURE_INIT;
            wgpuSurfaceGetCurrentTexture(surface, &surfaceTexture);
            if ((surfaceTexture.status != WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal) && (surfaceTexture.status != WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal)) {
                device->reportError("the canvas has no texture to present to");
                return;
            }

            WGPUTexelCopyTextureInfo src = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
            WGPUTexelCopyTextureInfo dst = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
            src.texture = source->texture;
            dst.texture = surfaceTexture.texture;

            const WGPUExtent3D size = { std::min(width, wgpuTextureGetWidth(surfaceTexture.texture)), std::min(height, wgpuTextureGetHeight(surfaceTexture.texture)), 1 };
            WGPUCommandEncoderDescriptor encoderDesc = WGPU_COMMAND_ENCODER_DESCRIPTOR_INIT;
            WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device->device, &encoderDesc);
            wgpuCommandEncoderCopyTextureToTexture(encoder, &src, &dst, &size);

            WGPUCommandBufferDescriptor commandBufferDesc = WGPU_COMMAND_BUFFER_DESCRIPTOR_INIT;
            WGPUCommandBuffer commandBuffer = wgpuCommandEncoderFinish(encoder, &commandBufferDesc);
            wgpuQueueSubmit(device->queue, 1, &commandBuffer);
            wgpuCommandBufferRelease(commandBuffer);
            wgpuCommandEncoderRelease(encoder);
            wgpuTextureRelease(surfaceTexture.texture);
            presented = true;
        });

        return presented;
    }

    void WebGPUSwapChain::wait() {
    }

    bool WebGPUSwapChain::resize() {
        uint32_t canvasWidth = 0;
        uint32_t canvasHeight = 0;
        canvasSize(canvasWidth, canvasHeight);
        if ((canvasWidth == width) && (canvasHeight == height) && !textures.empty()) {
            return true;
        }

        width = canvasWidth;
        height = canvasHeight;
        textures.clear();
        if (isEmpty()) {
            return false;
        }

        webgpuRunOnMain([&]() {
            WGPUSurfaceConfiguration config = WGPU_SURFACE_CONFIGURATION_INIT;
            config.device = device->device;
            config.format = format;
            config.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopyDst;
            config.width = width;
            config.height = height;
            config.alphaMode = WGPUCompositeAlphaMode_Opaque;
            config.presentMode = WGPUPresentMode_Fifo;
            wgpuSurfaceConfigure(surface, &config);
        });

        const uint32_t textureCount = std::max<uint32_t>(desc.textureCount, 1);
        for (uint32_t i = 0; i < textureCount; i++) {
            textures.push_back(device->createTexture(RenderTextureDesc::ColorTarget(width, height, desc.format)));
        }

        return true;
    }

    bool WebGPUSwapChain::needsResize() const {
        uint32_t canvasWidth = 0;
        uint32_t canvasHeight = 0;
        canvasSize(canvasWidth, canvasHeight);
        return (canvasWidth != width) || (canvasHeight != height);
    }

    void WebGPUSwapChain::setVsyncEnabled(bool vsyncEnabled) {
        this->vsyncEnabled = vsyncEnabled;
    }

    bool WebGPUSwapChain::isVsyncEnabled() const {
        return vsyncEnabled;
    }

    uint32_t WebGPUSwapChain::getWidth() const {
        return width;
    }

    uint32_t WebGPUSwapChain::getHeight() const {
        return height;
    }

    RenderTexture *WebGPUSwapChain::getTexture(uint32_t textureIndex) {
        return (textureIndex < textures.size()) ? textures[textureIndex].get() : nullptr;
    }

    uint32_t WebGPUSwapChain::getTextureCount() const {
        return uint32_t(textures.size());
    }

    bool WebGPUSwapChain::acquireTexture(RenderCommandSemaphore *signalSemaphore, uint32_t *textureIndex) {
        if (textures.empty()) {
            return false;
        }

        *textureIndex = nextTexture;
        nextTexture = (nextTexture + 1) % uint32_t(textures.size());
        return true;
    }

    RenderWindow WebGPUSwapChain::getWindow() const {
        return desc.renderWindow;
    }

    bool WebGPUSwapChain::isEmpty() const {
        return (width == 0) || (height == 0);
    }

    uint32_t WebGPUSwapChain::getRefreshRate() const {
        return 60;
    }

    void WebGPUCommandQueue::executeCommandLists(const RenderCommandList **commandLists, uint32_t commandListCount, RenderCommandSemaphore **waitSemaphores, uint32_t waitSemaphoreCount, RenderCommandSemaphore **signalSemaphores, uint32_t signalSemaphoreCount, RenderCommandFence *signalFence) {
        uint64_t pushSlots = 0;
        for (uint32_t i = 0; i < commandListCount; i++) {
            pushSlots += static_cast<const WebGPUCommandList *>(commandLists[i])->pushFlushes;
        }

        WebGPUCommandFence *fence = static_cast<WebGPUCommandFence *>(signalFence);
        if (fence != nullptr) {
            fence->pending = 1;
        }

        webgpuRunOnMain([&]() {
            const uint64_t ringNeeded = std::max<uint64_t>(pushSlots, 1) * WebGPUPushConstantStride;
            if (ringNeeded > device->pushRingSize) {
                if (device->pushRing != nullptr) {
                    wgpuBufferDestroy(device->pushRing);
                    wgpuBufferRelease(device->pushRing);
                }

                device->pushRingSize = std::max<uint64_t>(ringNeeded * 2, 64 * 1024);
                WGPUBufferDescriptor ringDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
                ringDesc.size = device->pushRingSize;
                ringDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
                device->pushRing = wgpuDeviceCreateBuffer(device->device, &ringDesc);
                device->pushRingGeneration++;
            }

            std::vector<WebGPUBuffer *> dirty;
            {
                std::lock_guard<std::mutex> lock(device->dirtyMutex);
                dirty.swap(device->dirtyBuffers);
            }

            for (WebGPUBuffer *buffer : dirty) {
                wgpuQueueWriteBuffer(device->queue, buffer->buffer, 0, buffer->shadow.data(), buffer->allocatedSize);
            }

            WebGPUContext ctx;
            ctx.device = device;
            ctx.pushStaging.reserve(pushSlots * WebGPUPushConstantStride);

            WGPUCommandEncoderDescriptor encoderDesc = WGPU_COMMAND_ENCODER_DESCRIPTOR_INIT;
            ctx.encoder = wgpuDeviceCreateCommandEncoder(device->device, &encoderDesc);
            for (uint32_t i = 0; i < commandListCount; i++) {
                for (const WebGPUCommand &command : static_cast<const WebGPUCommandList *>(commandLists[i])->commands) {
                    command(ctx);
                }
            }

            ctx.endPasses();
            if (!ctx.pushStaging.empty()) {
                wgpuQueueWriteBuffer(device->queue, device->pushRing, 0, ctx.pushStaging.data(), ctx.pushStaging.size());
            }

            WGPUCommandBufferDescriptor commandBufferDesc = WGPU_COMMAND_BUFFER_DESCRIPTOR_INIT;
            WGPUCommandBuffer commandBuffer = wgpuCommandEncoderFinish(ctx.encoder, &commandBufferDesc);
            wgpuQueueSubmit(device->queue, 1, &commandBuffer);
            wgpuCommandBufferRelease(commandBuffer);
            wgpuCommandEncoderRelease(ctx.encoder);

            if (fence != nullptr) {
                WGPUQueueWorkDoneCallbackInfo callbackInfo = WGPU_QUEUE_WORK_DONE_CALLBACK_INFO_INIT;
                callbackInfo.mode = WGPUCallbackMode_AllowSpontaneous;
                callbackInfo.userdata1 = fence;
                callbackInfo.callback = [](WGPUQueueWorkDoneStatus status, WGPUStringView message, void *userdata1, void *) {
                    WebGPUCommandFence *fence = static_cast<WebGPUCommandFence *>(userdata1);
                    fence->pending = 0;
                    emscripten_futex_wake(&fence->pending, INT_MAX);
                };

                wgpuQueueOnSubmittedWorkDone(device->queue, callbackInfo);
            }
        });
    }

    void WebGPUCommandQueue::waitForCommandFence(RenderCommandFence *fence) {
        WebGPUCommandFence *webFence = static_cast<WebGPUCommandFence *>(fence);
        if (emscripten_is_main_runtime_thread()) {
            if (webFence->pending.load() != 0) {
                device->reportError("a fence was waited on from the main browser thread, which can't block");
            }

            return;
        }

        while (webFence->pending.load() != 0) {
            emscripten_futex_wait(&webFence->pending, 1, INFINITY);
        }
    }

    // WebGPUPool

    WebGPUPool::WebGPUPool(WebGPUDevice *device) {
        this->device = device;
    }

    std::unique_ptr<RenderBuffer> WebGPUPool::createBuffer(const RenderBufferDesc &desc) {
        return std::make_unique<WebGPUBuffer>(device, desc);
    }

    std::unique_ptr<RenderTexture> WebGPUPool::createTexture(const RenderTextureDesc &desc) {
        return std::make_unique<WebGPUTexture>(device, desc);
    }

    // WebGPUQueryPool

    WebGPUQueryPool::WebGPUQueryPool(uint32_t queryCount) {
        results.resize(queryCount, 0);
    }

    void WebGPUQueryPool::queryResults() {
    }

    const uint64_t *WebGPUQueryPool::getResults() const {
        return results.data();
    }

    uint32_t WebGPUQueryPool::getCount() const {
        return uint32_t(results.size());
    }

    // WebGPUDevice

    WebGPUDevice::WebGPUDevice(WebGPUInterface *renderInterface, const std::string &preferredDeviceName) {
        this->renderInterface = renderInterface;

        webgpuRunOnMainAsync([&](std::function<void()> done) {
            struct State {
                WebGPUDevice *device;
                std::function<void()> done;
            };

            WGPURequestAdapterOptions adapterOptions = WGPU_REQUEST_ADAPTER_OPTIONS_INIT;
            adapterOptions.powerPreference = WGPUPowerPreference_HighPerformance;

            WGPURequestAdapterCallbackInfo adapterCallback = WGPU_REQUEST_ADAPTER_CALLBACK_INFO_INIT;
            adapterCallback.mode = WGPUCallbackMode_AllowSpontaneous;
            adapterCallback.userdata1 = new State{ this, done };
            adapterCallback.callback = [](WGPURequestAdapterStatus status, WGPUAdapter adapter, WGPUStringView message, void *userdata1, void *) {
                State *state = static_cast<State *>(userdata1);
                WebGPUDevice *self = state->device;
                if (status != WGPURequestAdapterStatus_Success) {
                    self->reportError("no WebGPU adapter: " + toString(message));
                    state->done();
                    delete state;
                    return;
                }

                self->adapter = adapter;
                wgpuAdapterGetLimits(adapter, &self->limits);
                self->limits.nextInChain = nullptr;

                WGPUAdapterInfo info = WGPU_ADAPTER_INFO_INIT;
                if (wgpuAdapterGetInfo(adapter, &info) == WGPUStatus_Success) {
                    const std::string description = toString(info.description);
                    const std::string vendor = toString(info.vendor);
                    self->description.name = description.empty() ? (vendor.empty() ? "WebGPU" : vendor) : description;
                    wgpuAdapterInfoFreeMembers(info);
                }

                std::vector<WGPUFeatureName> features;
                const WGPUFeatureName optionalFeatures[] = { WGPUFeatureName_DualSourceBlending, WGPUFeatureName_Float32Filterable };
                for (WGPUFeatureName feature : optionalFeatures) {
                    if (wgpuAdapterHasFeature(adapter, feature)) {
                        features.push_back(feature);
                    }
                }

                self->dualSourceBlending = wgpuAdapterHasFeature(adapter, WGPUFeatureName_DualSourceBlending);

                WGPUDeviceDescriptor deviceDesc = WGPU_DEVICE_DESCRIPTOR_INIT;
                deviceDesc.requiredFeatureCount = features.size();
                deviceDesc.requiredFeatures = features.data();
                deviceDesc.requiredLimits = &self->limits;
                deviceDesc.uncapturedErrorCallbackInfo.userdata1 = self;
                deviceDesc.uncapturedErrorCallbackInfo.callback = [](const WGPUDevice *, WGPUErrorType type, WGPUStringView message, void *userdata1, void *) {
                    static_cast<WebGPUDevice *>(userdata1)->reportError(toString(message));
                };

                deviceDesc.deviceLostCallbackInfo.mode = WGPUCallbackMode_AllowSpontaneous;
                deviceDesc.deviceLostCallbackInfo.userdata1 = self;
                deviceDesc.deviceLostCallbackInfo.callback = [](const WGPUDevice *, WGPUDeviceLostReason reason, WGPUStringView message, void *userdata1, void *) {
                    if (reason != WGPUDeviceLostReason_Destroyed) {
                        WebGPUDevice *self = static_cast<WebGPUDevice *>(userdata1);
                        self->lost = true;
                        self->reportError("device lost: " + toString(message));
                    }
                };

                WGPURequestDeviceCallbackInfo deviceCallback = WGPU_REQUEST_DEVICE_CALLBACK_INFO_INIT;
                deviceCallback.mode = WGPUCallbackMode_AllowSpontaneous;
                deviceCallback.userdata1 = state;
                deviceCallback.callback = [](WGPURequestDeviceStatus status, WGPUDevice device, WGPUStringView message, void *userdata1, void *) {
                    State *state = static_cast<State *>(userdata1);
                    if (status == WGPURequestDeviceStatus_Success) {
                        state->device->device = device;
                        state->device->queue = wgpuDeviceGetQueue(device);
                    }
                    else {
                        state->device->reportError("no WebGPU device: " + toString(message));
                    }

                    state->done();
                    delete state;
                };

                wgpuAdapterRequestDevice(adapter, &deviceDesc, deviceCallback);
            };

            wgpuInstanceRequestAdapter(renderInterface->instance, &adapterOptions, adapterCallback);
        });

        capabilities.computeShaders = true;
        capabilities.geometryShader = false;
        capabilities.raytracing = false;
        capabilities.rayQuery = false;
        capabilities.descriptorIndexing = false;
        capabilities.maxTexturesPerStage = limits.maxSampledTexturesPerShaderStage;
        capabilities.maxTextureSize = limits.maxTextureDimension2D;
        capabilities.triangleFan = false;
        capabilities.dynamicDepthBias = false;
        capabilities.queryPools = false;
        description.type = RenderDeviceType::UNKNOWN;
    }

    WebGPUDevice::~WebGPUDevice() {
        releaseOnMain([&]() {
            for (auto &entry : setlessGroups) {
                wgpuBindGroupRelease(entry.second.group);
            }

            for (auto &entry : modules) {
                if (entry.second.module != nullptr) {
                    wgpuShaderModuleRelease(entry.second.module);
                }
            }

            if (pushRing != nullptr) {
                wgpuBufferDestroy(pushRing);
                wgpuBufferRelease(pushRing);
            }

            if (queue != nullptr) {
                wgpuQueueRelease(queue);
            }

            if (device != nullptr) {
                wgpuDeviceDestroy(device);
                wgpuDeviceRelease(device);
            }

            if (adapter != nullptr) {
                wgpuAdapterRelease(adapter);
            }
        });
    }

    std::string WebGPUDevice::takeErrors() {
        std::lock_guard<std::mutex> lock(errorMutex);
        std::string taken;
        taken.swap(errors);
        return taken;
    }

    void WebGPUDevice::reportError(const std::string &message) {
        fprintf(stderr, "[webgpu] %s\n", message.c_str());

        std::lock_guard<std::mutex> lock(errorMutex);
        if (!errors.empty()) {
            errors += "\n";
        }

        errors += message;
    }

    void WebGPUDevice::markDirty(WebGPUBuffer *buffer) {
        std::lock_guard<std::mutex> lock(dirtyMutex);
        if (std::find(dirtyBuffers.begin(), dirtyBuffers.end(), buffer) == dirtyBuffers.end()) {
            dirtyBuffers.push_back(buffer);
        }
    }

    void WebGPUDevice::forgetBuffer(WebGPUBuffer *buffer) {
        std::lock_guard<std::mutex> lock(dirtyMutex);
        dirtyBuffers.erase(std::remove(dirtyBuffers.begin(), dirtyBuffers.end(), buffer), dirtyBuffers.end());
    }

    const WebGPUModule &WebGPUDevice::getModule(const WebGPUShader *shader, const RenderSpecConstant *specConstants, uint32_t specConstantsCount, WGPUShaderStage stage) {
        uint64_t hash = fnv1a(shader->spirv.data(), shader->spirv.size() * sizeof(uint32_t));
        hash = fnv1a(specConstants, specConstantsCount * sizeof(RenderSpecConstant), hash);
        const std::string key = std::to_string(hash) + ":" + shader->entryPointName + ":" + std::to_string(stage);
        auto it = modules.find(key);
        if (it != modules.end()) {
            return it->second;
        }

        WebGPUModule &module = modules[key];
        std::string wgsl;
        if (!renderInterface->converter || !renderInterface->converter(shader->spirv, specConstants, specConstantsCount, wgsl, module.error)) {
            if (module.error.empty()) {
                module.error = "no shader converter";
            }

            return module;
        }

        WGPUShaderSourceWGSL source = WGPU_SHADER_SOURCE_WGSL_INIT;
        source.code = toView(wgsl);

        WGPUShaderModuleDescriptor moduleDesc = WGPU_SHADER_MODULE_DESCRIPTOR_INIT;
        moduleDesc.nextInChain = &source.chain;
        module.module = wgpuDeviceCreateShaderModule(device, &moduleDesc);
        module.bindings = reflectBindings(wgsl, stage);
        return module;
    }

    std::unique_ptr<RenderDescriptorSet> WebGPUDevice::createDescriptorSet(const RenderDescriptorSetDesc &desc) {
        return std::make_unique<WebGPUDescriptorSet>(this, desc);
    }

    std::unique_ptr<RenderShader> WebGPUDevice::createShader(const void *data, uint64_t size, const char *entryPointName, RenderShaderFormat format) {
        if (format != RenderShaderFormat::SPIRV) {
            reportError("only SPIR-V shaders are supported");
            return nullptr;
        }

        return std::make_unique<WebGPUShader>(data, size, entryPointName);
    }

    std::unique_ptr<RenderSampler> WebGPUDevice::createSampler(const RenderSamplerDesc &desc) {
        return std::make_unique<WebGPUSampler>(this, desc);
    }

    std::unique_ptr<RenderPipeline> WebGPUDevice::createComputePipeline(const RenderComputePipelineDesc &desc) {
        auto pipeline = std::make_unique<WebGPUPipeline>(WebGPUPipeline::Type::Compute);
        pipeline->valid = true;
        createWithErrorScope(this, pipeline.get(), [&]() {
            const WebGPUShader *shader = static_cast<const WebGPUShader *>(desc.computeShader);
            const WebGPUModule &module = getModule(shader, desc.specConstants, desc.specConstantsCount, WGPUShaderStage_Compute);
            if (module.module == nullptr) {
                pipeline->valid = false;
                pipeline->error = module.error;
                return;
            }

            buildLayouts(this, pipeline.get(), { &module });

            WGPUComputePipelineDescriptor pipelineDesc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
            pipelineDesc.layout = pipeline->layout;
            pipelineDesc.compute.module = module.module;
            pipelineDesc.compute.entryPoint = toView(shader->entryPointName);
            pipeline->compute = wgpuDeviceCreateComputePipeline(device, &pipelineDesc);
        });

        return pipeline;
    }

    std::unique_ptr<RenderPipeline> WebGPUDevice::createGraphicsPipeline(const RenderGraphicsPipelineDesc &desc) {
        auto pipeline = std::make_unique<WebGPUPipeline>(WebGPUPipeline::Type::Graphics);
        pipeline->valid = true;
        pipeline->stencilReference = desc.stencilReference;
        if (desc.geometryShader != nullptr) {
            pipeline->valid = false;
            pipeline->error = "WebGPU has no geometry shaders";
            reportError(pipeline->error);
            return pipeline;
        }

        createWithErrorScope(this, pipeline.get(), [&]() {
            const WebGPUShader *vertexShader = static_cast<const WebGPUShader *>(desc.vertexShader);
            const WebGPUShader *pixelShader = static_cast<const WebGPUShader *>(desc.pixelShader);
            const WebGPUModule &vertexModule = getModule(vertexShader, desc.specConstants, desc.specConstantsCount, WGPUShaderStage_Vertex);
            const WebGPUModule *pixelModule = (pixelShader != nullptr) ? &getModule(pixelShader, desc.specConstants, desc.specConstantsCount, WGPUShaderStage_Fragment) : nullptr;
            if ((vertexModule.module == nullptr) || ((pixelModule != nullptr) && (pixelModule->module == nullptr))) {
                pipeline->valid = false;
                pipeline->error = (vertexModule.module == nullptr) ? vertexModule.error : pixelModule->error;
                return;
            }

            std::vector<const WebGPUModule *> modules = { &vertexModule };
            if (pixelModule != nullptr) {
                modules.push_back(pixelModule);
            }

            buildLayouts(this, pipeline.get(), modules);

            // Vertex buffers, indexed by plume's slot index.
            uint32_t slotCount = 0;
            for (uint32_t i = 0; i < desc.inputSlotsCount; i++) {
                slotCount = std::max(slotCount, desc.inputSlots[i].index + 1);
            }

            std::vector<std::vector<WGPUVertexAttribute>> attributes(slotCount);
            for (uint32_t i = 0; i < desc.inputElementsCount; i++) {
                const RenderInputElement &element = desc.inputElements[i];
                if (element.slotIndex >= slotCount) {
                    continue;
                }

                WGPUVertexAttribute attribute = WGPU_VERTEX_ATTRIBUTE_INIT;
                attribute.format = toVertexFormat(element.format);
                attribute.offset = element.alignedByteOffset;
                attribute.shaderLocation = element.location;
                attributes[element.slotIndex].push_back(attribute);
            }

            std::vector<WGPUVertexBufferLayout> buffers(slotCount);
            for (uint32_t i = 0; i < slotCount; i++) {
                buffers[i] = WGPU_VERTEX_BUFFER_LAYOUT_INIT;
            }

            for (uint32_t i = 0; i < desc.inputSlotsCount; i++) {
                const RenderInputSlot &slot = desc.inputSlots[i];
                WGPUVertexBufferLayout &buffer = buffers[slot.index];
                buffer.stepMode = (slot.classification == RenderInputSlotClassification::PER_INSTANCE_DATA) ? WGPUVertexStepMode_Instance : WGPUVertexStepMode_Vertex;
                buffer.arrayStride = slot.stride;
                buffer.attributeCount = attributes[slot.index].size();
                buffer.attributes = attributes[slot.index].data();
            }

            WGPURenderPipelineDescriptor pipelineDesc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
            pipelineDesc.layout = pipeline->layout;
            pipelineDesc.vertex.module = vertexModule.module;
            pipelineDesc.vertex.entryPoint = toView(vertexShader->entryPointName);
            pipelineDesc.vertex.bufferCount = buffers.size();
            pipelineDesc.vertex.buffers = buffers.data();
            pipelineDesc.primitive.topology = toWgpu(desc.primitiveTopology);
            pipelineDesc.primitive.frontFace = (desc.frontFace == RenderFrontFace::COUNTER_CLOCKWISE) ? WGPUFrontFace_CCW : WGPUFrontFace_CW;
            pipelineDesc.primitive.cullMode = (desc.cullMode == RenderCullMode::FRONT) ? WGPUCullMode_Front : ((desc.cullMode == RenderCullMode::BACK) ? WGPUCullMode_Back : WGPUCullMode_None);
            pipelineDesc.multisample.count = std::max<uint32_t>(desc.multisampling.sampleCount, 1);
            pipelineDesc.multisample.alphaToCoverageEnabled = desc.alphaToCoverageEnabled;

            WGPUDepthStencilState depthStencil = WGPU_DEPTH_STENCIL_STATE_INIT;
            if (desc.depthTargetFormat != RenderFormat::UNKNOWN) {
                depthStencil.format = toWgpu(desc.depthTargetFormat);
                depthStencil.depthWriteEnabled = (desc.depthEnabled && desc.depthWriteEnabled) ? WGPUOptionalBool_True : WGPUOptionalBool_False;
                depthStencil.depthCompare = desc.depthEnabled ? toWgpu(desc.depthFunction) : WGPUCompareFunction_Always;
                depthStencil.depthBias = desc.depthBias;
                depthStencil.depthBiasClamp = desc.depthBiasClamp;
                depthStencil.depthBiasSlopeScale = desc.slopeScaledDepthBias;
                if (desc.stencilEnabled && hasStencil(depthStencil.format)) {
                    depthStencil.stencilReadMask = desc.stencilReadMask;
                    depthStencil.stencilWriteMask = desc.stencilWriteMask;
                    depthStencil.stencilFront = { toWgpu(desc.stencilFrontFace.compareFunction), toWgpu(desc.stencilFrontFace.failOp), toWgpu(desc.stencilFrontFace.depthFailOp), toWgpu(desc.stencilFrontFace.passOp) };
                    depthStencil.stencilBack = { toWgpu(desc.stencilBackFace.compareFunction), toWgpu(desc.stencilBackFace.failOp), toWgpu(desc.stencilBackFace.depthFailOp), toWgpu(desc.stencilBackFace.passOp) };
                }
                else {
                    depthStencil.stencilReadMask = 0;
                    depthStencil.stencilWriteMask = 0;
                }

                pipelineDesc.depthStencil = &depthStencil;
            }

            std::vector<WGPUColorTargetState> targets(desc.renderTargetCount);
            std::vector<WGPUBlendState> blends(desc.renderTargetCount);
            for (uint32_t i = 0; i < desc.renderTargetCount; i++) {
                const RenderBlendDesc &blend = desc.renderTargetBlend[i];
                targets[i] = WGPU_COLOR_TARGET_STATE_INIT;
                targets[i].format = toWgpu(desc.renderTargetFormat[i]);
                targets[i].writeMask = blend.renderTargetWriteMask & WGPUColorWriteMask_All;
                if (blend.blendEnabled) {
                    blends[i].color = { toWgpu(blend.blendOp), toWgpu(blend.srcBlend), toWgpu(blend.dstBlend) };
                    blends[i].alpha = { toWgpu(blend.blendOpAlpha), toWgpu(blend.srcBlendAlpha), toWgpu(blend.dstBlendAlpha) };
                    targets[i].blend = &blends[i];
                }
            }

            WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
            if (pixelModule != nullptr) {
                fragment.module = pixelModule->module;
                fragment.entryPoint = toView(pixelShader->entryPointName);
                fragment.targetCount = targets.size();
                fragment.targets = targets.data();
                pipelineDesc.fragment = &fragment;
            }

            pipeline->render = wgpuDeviceCreateRenderPipeline(device, &pipelineDesc);
        });

        return pipeline;
    }

    std::unique_ptr<RenderPipeline> WebGPUDevice::createRaytracingPipeline(const RenderRaytracingPipelineDesc &desc, const RenderPipeline *previousPipeline) {
        return nullptr;
    }

    std::unique_ptr<RenderCommandQueue> WebGPUDevice::createCommandQueue(RenderCommandListType type) {
        return std::make_unique<WebGPUCommandQueue>(this);
    }

    std::unique_ptr<RenderBuffer> WebGPUDevice::createBuffer(const RenderBufferDesc &desc) {
        return std::make_unique<WebGPUBuffer>(this, desc);
    }

    std::unique_ptr<RenderTexture> WebGPUDevice::createTexture(const RenderTextureDesc &desc) {
        return std::make_unique<WebGPUTexture>(this, desc);
    }

    std::unique_ptr<RenderAccelerationStructure> WebGPUDevice::createAccelerationStructure(const RenderAccelerationStructureDesc &desc) {
        return nullptr;
    }

    std::unique_ptr<RenderPool> WebGPUDevice::createPool(const RenderPoolDesc &desc) {
        return std::make_unique<WebGPUPool>(this);
    }

    std::unique_ptr<RenderPipelineLayout> WebGPUDevice::createPipelineLayout(const RenderPipelineLayoutDesc &desc) {
        return std::make_unique<WebGPUPipelineLayout>(this, desc);
    }

    std::unique_ptr<RenderCommandFence> WebGPUDevice::createCommandFence() {
        return std::make_unique<WebGPUCommandFence>();
    }

    std::unique_ptr<RenderCommandSemaphore> WebGPUDevice::createCommandSemaphore() {
        return std::make_unique<WebGPUCommandSemaphore>();
    }

    std::unique_ptr<RenderFramebuffer> WebGPUDevice::createFramebuffer(const RenderFramebufferDesc &desc) {
        return std::make_unique<WebGPUFramebuffer>(this, desc);
    }

    std::unique_ptr<RenderQueryPool> WebGPUDevice::createQueryPool(uint32_t queryCount) {
        return std::make_unique<WebGPUQueryPool>(queryCount);
    }

    void WebGPUDevice::setBottomLevelASBuildInfo(RenderBottomLevelASBuildInfo &buildInfo, const RenderBottomLevelASMesh *meshes, uint32_t meshCount, bool preferFastBuild, bool preferFastTrace) {
    }

    void WebGPUDevice::setTopLevelASBuildInfo(RenderTopLevelASBuildInfo &buildInfo, const RenderTopLevelASInstance *instances, uint32_t instanceCount, bool preferFastBuild, bool preferFastTrace) {
    }

    void WebGPUDevice::setShaderBindingTableInfo(RenderShaderBindingTableInfo &tableInfo, const RenderShaderBindingGroups &groups, const RenderPipeline *pipeline, RenderDescriptorSet **descriptorSets, uint32_t descriptorSetCount) {
    }

    const RenderDeviceCapabilities &WebGPUDevice::getCapabilities() const {
        return capabilities;
    }

    const RenderDeviceDescription &WebGPUDevice::getDescription() const {
        return description;
    }

    RenderSampleCounts WebGPUDevice::getSampleCountsSupported(RenderFormat format) const {
        return RenderSampleCount::COUNT_1 | RenderSampleCount::COUNT_4;
    }

    bool WebGPUDevice::isLost() const {
        return lost;
    }

    bool WebGPUDevice::beginCapture() {
        return false;
    }

    bool WebGPUDevice::endCapture() {
        return false;
    }

    // WebGPUInterface

    WebGPUInterface::WebGPUInterface(WebGPUShaderConverter converter) {
        this->converter = std::move(converter);
        capabilities.shaderFormat = RenderShaderFormat::SPIRV;
        deviceNames.push_back("WebGPU");
        webgpuRunOnMain([&]() {
            WGPUInstanceDescriptor instanceDesc = WGPU_INSTANCE_DESCRIPTOR_INIT;
            instance = wgpuCreateInstance(&instanceDesc);
        });
    }

    std::unique_ptr<RenderDevice> WebGPUInterface::createDevice(const std::string &preferredDeviceName) {
        if (instance == nullptr) {
            return nullptr;
        }

        auto device = std::make_unique<WebGPUDevice>(this, preferredDeviceName);
        if (device->device == nullptr) {
            return nullptr;
        }

        return device;
    }

    const RenderInterfaceCapabilities &WebGPUInterface::getCapabilities() const {
        return capabilities;
    }

    const std::vector<std::string> &WebGPUInterface::getDeviceNames() const {
        return deviceNames;
    }

    std::unique_ptr<RenderInterface> CreateWebGPUInterface(WebGPUShaderConverter converter) {
        return std::make_unique<WebGPUInterface>(std::move(converter));
    }
};
