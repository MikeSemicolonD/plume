//
// plume
//
// Copyright (c) 2024 renderbag and contributors. All rights reserved.
// Licensed under the MIT license. See LICENSE file for details.
//

#include "plume_d3d11.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <thread>

#include <d3dcompiler.h>
#include <dxgi1_5.h>

#include "spirv_hlsl.hpp"

namespace plume {
    static const uint32_t MaxDescriptorSets = 8;
    static const uint32_t MaxConstantBufferBytes = 65536;

    enum D3D11Stage : uint32_t {
        StageVertex,
        StageGeometry,
        StagePixel,
        StageCompute
    };

    // Common functions.

    static std::string Utf16ToUtf8(const std::wstring_view &value) {
        std::string utf8(WideCharToMultiByte(CP_UTF8, 0, value.data(), int(value.size()), nullptr, 0, nullptr, nullptr), 0);
        WideCharToMultiByte(CP_UTF8, 0, value.data(), int(value.size()), utf8.data(), int(utf8.size()), nullptr, nullptr);
        return utf8;
    }

    static uint64_t roundUp(uint64_t value, uint64_t powerOf2Alignment) {
        return (value + powerOf2Alignment - 1) & ~(powerOf2Alignment - 1);
    }

    static void warnOnce(const char *message) {
        static std::mutex warnMutex;
        static std::vector<const char *> warned;
        std::scoped_lock lock(warnMutex);
        if (std::find(warned.begin(), warned.end(), message) == warned.end()) {
            warned.push_back(message);
            fprintf(stderr, "[plume-d3d11] %s\n", message);
        }
    }

    static DXGI_FORMAT toDXGI(RenderFormat format) {
        switch (format) {
        case RenderFormat::UNKNOWN: return DXGI_FORMAT_UNKNOWN;
        case RenderFormat::R32G32B32A32_TYPELESS: return DXGI_FORMAT_R32G32B32A32_TYPELESS;
        case RenderFormat::R32G32B32A32_FLOAT: return DXGI_FORMAT_R32G32B32A32_FLOAT;
        case RenderFormat::R32G32B32A32_UINT: return DXGI_FORMAT_R32G32B32A32_UINT;
        case RenderFormat::R32G32B32A32_SINT: return DXGI_FORMAT_R32G32B32A32_SINT;
        case RenderFormat::R32G32B32_TYPELESS: return DXGI_FORMAT_R32G32B32_TYPELESS;
        case RenderFormat::R32G32B32_FLOAT: return DXGI_FORMAT_R32G32B32_FLOAT;
        case RenderFormat::R32G32B32_UINT: return DXGI_FORMAT_R32G32B32_UINT;
        case RenderFormat::R32G32B32_SINT: return DXGI_FORMAT_R32G32B32_SINT;
        case RenderFormat::R16G16B16A16_TYPELESS: return DXGI_FORMAT_R16G16B16A16_TYPELESS;
        case RenderFormat::R16G16B16A16_FLOAT: return DXGI_FORMAT_R16G16B16A16_FLOAT;
        case RenderFormat::R16G16B16A16_UNORM: return DXGI_FORMAT_R16G16B16A16_UNORM;
        case RenderFormat::R16G16B16A16_UINT: return DXGI_FORMAT_R16G16B16A16_UINT;
        case RenderFormat::R16G16B16A16_SNORM: return DXGI_FORMAT_R16G16B16A16_SNORM;
        case RenderFormat::R16G16B16A16_SINT: return DXGI_FORMAT_R16G16B16A16_SINT;
        case RenderFormat::R32G32_TYPELESS: return DXGI_FORMAT_R32G32_TYPELESS;
        case RenderFormat::R32G32_FLOAT: return DXGI_FORMAT_R32G32_FLOAT;
        case RenderFormat::R32G32_UINT: return DXGI_FORMAT_R32G32_UINT;
        case RenderFormat::R32G32_SINT: return DXGI_FORMAT_R32G32_SINT;
        case RenderFormat::R8G8B8A8_TYPELESS: return DXGI_FORMAT_R8G8B8A8_TYPELESS;
        case RenderFormat::R8G8B8A8_UNORM: return DXGI_FORMAT_R8G8B8A8_UNORM;
        case RenderFormat::R8G8B8A8_UINT: return DXGI_FORMAT_R8G8B8A8_UINT;
        case RenderFormat::R8G8B8A8_SNORM: return DXGI_FORMAT_R8G8B8A8_SNORM;
        case RenderFormat::R8G8B8A8_SINT: return DXGI_FORMAT_R8G8B8A8_SINT;
        case RenderFormat::B8G8R8A8_UNORM: return DXGI_FORMAT_B8G8R8A8_UNORM;
        case RenderFormat::R16G16_TYPELESS: return DXGI_FORMAT_R16G16_TYPELESS;
        case RenderFormat::R16G16_FLOAT: return DXGI_FORMAT_R16G16_FLOAT;
        case RenderFormat::R16G16_UNORM: return DXGI_FORMAT_R16G16_UNORM;
        case RenderFormat::R16G16_UINT: return DXGI_FORMAT_R16G16_UINT;
        case RenderFormat::R16G16_SNORM: return DXGI_FORMAT_R16G16_SNORM;
        case RenderFormat::R16G16_SINT: return DXGI_FORMAT_R16G16_SINT;
        case RenderFormat::R32_TYPELESS: return DXGI_FORMAT_R32_TYPELESS;
        case RenderFormat::D32_FLOAT: return DXGI_FORMAT_D32_FLOAT;
        case RenderFormat::D32_FLOAT_S8_UINT: return DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
        case RenderFormat::R32_FLOAT: return DXGI_FORMAT_R32_FLOAT;
        case RenderFormat::R32_UINT: return DXGI_FORMAT_R32_UINT;
        case RenderFormat::R32_SINT: return DXGI_FORMAT_R32_SINT;
        case RenderFormat::R8G8_TYPELESS: return DXGI_FORMAT_R8G8_TYPELESS;
        case RenderFormat::R8G8_UNORM: return DXGI_FORMAT_R8G8_UNORM;
        case RenderFormat::R8G8_UINT: return DXGI_FORMAT_R8G8_UINT;
        case RenderFormat::R8G8_SNORM: return DXGI_FORMAT_R8G8_SNORM;
        case RenderFormat::R8G8_SINT: return DXGI_FORMAT_R8G8_SINT;
        case RenderFormat::R16_TYPELESS: return DXGI_FORMAT_R16_TYPELESS;
        case RenderFormat::R16_FLOAT: return DXGI_FORMAT_R16_FLOAT;
        case RenderFormat::D16_UNORM: return DXGI_FORMAT_D16_UNORM;
        case RenderFormat::R16_UNORM: return DXGI_FORMAT_R16_UNORM;
        case RenderFormat::R16_UINT: return DXGI_FORMAT_R16_UINT;
        case RenderFormat::R16_SNORM: return DXGI_FORMAT_R16_SNORM;
        case RenderFormat::R16_SINT: return DXGI_FORMAT_R16_SINT;
        case RenderFormat::R8_TYPELESS: return DXGI_FORMAT_R8_TYPELESS;
        case RenderFormat::R8_UNORM: return DXGI_FORMAT_R8_UNORM;
        case RenderFormat::R8_UINT: return DXGI_FORMAT_R8_UINT;
        case RenderFormat::R8_SNORM: return DXGI_FORMAT_R8_SNORM;
        case RenderFormat::R8_SINT: return DXGI_FORMAT_R8_SINT;
        case RenderFormat::BC1_TYPELESS: return DXGI_FORMAT_BC1_TYPELESS;
        case RenderFormat::BC1_UNORM: return DXGI_FORMAT_BC1_UNORM;
        case RenderFormat::BC1_UNORM_SRGB: return DXGI_FORMAT_BC1_UNORM_SRGB;
        case RenderFormat::BC2_TYPELESS: return DXGI_FORMAT_BC2_TYPELESS;
        case RenderFormat::BC2_UNORM: return DXGI_FORMAT_BC2_UNORM;
        case RenderFormat::BC2_UNORM_SRGB: return DXGI_FORMAT_BC2_UNORM_SRGB;
        case RenderFormat::BC3_TYPELESS: return DXGI_FORMAT_BC3_TYPELESS;
        case RenderFormat::BC3_UNORM: return DXGI_FORMAT_BC3_UNORM;
        case RenderFormat::BC3_UNORM_SRGB: return DXGI_FORMAT_BC3_UNORM_SRGB;
        case RenderFormat::BC4_TYPELESS: return DXGI_FORMAT_BC4_TYPELESS;
        case RenderFormat::BC4_UNORM: return DXGI_FORMAT_BC4_UNORM;
        case RenderFormat::BC4_SNORM: return DXGI_FORMAT_BC4_SNORM;
        case RenderFormat::BC5_TYPELESS: return DXGI_FORMAT_BC5_TYPELESS;
        case RenderFormat::BC5_UNORM: return DXGI_FORMAT_BC5_UNORM;
        case RenderFormat::BC5_SNORM: return DXGI_FORMAT_BC5_SNORM;
        case RenderFormat::BC6H_TYPELESS: return DXGI_FORMAT_BC6H_TYPELESS;
        case RenderFormat::BC6H_UF16: return DXGI_FORMAT_BC6H_UF16;
        case RenderFormat::BC6H_SF16: return DXGI_FORMAT_BC6H_SF16;
        case RenderFormat::BC7_TYPELESS: return DXGI_FORMAT_BC7_TYPELESS;
        case RenderFormat::BC7_UNORM: return DXGI_FORMAT_BC7_UNORM;
        case RenderFormat::BC7_UNORM_SRGB: return DXGI_FORMAT_BC7_UNORM_SRGB;
        default:
            assert(false && "Unknown format.");
            return DXGI_FORMAT_UNKNOWN;
        }
    }

    // Depth textures are created typeless so they can also be sampled.
    static DXGI_FORMAT toDepthResourceFormat(DXGI_FORMAT format) {
        switch (format) {
        case DXGI_FORMAT_D32_FLOAT: return DXGI_FORMAT_R32_TYPELESS;
        case DXGI_FORMAT_D16_UNORM: return DXGI_FORMAT_R16_TYPELESS;
        case DXGI_FORMAT_D32_FLOAT_S8X24_UINT: return DXGI_FORMAT_R32G8X24_TYPELESS;
        default: return format;
        }
    }

    static DXGI_FORMAT toShaderViewFormat(DXGI_FORMAT format) {
        switch (format) {
        case DXGI_FORMAT_D32_FLOAT:
        case DXGI_FORMAT_R32_TYPELESS:
            return DXGI_FORMAT_R32_FLOAT;
        case DXGI_FORMAT_D16_UNORM:
        case DXGI_FORMAT_R16_TYPELESS:
            return DXGI_FORMAT_R16_UNORM;
        case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
        case DXGI_FORMAT_R32G8X24_TYPELESS:
            return DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
        default:
            return format;
        }
    }

    static D3D11_BLEND toD3D11(RenderBlend blend) {
        switch (blend) {
        case RenderBlend::ZERO: return D3D11_BLEND_ZERO;
        case RenderBlend::ONE: return D3D11_BLEND_ONE;
        case RenderBlend::SRC_COLOR: return D3D11_BLEND_SRC_COLOR;
        case RenderBlend::INV_SRC_COLOR: return D3D11_BLEND_INV_SRC_COLOR;
        case RenderBlend::SRC_ALPHA: return D3D11_BLEND_SRC_ALPHA;
        case RenderBlend::INV_SRC_ALPHA: return D3D11_BLEND_INV_SRC_ALPHA;
        case RenderBlend::DEST_ALPHA: return D3D11_BLEND_DEST_ALPHA;
        case RenderBlend::INV_DEST_ALPHA: return D3D11_BLEND_INV_DEST_ALPHA;
        case RenderBlend::DEST_COLOR: return D3D11_BLEND_DEST_COLOR;
        case RenderBlend::INV_DEST_COLOR: return D3D11_BLEND_INV_DEST_COLOR;
        case RenderBlend::SRC_ALPHA_SAT: return D3D11_BLEND_SRC_ALPHA_SAT;
        case RenderBlend::BLEND_FACTOR: return D3D11_BLEND_BLEND_FACTOR;
        case RenderBlend::INV_BLEND_FACTOR: return D3D11_BLEND_INV_BLEND_FACTOR;
        case RenderBlend::SRC1_COLOR: return D3D11_BLEND_SRC1_COLOR;
        case RenderBlend::INV_SRC1_COLOR: return D3D11_BLEND_INV_SRC1_COLOR;
        case RenderBlend::SRC1_ALPHA: return D3D11_BLEND_SRC1_ALPHA;
        case RenderBlend::INV_SRC1_ALPHA: return D3D11_BLEND_INV_SRC1_ALPHA;
        default: return D3D11_BLEND_ONE;
        }
    }

    static D3D11_BLEND_OP toD3D11(RenderBlendOperation operation) {
        switch (operation) {
        case RenderBlendOperation::SUBTRACT: return D3D11_BLEND_OP_SUBTRACT;
        case RenderBlendOperation::REV_SUBTRACT: return D3D11_BLEND_OP_REV_SUBTRACT;
        case RenderBlendOperation::MIN: return D3D11_BLEND_OP_MIN;
        case RenderBlendOperation::MAX: return D3D11_BLEND_OP_MAX;
        default: return D3D11_BLEND_OP_ADD;
        }
    }

    static D3D11_LOGIC_OP toD3D11(RenderLogicOperation operation) {
        switch (operation) {
        case RenderLogicOperation::CLEAR: return D3D11_LOGIC_OP_CLEAR;
        case RenderLogicOperation::SET: return D3D11_LOGIC_OP_SET;
        case RenderLogicOperation::COPY: return D3D11_LOGIC_OP_COPY;
        case RenderLogicOperation::COPY_INVERTED: return D3D11_LOGIC_OP_COPY_INVERTED;
        case RenderLogicOperation::INVERT: return D3D11_LOGIC_OP_INVERT;
        case RenderLogicOperation::AND: return D3D11_LOGIC_OP_AND;
        case RenderLogicOperation::NAND: return D3D11_LOGIC_OP_NAND;
        case RenderLogicOperation::OR: return D3D11_LOGIC_OP_OR;
        case RenderLogicOperation::NOR: return D3D11_LOGIC_OP_NOR;
        case RenderLogicOperation::XOR: return D3D11_LOGIC_OP_XOR;
        case RenderLogicOperation::EQUIV: return D3D11_LOGIC_OP_EQUIV;
        case RenderLogicOperation::AND_REVERSE: return D3D11_LOGIC_OP_AND_REVERSE;
        case RenderLogicOperation::AND_INVERTED: return D3D11_LOGIC_OP_AND_INVERTED;
        case RenderLogicOperation::OR_REVERSE: return D3D11_LOGIC_OP_OR_REVERSE;
        case RenderLogicOperation::OR_INVERTED: return D3D11_LOGIC_OP_OR_INVERTED;
        default: return D3D11_LOGIC_OP_NOOP;
        }
    }

    static D3D11_COMPARISON_FUNC toD3D11(RenderComparisonFunction function) {
        switch (function) {
        case RenderComparisonFunction::NEVER: return D3D11_COMPARISON_NEVER;
        case RenderComparisonFunction::LESS: return D3D11_COMPARISON_LESS;
        case RenderComparisonFunction::EQUAL: return D3D11_COMPARISON_EQUAL;
        case RenderComparisonFunction::LESS_EQUAL: return D3D11_COMPARISON_LESS_EQUAL;
        case RenderComparisonFunction::GREATER: return D3D11_COMPARISON_GREATER;
        case RenderComparisonFunction::NOT_EQUAL: return D3D11_COMPARISON_NOT_EQUAL;
        case RenderComparisonFunction::GREATER_EQUAL: return D3D11_COMPARISON_GREATER_EQUAL;
        default: return D3D11_COMPARISON_ALWAYS;
        }
    }

    static D3D11_STENCIL_OP toD3D11(RenderStencilOp operation) {
        switch (operation) {
        case RenderStencilOp::ZERO: return D3D11_STENCIL_OP_ZERO;
        case RenderStencilOp::REPLACE: return D3D11_STENCIL_OP_REPLACE;
        case RenderStencilOp::INCREMENT_AND_CLAMP: return D3D11_STENCIL_OP_INCR_SAT;
        case RenderStencilOp::DECREMENT_AND_CLAMP: return D3D11_STENCIL_OP_DECR_SAT;
        case RenderStencilOp::INVERT: return D3D11_STENCIL_OP_INVERT;
        case RenderStencilOp::INCREMENT_AND_WRAP: return D3D11_STENCIL_OP_INCR;
        case RenderStencilOp::DECREMENT_AND_WRAP: return D3D11_STENCIL_OP_DECR;
        default: return D3D11_STENCIL_OP_KEEP;
        }
    }

    static D3D11_PRIMITIVE_TOPOLOGY toD3D11(RenderPrimitiveTopology topology) {
        switch (topology) {
        case RenderPrimitiveTopology::POINT_LIST: return D3D11_PRIMITIVE_TOPOLOGY_POINTLIST;
        case RenderPrimitiveTopology::LINE_LIST: return D3D11_PRIMITIVE_TOPOLOGY_LINELIST;
        case RenderPrimitiveTopology::LINE_STRIP: return D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP;
        case RenderPrimitiveTopology::TRIANGLE_LIST: return D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        case RenderPrimitiveTopology::TRIANGLE_STRIP: return D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
        default:
            assert(false && "Unsupported primitive topology.");
            return D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
        }
    }

    static D3D11_TEXTURE_ADDRESS_MODE toD3D11(RenderTextureAddressMode addressMode) {
        switch (addressMode) {
        case RenderTextureAddressMode::MIRROR: return D3D11_TEXTURE_ADDRESS_MIRROR;
        case RenderTextureAddressMode::CLAMP: return D3D11_TEXTURE_ADDRESS_CLAMP;
        case RenderTextureAddressMode::BORDER: return D3D11_TEXTURE_ADDRESS_BORDER;
        case RenderTextureAddressMode::MIRROR_ONCE: return D3D11_TEXTURE_ADDRESS_MIRROR_ONCE;
        default: return D3D11_TEXTURE_ADDRESS_WRAP;
        }
    }

    static D3D11_FILTER toFilter(const RenderSamplerDesc &desc) {
        if (desc.anisotropyEnabled) {
            return desc.comparisonEnabled ? D3D11_FILTER_COMPARISON_ANISOTROPIC : D3D11_FILTER_ANISOTROPIC;
        }

        uint32_t filter = 0;
        filter |= (desc.mipmapMode == RenderMipmapMode::LINEAR) ? 0x1 : 0x0;
        filter |= (desc.magFilter == RenderFilter::LINEAR) ? 0x4 : 0x0;
        filter |= (desc.minFilter == RenderFilter::LINEAR) ? 0x10 : 0x0;
        filter |= desc.comparisonEnabled ? 0x80 : 0x0;
        return D3D11_FILTER(filter);
    }

    static bool isDepthFormat(DXGI_FORMAT format) {
        return (format == DXGI_FORMAT_D32_FLOAT) || (format == DXGI_FORMAT_D16_UNORM) || (format == DXGI_FORMAT_D32_FLOAT_S8X24_UINT);
    }

    static uint64_t hashBytes(uint64_t hash, const void *data, size_t size) {
        const uint8_t *bytes = static_cast<const uint8_t *>(data);
        for (size_t i = 0; i < size; i++) {
            hash = (hash ^ bytes[i]) * 0x100000001B3ULL;
        }

        return hash;
    }

    // D3D11DescriptorSet

    D3D11DescriptorSet::D3D11DescriptorSet(D3D11Device *device, const RenderDescriptorSetDesc &desc) {
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
    }

    const D3D11DescriptorSet::Range *D3D11DescriptorSet::rangeOf(uint32_t descriptorIndex) const {
        for (const Range &range : ranges) {
            if ((descriptorIndex >= range.first) && (descriptorIndex < (range.first + range.count))) {
                return &range;
            }
        }

        return nullptr;
    }

    const D3D11Descriptor *D3D11DescriptorSet::find(uint32_t binding, uint32_t element) const {
        for (const Range &range : ranges) {
            if ((range.binding == binding) && (element < range.count)) {
                return &descriptors[range.first + element];
            }
        }

        return nullptr;
    }

    void D3D11DescriptorSet::setBuffer(uint32_t descriptorIndex, const RenderBuffer *buffer, uint64_t bufferSize, const RenderBufferStructuredView *bufferStructuredView, const RenderBufferFormattedView *bufferFormattedView) {
        const Range *range = rangeOf(descriptorIndex);
        assert(range != nullptr);

        D3D11Descriptor &descriptor = descriptors[descriptorIndex];
        descriptor = D3D11Descriptor();
        if (buffer == nullptr) {
            return;
        }

        const D3D11Buffer *interfaceBuffer = static_cast<const D3D11Buffer *>(buffer);
        descriptor.buffer = interfaceBuffer;
        if (range->type == RenderDescriptorRangeType::CONSTANT_BUFFER) {
            descriptor.constantBuffer = true;
            return;
        }

        if (interfaceBuffer->d3d == nullptr) {
            return;
        }

        D3D11_BUFFER_DESC bufferDesc;
        interfaceBuffer->d3d->GetDesc(&bufferDesc);

        const uint64_t viewBytes = (bufferSize > 0) ? bufferSize : interfaceBuffer->desc.size;
        const bool formatted = (range->type == RenderDescriptorRangeType::FORMATTED_BUFFER) || (range->type == RenderDescriptorRangeType::READ_WRITE_FORMATTED_BUFFER);
        if (formatted) {
            assert(bufferFormattedView != nullptr);
            const RenderFormat format = static_cast<const D3D11BufferFormattedView *>(bufferFormattedView)->format;
            const UINT elementCount = UINT(viewBytes / RenderFormatSize(format));
            if (bufferDesc.BindFlags & D3D11_BIND_SHADER_RESOURCE) {
                D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
                srvDesc.Format = toDXGI(format);
                srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
                srvDesc.Buffer.NumElements = elementCount;
                device->d3d->CreateShaderResourceView(interfaceBuffer->d3d.Get(), &srvDesc, descriptor.srv.GetAddressOf());
            }

            if ((range->type == RenderDescriptorRangeType::READ_WRITE_FORMATTED_BUFFER) && (bufferDesc.BindFlags & D3D11_BIND_UNORDERED_ACCESS)) {
                D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
                uavDesc.Format = toDXGI(format);
                uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
                uavDesc.Buffer.NumElements = elementCount;
                device->d3d->CreateUnorderedAccessView(interfaceBuffer->d3d.Get(), &uavDesc, descriptor.uav.GetAddressOf());
            }

            return;
        }

        // Storage buffers are ByteAddressBuffers after SPIRV-Cross, so every view is raw.
        const uint64_t firstByte = (bufferStructuredView != nullptr) ? uint64_t(bufferStructuredView->firstElement) * bufferStructuredView->structureByteStride : 0;
        const UINT firstElement = UINT(firstByte / 4);
        const UINT elementCount = UINT(std::min<uint64_t>(viewBytes, bufferDesc.ByteWidth - firstByte) / 4);
        if ((bufferDesc.MiscFlags & D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS) == 0) {
            if ((device->featureLevel < D3D_FEATURE_LEVEL_11_0) && (bufferDesc.BindFlags & D3D11_BIND_SHADER_RESOURCE)) {
                D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
                srvDesc.Format = DXGI_FORMAT_R32_UINT;
                srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
                srvDesc.Buffer.FirstElement = firstElement;
                srvDesc.Buffer.NumElements = elementCount;
                device->d3d->CreateShaderResourceView(interfaceBuffer->d3d.Get(), &srvDesc, descriptor.srv.GetAddressOf());
            }

            return;
        }

        if (bufferDesc.BindFlags & D3D11_BIND_SHADER_RESOURCE) {
            D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
            srvDesc.Format = DXGI_FORMAT_R32_TYPELESS;
            srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFEREX;
            srvDesc.BufferEx.FirstElement = firstElement;
            srvDesc.BufferEx.NumElements = elementCount;
            srvDesc.BufferEx.Flags = D3D11_BUFFEREX_SRV_FLAG_RAW;
            device->d3d->CreateShaderResourceView(interfaceBuffer->d3d.Get(), &srvDesc, descriptor.srv.GetAddressOf());
        }

        if (bufferDesc.BindFlags & D3D11_BIND_UNORDERED_ACCESS) {
            D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
            uavDesc.Format = DXGI_FORMAT_R32_TYPELESS;
            uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
            uavDesc.Buffer.FirstElement = firstElement;
            uavDesc.Buffer.NumElements = elementCount;
            uavDesc.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;
            device->d3d->CreateUnorderedAccessView(interfaceBuffer->d3d.Get(), &uavDesc, descriptor.uav.GetAddressOf());
        }
    }

    static bool fillSRVDesc(const D3D11Texture *texture, const RenderTextureViewDesc *viewDesc, D3D11_SHADER_RESOURCE_VIEW_DESC &srvDesc) {
        const RenderTextureDesc &desc = texture->desc;
        const RenderFormat format = ((viewDesc != nullptr) && (viewDesc->format != RenderFormat::UNKNOWN)) ? viewDesc->format : desc.format;
        RenderTextureViewDimension dimension = (viewDesc != nullptr) ? viewDesc->dimension : RenderTextureDimensionToView(desc.dimension);
        if ((desc.flags & RenderTextureFlag::CUBE) && (viewDesc == nullptr)) {
            dimension = RenderTextureViewDimension::TEXTURE_CUBE;
        }

        const UINT mostDetailedMip = (viewDesc != nullptr) ? viewDesc->mipSlice : 0;
        const UINT mipLevels = ((viewDesc != nullptr) && (viewDesc->mipLevels != UINT32_MAX)) ? viewDesc->mipLevels : UINT(-1);
        const UINT arrayIndex = (viewDesc != nullptr) ? viewDesc->arrayIndex : 0;
        const UINT arraySize = ((viewDesc != nullptr) && (viewDesc->arraySize != UINT32_MAX)) ? viewDesc->arraySize : std::max(desc.arraySize, 1U) - arrayIndex;
        const bool arrayed = desc.arraySize > 1;
        if ((viewDesc != nullptr) && ((viewDesc->componentMapping.r != RenderSwizzle::IDENTITY) || (viewDesc->componentMapping.g != RenderSwizzle::IDENTITY) || (viewDesc->componentMapping.b != RenderSwizzle::IDENTITY) || (viewDesc->componentMapping.a != RenderSwizzle::IDENTITY))) {
            warnOnce("Texture view component swizzles are not supported in D3D11 and are ignored.");
        }

        srvDesc = {};
        srvDesc.Format = toShaderViewFormat(toDXGI(format));
        switch (dimension) {
        case RenderTextureViewDimension::TEXTURE_1D:
            if (arrayed) {
                srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE1DARRAY;
                srvDesc.Texture1DArray = { mostDetailedMip, mipLevels, arrayIndex, arraySize };
            }
            else {
                srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE1D;
                srvDesc.Texture1D = { mostDetailedMip, mipLevels };
            }
            break;
        case RenderTextureViewDimension::TEXTURE_2D:
            if (desc.multisampling.sampleCount > 1) {
                srvDesc.ViewDimension = arrayed ? D3D11_SRV_DIMENSION_TEXTURE2DMSARRAY : D3D11_SRV_DIMENSION_TEXTURE2DMS;
                if (arrayed) {
                    srvDesc.Texture2DMSArray = { arrayIndex, arraySize };
                }
            }
            else if (arrayed) {
                srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
                srvDesc.Texture2DArray = { mostDetailedMip, mipLevels, arrayIndex, arraySize };
            }
            else {
                srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
                srvDesc.Texture2D = { mostDetailedMip, mipLevels };
            }
            break;
        case RenderTextureViewDimension::TEXTURE_3D:
            srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE3D;
            srvDesc.Texture3D = { mostDetailedMip, mipLevels };
            break;
        case RenderTextureViewDimension::TEXTURE_CUBE:
            srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
            srvDesc.TextureCube = { mostDetailedMip, mipLevels };
            break;
        default:
            return false;
        }

        return true;
    }

    void D3D11DescriptorSet::setTexture(uint32_t descriptorIndex, const RenderTexture *texture, RenderTextureLayout textureLayout, const RenderTextureView *textureView) {
        const Range *range = rangeOf(descriptorIndex);
        assert(range != nullptr);

        D3D11Descriptor &descriptor = descriptors[descriptorIndex];
        descriptor = D3D11Descriptor();
        if (texture == nullptr) {
            return;
        }

        const D3D11Texture *interfaceTexture = static_cast<const D3D11Texture *>(texture);
        const D3D11TextureView *interfaceView = static_cast<const D3D11TextureView *>(textureView);
        if (interfaceView != nullptr) {
            descriptor.srv = interfaceView->srv;
        }
        else {
            descriptor.srv = interfaceTexture->getDefaultSRV();
        }

        if (descriptor.srv != nullptr) {
            D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
            descriptor.srv->GetDesc(&srvDesc);
            descriptor.srvDimension = uint32_t(srvDesc.ViewDimension);
        }

        if (range->type == RenderDescriptorRangeType::READ_WRITE_TEXTURE) {
            const RenderTextureDesc &desc = interfaceTexture->desc;
            const RenderFormat format = ((interfaceView != nullptr) && (interfaceView->desc.format != RenderFormat::UNKNOWN)) ? interfaceView->desc.format : desc.format;
            const UINT mipSlice = (interfaceView != nullptr) ? interfaceView->desc.mipSlice : 0;
            D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
            uavDesc.Format = toDXGI(format);
            switch (desc.dimension) {
            case RenderTextureDimension::TEXTURE_1D:
                uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE1D;
                uavDesc.Texture1D.MipSlice = mipSlice;
                break;
            case RenderTextureDimension::TEXTURE_3D:
                uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE3D;
                uavDesc.Texture3D = { mipSlice, 0, UINT(-1) };
                break;
            default:
                uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
                uavDesc.Texture2D.MipSlice = mipSlice;
                break;
            }

            device->d3d->CreateUnorderedAccessView(interfaceTexture->d3d.Get(), &uavDesc, descriptor.uav.GetAddressOf());
        }
    }

    void D3D11DescriptorSet::setSampler(uint32_t descriptorIndex, const RenderSampler *sampler) {
        assert(descriptorIndex < descriptors.size());
        descriptors[descriptorIndex] = D3D11Descriptor();
        if (sampler != nullptr) {
            descriptors[descriptorIndex].sampler = static_cast<const D3D11Sampler *>(sampler)->d3d;
        }
    }

    void D3D11DescriptorSet::setAccelerationStructure(uint32_t descriptorIndex, const RenderAccelerationStructure *accelerationStructure) {
        assert(false && "Acceleration structures are not supported in D3D11.");
    }

    // D3D11Buffer

    D3D11Buffer::D3D11Buffer(D3D11Device *device, const RenderBufferDesc &desc) {
        this->device = device;
        this->desc = desc;

        const uint64_t size = roundUp(std::max<uint64_t>(desc.size, 16), 16);
        if (isUpload() || isReadback()) {
            shadow.resize(size_t(size), 0);
        }

        if (isReadback()) {
            return;
        }

        D3D11_BUFFER_DESC bufferDesc = {};
        bufferDesc.ByteWidth = UINT(size);
        bufferDesc.Usage = D3D11_USAGE_DEFAULT;
        if (desc.flags & RenderBufferFlag::VERTEX) {
            bufferDesc.BindFlags |= D3D11_BIND_VERTEX_BUFFER;
        }

        if (desc.flags & RenderBufferFlag::INDEX) {
            bufferDesc.BindFlags |= D3D11_BIND_INDEX_BUFFER;
        }

        if (desc.flags & (RenderBufferFlag::STORAGE | RenderBufferFlag::FORMATTED | RenderBufferFlag::UNORDERED_ACCESS)) {
            bufferDesc.BindFlags |= D3D11_BIND_SHADER_RESOURCE;

            // Feature level 10_x has no raw views; its shaders read storage buffers through typed views instead.
            if (device->featureLevel >= D3D_FEATURE_LEVEL_11_0) {
                bufferDesc.MiscFlags |= D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
            }
        }

        if ((desc.flags & RenderBufferFlag::UNORDERED_ACCESS) && (device->featureLevel >= D3D_FEATURE_LEVEL_11_0)) {
            bufferDesc.BindFlags |= D3D11_BIND_UNORDERED_ACCESS;
        }

        // Upload buffers with no bind flags only feed copies, which read the CPU shadow directly.
        if (isUpload() && (bufferDesc.BindFlags == 0)) {
            return;
        }

        HRESULT res = device->d3d->CreateBuffer(&bufferDesc, nullptr, d3d.GetAddressOf());
        if (FAILED(res)) {
            fprintf(stderr, "[plume-d3d11] CreateBuffer failed with error code 0x%lX.\n", res);
        }
    }

    void *D3D11Buffer::map(uint32_t subresource, const RenderRange *readRange) {
        if (isReadback() && !pendingReadbacks.empty()) {
            std::scoped_lock lock(device->contextMutex);
            ID3D11DeviceContext1 *ctx = device->immediateContext.Get();
            for (const PendingReadback &pending : pendingReadbacks) {
                D3D11_MAPPED_SUBRESOURCE mapped;
                if (FAILED(ctx->Map(pending.staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) {
                    continue;
                }

                for (uint32_t z = 0; z < pending.depth; z++) {
                    for (uint32_t row = 0; row < pending.rows; row++) {
                        const uint8_t *src = static_cast<const uint8_t *>(mapped.pData) + z * mapped.DepthPitch + row * mapped.RowPitch;
                        const uint64_t dstOffset = pending.dstOffset + (uint64_t(z) * pending.rows + row) * pending.rowPitch;
                        if ((dstOffset + pending.rowBytes) <= shadow.size()) {
                            memcpy(&shadow[size_t(dstOffset)], src, pending.rowBytes);
                        }
                    }
                }

                ctx->Unmap(pending.staging.Get(), 0);
            }

            pendingReadbacks.clear();
        }

        mapped = true;
        writeGeneration++;
        return shadow.data();
    }

    void D3D11Buffer::unmap(uint32_t subresource, const RenderRange *writtenRange) {
        writeGeneration++;
        mapped = false;
    }

    std::unique_ptr<RenderBufferFormattedView> D3D11Buffer::createBufferFormattedView(RenderFormat format) {
        return std::make_unique<D3D11BufferFormattedView>(format);
    }

    void D3D11Buffer::setName(const std::string &name) {
        if (d3d != nullptr) {
            d3d->SetPrivateData(WKPDID_D3DDebugObjectName, UINT(name.size()), name.data());
        }
    }

    uint64_t D3D11Buffer::getDeviceAddress() const {
        assert(false && "Buffer device addresses are not supported in D3D11.");
        return 0;
    }

    bool D3D11Buffer::isUpload() const {
        return (desc.heapType == RenderHeapType::UPLOAD) || (desc.heapType == RenderHeapType::GPU_UPLOAD);
    }

    bool D3D11Buffer::isReadback() const {
        return desc.heapType == RenderHeapType::READBACK;
    }

    // Upload buffers stay mapped, so a mapped buffer is re-uploaded once per submission it's used in.
    void D3D11Buffer::sync(D3D11Context &context) const {
        if (!isUpload() || (d3d == nullptr)) {
            return;
        }

        const uint64_t generation = writeGeneration;
        const bool changed = (generation != syncedGeneration);
        if (!changed && (!mapped || (syncedSubmission == context.submission))) {
            return;
        }

        context.ctx->UpdateSubresource(d3d.Get(), 0, nullptr, shadow.data(), 0, 0);
        syncedSubmission = context.submission;
        syncedGeneration = generation;
    }

    // D3D11 can't combine the constant buffer binding with others, so constants are copied into a twin buffer on bind.
    ID3D11Buffer *D3D11Buffer::constantBuffer(D3D11Context &context) const {
        const UINT size = UINT(std::min<uint64_t>(roundUp(std::max<uint64_t>(desc.size, 16), 16), MaxConstantBufferBytes));
        if (cbuffer == nullptr) {
            D3D11_BUFFER_DESC bufferDesc = {};
            bufferDesc.ByteWidth = size;
            bufferDesc.Usage = D3D11_USAGE_DEFAULT;
            bufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            if (FAILED(device->d3d->CreateBuffer(&bufferDesc, nullptr, cbuffer.GetAddressOf()))) {
                return nullptr;
            }
        }

        if (isUpload()) {
            context.ctx->UpdateSubresource(cbuffer.Get(), 0, nullptr, shadow.data(), 0, 0);
        }
        else if (d3d != nullptr) {
            D3D11_BOX box = { 0, 0, 0, size, 1, 1 };
            context.ctx->CopySubresourceRegion(cbuffer.Get(), 0, 0, 0, 0, d3d.Get(), 0, &box);
        }

        return cbuffer.Get();
    }

    // D3D11BufferFormattedView

    D3D11BufferFormattedView::D3D11BufferFormattedView(RenderFormat format) {
        this->format = format;
    }

    // D3D11Texture

    D3D11Texture::D3D11Texture(D3D11Device *device, const RenderTextureDesc &desc) {
        this->device = device;
        this->desc = desc;

        const DXGI_FORMAT format = toDXGI(desc.format);
        resourceFormat = isDepthFormat(format) ? toDepthResourceFormat(format) : format;

        UINT bindFlags = D3D11_BIND_SHADER_RESOURCE;
        if (desc.flags & RenderTextureFlag::RENDER_TARGET) {
            bindFlags |= D3D11_BIND_RENDER_TARGET;
        }

        if (desc.flags & RenderTextureFlag::DEPTH_TARGET) {
            bindFlags |= D3D11_BIND_DEPTH_STENCIL;
        }

        // Feature level 10_x has no texture UAVs; those passes need a graphics or CPU path there.
        if ((desc.flags & (RenderTextureFlag::STORAGE | RenderTextureFlag::UNORDERED_ACCESS)) && (device->featureLevel >= D3D_FEATURE_LEVEL_11_0)) {
            bindFlags |= D3D11_BIND_UNORDERED_ACCESS;
        }

        const UINT mipLevels = std::max(desc.mipLevels, 1U);
        const UINT arraySize = std::max(desc.arraySize, 1U);
        HRESULT res = E_FAIL;
        switch (desc.dimension) {
        case RenderTextureDimension::TEXTURE_1D: {
            D3D11_TEXTURE1D_DESC textureDesc = {};
            textureDesc.Width = desc.width;
            textureDesc.MipLevels = mipLevels;
            textureDesc.ArraySize = arraySize;
            textureDesc.Format = resourceFormat;
            textureDesc.Usage = D3D11_USAGE_DEFAULT;
            textureDesc.BindFlags = bindFlags;
            ID3D11Texture1D *texture = nullptr;
            res = device->d3d->CreateTexture1D(&textureDesc, nullptr, &texture);
            d3d.Attach(texture);
            break;
        }
        case RenderTextureDimension::TEXTURE_2D: {
            D3D11_TEXTURE2D_DESC textureDesc = {};
            textureDesc.Width = desc.width;
            textureDesc.Height = desc.height;
            textureDesc.MipLevels = mipLevels;
            textureDesc.ArraySize = arraySize;
            textureDesc.Format = resourceFormat;
            textureDesc.SampleDesc.Count = std::max(desc.multisampling.sampleCount, 1U);
            textureDesc.Usage = D3D11_USAGE_DEFAULT;
            textureDesc.BindFlags = bindFlags;
            textureDesc.MiscFlags = (desc.flags & RenderTextureFlag::CUBE) ? D3D11_RESOURCE_MISC_TEXTURECUBE : 0;
            ID3D11Texture2D *texture = nullptr;
            res = device->d3d->CreateTexture2D(&textureDesc, nullptr, &texture);
            d3d.Attach(texture);
            break;
        }
        case RenderTextureDimension::TEXTURE_3D: {
            D3D11_TEXTURE3D_DESC textureDesc = {};
            textureDesc.Width = desc.width;
            textureDesc.Height = desc.height;
            textureDesc.Depth = desc.depth;
            textureDesc.MipLevels = mipLevels;
            textureDesc.Format = resourceFormat;
            textureDesc.Usage = D3D11_USAGE_DEFAULT;
            textureDesc.BindFlags = bindFlags;
            ID3D11Texture3D *texture = nullptr;
            res = device->d3d->CreateTexture3D(&textureDesc, nullptr, &texture);
            d3d.Attach(texture);
            break;
        }
        default:
            assert(false && "Unknown texture dimension.");
            break;
        }

        if (FAILED(res)) {
            fprintf(stderr, "[plume-d3d11] Texture creation failed with error code 0x%lX (%ux%u format %d).\n", res, desc.width, desc.height, int(desc.format));
        }
    }

    std::unique_ptr<RenderTextureView> D3D11Texture::createTextureView(const RenderTextureViewDesc &desc) const {
        return std::make_unique<D3D11TextureView>(this, desc);
    }

    void D3D11Texture::setName(const std::string &name) {
        if (d3d != nullptr) {
            d3d->SetPrivateData(WKPDID_D3DDebugObjectName, UINT(name.size()), name.data());
        }
    }

    ID3D11ShaderResourceView *D3D11Texture::getDefaultSRV() const {
        std::scoped_lock lock(viewMutex);
        if ((defaultSRV == nullptr) && (d3d != nullptr)) {
            D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
            if (fillSRVDesc(this, nullptr, srvDesc)) {
                device->d3d->CreateShaderResourceView(d3d.Get(), &srvDesc, defaultSRV.GetAddressOf());
            }
        }

        return defaultSRV.Get();
    }

    uint32_t D3D11Texture::subresourceIndex(uint32_t mipLevel, uint32_t arrayIndex) const {
        return D3D11CalcSubresource(mipLevel, arrayIndex, std::max(desc.mipLevels, 1U));
    }

    // D3D11TextureView

    D3D11TextureView::D3D11TextureView(const D3D11Texture *texture, const RenderTextureViewDesc &desc) {
        this->texture = texture;
        this->desc = desc;

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
        if ((texture->d3d != nullptr) && fillSRVDesc(texture, &desc, srvDesc)) {
            texture->device->d3d->CreateShaderResourceView(texture->d3d.Get(), &srvDesc, srv.GetAddressOf());
        }
    }

    // D3D11Shader

    D3D11Shader::D3D11Shader(const void *data, uint64_t size, const char *entryPointName, RenderShaderFormat format) {
        assert((format == RenderShaderFormat::SPIRV) && "D3D11 consumes SPIR-V and translates it.");
        assert((size % sizeof(uint32_t)) == 0);

        spirv.resize(size_t(size / sizeof(uint32_t)));
        memcpy(spirv.data(), data, size_t(size));
        this->entryPointName = (entryPointName != nullptr) ? entryPointName : "main";
        this->format = format;
    }

    void D3D11Shader::setName(const std::string &name) { }

    // D3D11Sampler

    D3D11Sampler::D3D11Sampler(D3D11Device *device, const RenderSamplerDesc &desc) {
        D3D11_SAMPLER_DESC samplerDesc = {};
        samplerDesc.Filter = toFilter(desc);
        samplerDesc.AddressU = toD3D11(desc.addressU);
        samplerDesc.AddressV = toD3D11(desc.addressV);
        samplerDesc.AddressW = toD3D11(desc.addressW);
        samplerDesc.MipLODBias = desc.mipLODBias;
        samplerDesc.MaxAnisotropy = desc.maxAnisotropy;
        samplerDesc.ComparisonFunc = toD3D11(desc.comparisonFunc);
        samplerDesc.MinLOD = desc.minLOD;
        samplerDesc.MaxLOD = desc.maxLOD;
        const float alpha = (desc.borderColor == RenderBorderColor::TRANSPARENT_BLACK) ? 0.0f : 1.0f;
        const float color = (desc.borderColor == RenderBorderColor::OPAQUE_WHITE) ? 1.0f : 0.0f;
        samplerDesc.BorderColor[0] = color;
        samplerDesc.BorderColor[1] = color;
        samplerDesc.BorderColor[2] = color;
        samplerDesc.BorderColor[3] = alpha;
        device->d3d->CreateSamplerState(&samplerDesc, d3d.GetAddressOf());
    }

    // D3D11Pipeline

    D3D11Pipeline::D3D11Pipeline(Type type) {
        this->type = type;
    }

    void D3D11Pipeline::setName(const std::string &name) { }

    RenderPipelineProgram D3D11Pipeline::getProgram(const std::string &name) const {
        assert(false && "Pipeline programs are only used by raytracing.");
        return RenderPipelineProgram();
    }

    D3D11ComputePipeline::D3D11ComputePipeline(D3D11Device *device, const RenderComputePipelineDesc &desc) : D3D11Pipeline(Type::Compute) {
        uint32_t stage = 0;
        if (!device->translateShader(desc.computeShader, desc.specConstants, desc.specConstantsCount, 0, compute, stage)) {
            return;
        }

        ID3D11ComputeShader *shader = nullptr;
        if (FAILED(device->d3d->CreateComputeShader(compute.bytecode.data(), compute.bytecode.size(), nullptr, &shader))) {
            return;
        }

        compute.shader.Attach(shader);
        valid = true;
    }

    D3D11GraphicsPipeline::D3D11GraphicsPipeline(D3D11Device *device, const RenderGraphicsPipelineDesc &desc) : D3D11Pipeline(Type::Graphics) {
        uint32_t stage = 0;
        renderTargetCount = desc.renderTargetCount;
        if (!device->translateShader(desc.vertexShader, desc.specConstants, desc.specConstantsCount, 0, vertex, stage)) {
            return;
        }

        ID3D11VertexShader *vertexShader = nullptr;
        if (FAILED(device->d3d->CreateVertexShader(vertex.bytecode.data(), vertex.bytecode.size(), nullptr, &vertexShader))) {
            return;
        }

        vertex.shader.Attach(vertexShader);

        if (desc.geometryShader != nullptr) {
            if (!device->translateShader(desc.geometryShader, desc.specConstants, desc.specConstantsCount, 0, geometry, stage)) {
                return;
            }

            ID3D11GeometryShader *geometryShader = nullptr;
            if (FAILED(device->d3d->CreateGeometryShader(geometry.bytecode.data(), geometry.bytecode.size(), nullptr, &geometryShader))) {
                return;
            }

            geometry.shader.Attach(geometryShader);
        }

        if (desc.pixelShader != nullptr) {
            // Pixel shader UAVs share slots with render targets.
            if (!device->translateShader(desc.pixelShader, desc.specConstants, desc.specConstantsCount, desc.renderTargetCount, pixel, stage)) {
                return;
            }

            ID3D11PixelShader *pixelShader = nullptr;
            if (FAILED(device->d3d->CreatePixelShader(pixel.bytecode.data(), pixel.bytecode.size(), nullptr, &pixelShader))) {
                return;
            }

            pixel.shader.Attach(pixelShader);
        }

        if (desc.inputElementsCount > 0) {
            std::vector<D3D11_INPUT_ELEMENT_DESC> elements(desc.inputElementsCount);
            for (uint32_t i = 0; i < desc.inputElementsCount; i++) {
                const RenderInputElement &element = desc.inputElements[i];
                bool perInstance = false;
                for (uint32_t s = 0; s < desc.inputSlotsCount; s++) {
                    if (desc.inputSlots[s].index == element.slotIndex) {
                        perInstance = desc.inputSlots[s].classification == RenderInputSlotClassification::PER_INSTANCE_DATA;
                    }
                }

                // SPIRV-Cross names every vertex input TEXCOORD<location>.
                elements[i].SemanticName = "TEXCOORD";
                elements[i].SemanticIndex = element.location;
                elements[i].Format = toDXGI(element.format);
                elements[i].InputSlot = element.slotIndex;
                elements[i].AlignedByteOffset = element.alignedByteOffset;
                elements[i].InputSlotClass = perInstance ? D3D11_INPUT_PER_INSTANCE_DATA : D3D11_INPUT_PER_VERTEX_DATA;
                elements[i].InstanceDataStepRate = perInstance ? 1 : 0;
            }

            if (FAILED(device->d3d->CreateInputLayout(elements.data(), UINT(elements.size()), vertex.bytecode.data(), vertex.bytecode.size(), inputLayout.GetAddressOf()))) {
                fprintf(stderr, "[plume-d3d11] CreateInputLayout failed.\n");
                return;
            }
        }

        D3D11_RASTERIZER_DESC rasterizerDesc = {};
        rasterizerDesc.FillMode = D3D11_FILL_SOLID;
        rasterizerDesc.CullMode = (desc.cullMode == RenderCullMode::FRONT) ? D3D11_CULL_FRONT : (desc.cullMode == RenderCullMode::BACK) ? D3D11_CULL_BACK : D3D11_CULL_NONE;
        rasterizerDesc.FrontCounterClockwise = (desc.frontFace == RenderFrontFace::COUNTER_CLOCKWISE);
        rasterizerDesc.DepthBias = desc.depthBias;
        rasterizerDesc.DepthBiasClamp = desc.depthBiasClamp;
        rasterizerDesc.SlopeScaledDepthBias = desc.slopeScaledDepthBias;
        rasterizerDesc.DepthClipEnable = desc.depthClipEnabled;
        rasterizerDesc.ScissorEnable = TRUE;
        rasterizerDesc.MultisampleEnable = (desc.multisampling.sampleCount > 1);
        device->d3d->CreateRasterizerState(&rasterizerDesc, rasterizerState.GetAddressOf());

        D3D11_BLEND_DESC1 blendDesc = {};
        blendDesc.AlphaToCoverageEnable = desc.alphaToCoverageEnabled;
        blendDesc.IndependentBlendEnable = TRUE;
        for (uint32_t i = 0; i < desc.renderTargetCount; i++) {
            const RenderBlendDesc &targetBlend = desc.renderTargetBlend[i];
            D3D11_RENDER_TARGET_BLEND_DESC1 &target = blendDesc.RenderTarget[i];
            target.BlendEnable = targetBlend.blendEnabled;
            target.LogicOpEnable = desc.logicOpEnabled;
            target.SrcBlend = toD3D11(targetBlend.srcBlend);
            target.DestBlend = toD3D11(targetBlend.dstBlend);
            target.BlendOp = toD3D11(targetBlend.blendOp);
            target.SrcBlendAlpha = toD3D11(targetBlend.srcBlendAlpha);
            target.DestBlendAlpha = toD3D11(targetBlend.dstBlendAlpha);
            target.BlendOpAlpha = toD3D11(targetBlend.blendOpAlpha);
            target.LogicOp = toD3D11(desc.logicOp);
            target.RenderTargetWriteMask = targetBlend.renderTargetWriteMask;
        }

        ID3D11BlendState1 *blendState1 = nullptr;
        if (SUCCEEDED(device->d3d->CreateBlendState1(&blendDesc, &blendState1))) {
            blendState.Attach(blendState1);
        }

        D3D11_DEPTH_STENCIL_DESC depthStencilDesc = {};
        depthStencilDesc.DepthEnable = desc.depthEnabled;
        depthStencilDesc.DepthWriteMask = desc.depthWriteEnabled ? D3D11_DEPTH_WRITE_MASK_ALL : D3D11_DEPTH_WRITE_MASK_ZERO;
        depthStencilDesc.DepthFunc = toD3D11(desc.depthFunction);
        depthStencilDesc.StencilEnable = desc.stencilEnabled;
        depthStencilDesc.StencilReadMask = UINT8(desc.stencilReadMask);
        depthStencilDesc.StencilWriteMask = UINT8(desc.stencilWriteMask);
        depthStencilDesc.FrontFace = { toD3D11(desc.stencilFrontFace.failOp), toD3D11(desc.stencilFrontFace.depthFailOp), toD3D11(desc.stencilFrontFace.passOp), toD3D11(desc.stencilFrontFace.compareFunction) };
        depthStencilDesc.BackFace = { toD3D11(desc.stencilBackFace.failOp), toD3D11(desc.stencilBackFace.depthFailOp), toD3D11(desc.stencilBackFace.passOp), toD3D11(desc.stencilBackFace.compareFunction) };
        device->d3d->CreateDepthStencilState(&depthStencilDesc, depthStencilState.GetAddressOf());

        topology = toD3D11(desc.primitiveTopology);
        stencilRef = desc.stencilReference;
        valid = (rasterizerState != nullptr) && (blendState != nullptr) && (depthStencilState != nullptr);
    }

    // D3D11PipelineLayout

    static uint64_t samplerKey(uint32_t set, uint32_t binding, uint32_t element) {
        return (uint64_t(set) << 48) | (uint64_t(binding) << 24) | element;
    }

    D3D11PipelineLayout::D3D11PipelineLayout(D3D11Device *device, const RenderPipelineLayoutDesc &desc) {
        pushConstantRanges.assign(desc.pushConstantRanges, desc.pushConstantRanges + desc.pushConstantRangesCount);
        for (const RenderPushConstantRange &range : pushConstantRanges) {
            pushConstantSize = std::max(pushConstantSize, range.offset + range.size);
        }

        for (uint32_t set = 0; set < desc.descriptorSetDescsCount; set++) {
            const RenderDescriptorSetDesc &setDesc = desc.descriptorSetDescs[set];
            for (uint32_t i = 0; i < setDesc.descriptorRangesCount; i++) {
                const RenderDescriptorRange &range = setDesc.descriptorRanges[i];
                if (range.immutableSampler == nullptr) {
                    continue;
                }

                for (uint32_t e = 0; e < range.count; e++) {
                    immutableSamplers[samplerKey(set, range.binding, e)] = static_cast<const D3D11Sampler *>(range.immutableSampler[e])->d3d;
                }
            }
        }
    }

    ID3D11SamplerState *D3D11PipelineLayout::immutableSampler(uint32_t set, uint32_t binding, uint32_t element) const {
        auto it = immutableSamplers.find(samplerKey(set, binding, element));
        return (it != immutableSamplers.end()) ? it->second.Get() : nullptr;
    }

    // D3D11Framebuffer

    D3D11Framebuffer::D3D11Framebuffer(D3D11Device *device, const RenderFramebufferDesc &desc) {
        for (uint32_t i = 0; i < desc.colorAttachmentsCount; i++) {
            const D3D11Texture *texture = static_cast<const D3D11Texture *>(desc.colorAttachments[i]);
            const D3D11TextureView *view = (desc.colorAttachmentViews != nullptr) ? static_cast<const D3D11TextureView *>(desc.colorAttachmentViews[i]) : nullptr;
            const RenderFormat format = ((view != nullptr) && (view->desc.format != RenderFormat::UNKNOWN)) ? view->desc.format : texture->desc.format;
            D3D11_RENDER_TARGET_VIEW_DESC rtvDesc = {};
            rtvDesc.Format = toDXGI(format);
            if (texture->desc.multisampling.sampleCount > 1) {
                rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DMS;
            }
            else {
                rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
                rtvDesc.Texture2D.MipSlice = (view != nullptr) ? view->desc.mipSlice : 0;
            }

            D3D11Ptr<ID3D11RenderTargetView> rtv;
            device->d3d->CreateRenderTargetView(texture->d3d.Get(), &rtvDesc, rtv.GetAddressOf());
            colorViews.push_back(rtv);
            width = texture->desc.width;
            height = texture->desc.height;
        }

        if (desc.depthAttachment != nullptr) {
            const D3D11Texture *texture = static_cast<const D3D11Texture *>(desc.depthAttachment);
            D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
            dsvDesc.Format = toDXGI(texture->desc.format);
            dsvDesc.ViewDimension = (texture->desc.multisampling.sampleCount > 1) ? D3D11_DSV_DIMENSION_TEXTURE2DMS : D3D11_DSV_DIMENSION_TEXTURE2D;
            // Read-only views need feature level 11; below it the pipeline's depth state still keeps the depth unwritten, but the depth can't also be sampled.
            if (desc.depthAttachmentReadOnly && (device->featureLevel >= D3D_FEATURE_LEVEL_11_0)) {
                dsvDesc.Flags = D3D11_DSV_READ_ONLY_DEPTH | (RenderFormatIsStencil(texture->desc.format) ? D3D11_DSV_READ_ONLY_STENCIL : 0);
            }

            device->d3d->CreateDepthStencilView(texture->d3d.Get(), &dsvDesc, depthView.GetAddressOf());
            width = texture->desc.width;
            height = texture->desc.height;
        }
    }

    uint32_t D3D11Framebuffer::getWidth() const {
        return width;
    }

    uint32_t D3D11Framebuffer::getHeight() const {
        return height;
    }

    // D3D11Context

    void D3D11Context::reset() {
        graphics.layout = nullptr;
        compute.layout = nullptr;
        std::fill(std::begin(graphics.sets), std::end(graphics.sets), nullptr);
        std::fill(std::begin(compute.sets), std::end(compute.sets), nullptr);
        graphics.bindingsDirty = true;
        compute.bindingsDirty = true;
        graphicsPipeline = nullptr;
        computePipeline = nullptr;
        framebuffer = nullptr;
        graphicsPipelineDirty = true;
        computePipelineDirty = true;
        framebufferDirty = true;
        std::fill(std::begin(vertexBuffers), std::end(vertexBuffers), nullptr);
        indexBuffer = nullptr;
        viewports.clear();
        scissors.clear();
        ctx->ClearState();
    }

    // D3D11 nulls an SRV that's still bound as an output, so outputs are dropped at every barrier and rebound lazily.
    void D3D11Context::unbindOutputs() {
        ctx->OMSetRenderTargets(0, nullptr, nullptr);
        ID3D11UnorderedAccessView *nullUAVs[D3D11_1_UAV_SLOT_COUNT] = {};
        ctx->CSSetUnorderedAccessViews(0, (device->featureLevel >= D3D_FEATURE_LEVEL_11_1) ? D3D11_1_UAV_SLOT_COUNT : D3D11_PS_CS_UAV_REGISTER_COUNT, nullUAVs, nullptr);
        ID3D11ShaderResourceView *nullSRVs[D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT] = {};
        ctx->VSSetShaderResources(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, nullSRVs);
        ctx->GSSetShaderResources(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, nullSRVs);
        ctx->PSSetShaderResources(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, nullSRVs);
        ctx->CSSetShaderResources(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, nullSRVs);
        framebufferDirty = true;
        graphics.bindingsDirty = true;
        compute.bindingsDirty = true;
    }

    void D3D11Context::updatePushConstants(D3D11BindPoint &bindPoint) {
        if (!bindPoint.pushDirty || bindPoint.pushConstants.empty()) {
            return;
        }

        const UINT size = UINT(roundUp(bindPoint.pushConstants.size(), 16));
        if (bindPoint.pushBufferSize < size) {
            D3D11_BUFFER_DESC bufferDesc = {};
            bufferDesc.ByteWidth = size;
            bufferDesc.Usage = D3D11_USAGE_DYNAMIC;
            bufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            bindPoint.pushBuffer.Reset();
            device->d3d->CreateBuffer(&bufferDesc, nullptr, bindPoint.pushBuffer.GetAddressOf());
            bindPoint.pushBufferSize = size;
        }

        D3D11_MAPPED_SUBRESOURCE mapped;
        if (SUCCEEDED(ctx->Map(bindPoint.pushBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
            memcpy(mapped.pData, bindPoint.pushConstants.data(), bindPoint.pushConstants.size());
            ctx->Unmap(bindPoint.pushBuffer.Get(), 0);
        }

        bindPoint.pushDirty = false;
    }

    void D3D11Context::applyBindings(D3D11BindPoint &bindPoint, const D3D11StageProgram &program, uint32_t stage, ID3D11UnorderedAccessView **psUAVs, uint32_t *psUAVCount) {
        ID3D11ShaderResourceView *srvs[D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT] = {};
        ID3D11UnorderedAccessView *uavs[D3D11_1_UAV_SLOT_COUNT] = {};
        ID3D11Buffer *cbs[D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT] = {};
        ID3D11SamplerState *samplers[D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT] = {};
        uint32_t srvCount = 0, uavCount = 0, cbCount = 0, samplerCount = 0;
        for (const D3D11ShaderBinding &binding : program.bindings) {
            for (uint32_t e = 0; e < binding.count; e++) {
                const uint32_t reg = binding.reg + e;
                if (binding.kind == D3D11BindingKind::PUSH_CONSTANTS) {
                    cbs[reg] = bindPoint.pushBuffer.Get();
                    cbCount = std::max(cbCount, reg + 1);
                    continue;
                }

                const D3D11DescriptorSet *set = (binding.set < MaxDescriptorSets) ? bindPoint.sets[binding.set] : nullptr;
                const D3D11Descriptor *descriptor = (set != nullptr) ? set->find(binding.binding, e) : nullptr;
                if ((descriptor != nullptr) && (descriptor->buffer != nullptr)) {
                    descriptor->buffer->sync(*this);
                }

                switch (binding.kind) {
                case D3D11BindingKind::SRV:
                    // RT64's texture slots bind one view to both a 2D and a 1D register; the mismatched one stays empty.
                    if ((descriptor != nullptr) && ((binding.viewDimension == 0) || (descriptor->srvDimension == 0) || (descriptor->srvDimension == binding.viewDimension))) {
                        srvs[reg] = descriptor->srv.Get();
                    }
                    else {
                        srvs[reg] = nullptr;
                    }

                    srvCount = std::max(srvCount, reg + 1);
                    break;
                case D3D11BindingKind::UAV:
                    uavs[reg] = (descriptor != nullptr) ? descriptor->uav.Get() : nullptr;
                    uavCount = std::max(uavCount, reg + 1);
                    break;
                case D3D11BindingKind::CBV:
                    cbs[reg] = ((descriptor != nullptr) && (descriptor->buffer != nullptr)) ? descriptor->buffer->constantBuffer(*this) : nullptr;
                    cbCount = std::max(cbCount, reg + 1);
                    break;
                case D3D11BindingKind::SAMPLER: {
                    ID3D11SamplerState *sampler = (bindPoint.layout != nullptr) ? bindPoint.layout->immutableSampler(binding.set, binding.binding, e) : nullptr;
                    if ((sampler == nullptr) && (descriptor != nullptr)) {
                        sampler = descriptor->sampler.Get();
                    }

                    samplers[reg] = sampler;
                    samplerCount = std::max(samplerCount, reg + 1);
                    break;
                }
                default:
                    break;
                }
            }
        }

        switch (stage) {
        case StageVertex:
            ctx->VSSetShaderResources(0, srvCount, srvs);
            ctx->VSSetConstantBuffers(0, cbCount, cbs);
            ctx->VSSetSamplers(0, samplerCount, samplers);
            break;
        case StageGeometry:
            ctx->GSSetShaderResources(0, srvCount, srvs);
            ctx->GSSetConstantBuffers(0, cbCount, cbs);
            ctx->GSSetSamplers(0, samplerCount, samplers);
            break;
        case StagePixel:
            ctx->PSSetShaderResources(0, srvCount, srvs);
            ctx->PSSetConstantBuffers(0, cbCount, cbs);
            ctx->PSSetSamplers(0, samplerCount, samplers);
            if ((psUAVs != nullptr) && (uavCount > 0)) {
                std::copy(uavs, uavs + uavCount, psUAVs);
                *psUAVCount = uavCount;
            }
            break;
        case StageCompute:
            ctx->CSSetShaderResources(0, srvCount, srvs);
            ctx->CSSetConstantBuffers(0, cbCount, cbs);
            ctx->CSSetSamplers(0, samplerCount, samplers);
            if (uavCount > 0) {
                ctx->CSSetUnorderedAccessViews(0, uavCount, uavs, nullptr);
            }
            break;
        }
    }

    void D3D11Context::flushGraphics() {
        const D3D11GraphicsPipeline *pipeline = graphicsPipeline;
        if (pipeline == nullptr) {
            return;
        }

        if (graphicsPipelineDirty) {
            ctx->IASetInputLayout(pipeline->inputLayout.Get());
            ctx->IASetPrimitiveTopology(pipeline->topology);
            ctx->VSSetShader(static_cast<ID3D11VertexShader *>(pipeline->vertex.shader.Get()), nullptr, 0);
            ctx->GSSetShader(static_cast<ID3D11GeometryShader *>(pipeline->geometry.shader.Get()), nullptr, 0);
            ctx->PSSetShader(static_cast<ID3D11PixelShader *>(pipeline->pixel.shader.Get()), nullptr, 0);
            ctx->RSSetState(pipeline->rasterizerState.Get());
            ctx->OMSetBlendState(pipeline->blendState.Get(), nullptr, 0xFFFFFFFF);
            ctx->OMSetDepthStencilState(pipeline->depthStencilState.Get(), pipeline->stencilRef);
            graphicsPipelineDirty = false;
            graphics.bindingsDirty = true;
        }

        for (const D3D11Buffer *buffer : vertexBuffers) {
            if (buffer != nullptr) {
                buffer->sync(*this);
            }
        }

        if (indexBuffer != nullptr) {
            indexBuffer->sync(*this);
        }

        updatePushConstants(graphics);

        ID3D11UnorderedAccessView *psUAVs[D3D11_1_UAV_SLOT_COUNT] = {};
        uint32_t psUAVCount = 0;
        if (graphics.bindingsDirty) {
            applyBindings(graphics, pipeline->vertex, StageVertex, nullptr, nullptr);
            if (pipeline->geometry.shader != nullptr) {
                applyBindings(graphics, pipeline->geometry, StageGeometry, nullptr, nullptr);
            }

            if (pipeline->pixel.shader != nullptr) {
                applyBindings(graphics, pipeline->pixel, StagePixel, psUAVs, &psUAVCount);
            }

            graphics.bindingsDirty = false;
            framebufferDirty = framebufferDirty || (psUAVCount > 0);
        }

        applyFramebuffer(psUAVs, psUAVCount);
    }

    static D3D11Ptr<ID3DBlob> compileBuiltinShader(const char *source, const char *target) {
        D3D11Ptr<ID3DBlob> blob;
        D3D11Ptr<ID3DBlob> errors;
        if (FAILED(D3DCompile(source, strlen(source), "plume_builtin", nullptr, nullptr, "main", target, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, blob.GetAddressOf(), errors.GetAddressOf()))) {
            fprintf(stderr, "[plume-d3d11] Builtin shader failed: %s\n", (errors != nullptr) ? static_cast<const char *>(errors->GetBufferPointer()) : "");
            return nullptr;
        }

        return blob;
    }

    void D3D11Context::clearDepthRects(bool clearDepth, bool clearStencil, float depthValue, uint32_t stencilValue, const std::vector<D3D11_RECT> &rects) {
        auto &rc = device->rectClear;
        if (!rc.created) {
            rc.created = true;
            const char *vertexSource = "float4 main(uint id : SV_VertexID) : SV_Position { float2 uv = float2((id << 1) & 2, id & 2); return float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0); }";
            const char *pixelSource = "cbuffer C : register(b0) { float depthValue; }; float main() : SV_Depth { return depthValue; }";
            const bool sm5 = (device->featureLevel >= D3D_FEATURE_LEVEL_11_0);
            D3D11Ptr<ID3DBlob> vertexBlob = compileBuiltinShader(vertexSource, sm5 ? "vs_5_0" : "vs_4_0");
            D3D11Ptr<ID3DBlob> pixelBlob = compileBuiltinShader(pixelSource, sm5 ? "ps_5_0" : "ps_4_0");
            if ((vertexBlob == nullptr) || (pixelBlob == nullptr)) {
                return;
            }

            ID3D11Device1 *d3d = device->d3d.Get();
            d3d->CreateVertexShader(vertexBlob->GetBufferPointer(), vertexBlob->GetBufferSize(), nullptr, rc.vertexShader.GetAddressOf());
            d3d->CreatePixelShader(pixelBlob->GetBufferPointer(), pixelBlob->GetBufferSize(), nullptr, rc.pixelShader.GetAddressOf());
            if ((rc.vertexShader == nullptr) || (rc.pixelShader == nullptr)) {
                fprintf(stderr, "[plume-d3d11] Depth rect clear shaders couldn't be created; partial depth clears are skipped.\n");
            }

            D3D11_BUFFER_DESC bufferDesc = { 16, D3D11_USAGE_DEFAULT, D3D11_BIND_CONSTANT_BUFFER, 0, 0, 0 };
            d3d->CreateBuffer(&bufferDesc, nullptr, rc.constants.GetAddressOf());

            D3D11_RASTERIZER_DESC rasterizerDesc = {};
            rasterizerDesc.FillMode = D3D11_FILL_SOLID;
            rasterizerDesc.CullMode = D3D11_CULL_NONE;
            rasterizerDesc.ScissorEnable = TRUE;
            d3d->CreateRasterizerState(&rasterizerDesc, rc.rasterizerState.GetAddressOf());

            D3D11_BLEND_DESC blendDesc = {};
            blendDesc.IndependentBlendEnable = FALSE;
            blendDesc.RenderTarget[0].RenderTargetWriteMask = 0;
            d3d->CreateBlendState(&blendDesc, rc.blendState.GetAddressOf());

            // Index 0: depth, 1: stencil, 2: both.
            for (uint32_t i = 0; i < 3; i++) {
                const bool depth = (i != 1);
                const bool stencil = (i != 0);
                D3D11_DEPTH_STENCIL_DESC depthStencilDesc = {};
                depthStencilDesc.DepthEnable = depth;
                depthStencilDesc.DepthWriteMask = depth ? D3D11_DEPTH_WRITE_MASK_ALL : D3D11_DEPTH_WRITE_MASK_ZERO;
                depthStencilDesc.DepthFunc = D3D11_COMPARISON_ALWAYS;
                depthStencilDesc.StencilEnable = stencil;
                depthStencilDesc.StencilReadMask = 0xFF;
                depthStencilDesc.StencilWriteMask = 0xFF;
                depthStencilDesc.FrontFace = { D3D11_STENCIL_OP_KEEP, D3D11_STENCIL_OP_KEEP, D3D11_STENCIL_OP_REPLACE, D3D11_COMPARISON_ALWAYS };
                depthStencilDesc.BackFace = depthStencilDesc.FrontFace;
                d3d->CreateDepthStencilState(&depthStencilDesc, rc.depthStencilStates[i].GetAddressOf());
            }
        }

        if ((rc.pixelShader == nullptr) || !(clearDepth || clearStencil)) {
            return;
        }

        applyFramebuffer();
        const float constants[4] = { depthValue, 0.0f, 0.0f, 0.0f };
        ctx->UpdateSubresource(rc.constants.Get(), 0, nullptr, constants, 0, 0);
        ctx->IASetInputLayout(nullptr);
        ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ctx->VSSetShader(rc.vertexShader.Get(), nullptr, 0);
        ctx->GSSetShader(nullptr, nullptr, 0);
        ctx->PSSetShader(rc.pixelShader.Get(), nullptr, 0);
        ctx->PSSetConstantBuffers(0, 1, rc.constants.GetAddressOf());
        ctx->RSSetState(rc.rasterizerState.Get());
        ctx->OMSetBlendState(rc.blendState.Get(), nullptr, 0xFFFFFFFF);
        ctx->OMSetDepthStencilState(rc.depthStencilStates[clearDepth ? (clearStencil ? 2 : 0) : 1].Get(), stencilValue);

        const D3D11_VIEWPORT viewport = { 0.0f, 0.0f, float(framebuffer->width), float(framebuffer->height), 0.0f, 1.0f };
        ctx->RSSetViewports(1, &viewport);
        for (const D3D11_RECT &rect : rects) {
            ctx->RSSetScissorRects(1, &rect);
            ctx->Draw(3, 0);
        }

        ctx->RSSetViewports(UINT(viewports.size()), viewports.data());
        ctx->RSSetScissorRects(UINT(scissors.size()), scissors.data());
        graphicsPipelineDirty = true;
        graphics.bindingsDirty = true;
    }

    void D3D11Context::applyFramebuffer(ID3D11UnorderedAccessView **psUAVs, uint32_t psUAVCount) {
        if (!framebufferDirty) {
            return;
        }

        ID3D11RenderTargetView *rtvs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
        uint32_t rtvCount = 0;
        ID3D11DepthStencilView *dsv = nullptr;
        if (framebuffer != nullptr) {
            rtvCount = uint32_t(framebuffer->colorViews.size());
            for (uint32_t i = 0; i < rtvCount; i++) {
                rtvs[i] = framebuffer->colorViews[i].Get();
            }

            dsv = framebuffer->depthView.Get();
        }

        if (psUAVCount > rtvCount) {
            ctx->OMSetRenderTargetsAndUnorderedAccessViews(rtvCount, rtvs, dsv, rtvCount, psUAVCount - rtvCount, psUAVs + rtvCount, nullptr);
        }
        else {
            ctx->OMSetRenderTargets(rtvCount, rtvs, dsv);
        }

        framebufferDirty = false;
    }

    void D3D11Context::flushCompute() {
        if (computePipeline == nullptr) {
            return;
        }

        if (computePipelineDirty) {
            ctx->CSSetShader(static_cast<ID3D11ComputeShader *>(computePipeline->compute.shader.Get()), nullptr, 0);
            computePipelineDirty = false;
            compute.bindingsDirty = true;
        }

        updatePushConstants(compute);
        if (compute.bindingsDirty) {
            applyBindings(compute, computePipeline->compute, StageCompute, nullptr, nullptr);
            compute.bindingsDirty = false;
        }
    }

    // D3D11CommandList

    D3D11CommandList::D3D11CommandList(D3D11Device *device) {
        this->device = device;
    }

    // Small sets are copied when bound, like Vulkan's rule that a bound set isn't updated before it executes; large ones (texture caches) stay live.
    D3D11DescriptorSet *D3D11CommandList::captureSet(D3D11DescriptorSet *set) {
        if ((set == nullptr) || (set->descriptors.size() > 16)) {
            return set;
        }

        capturedSets.emplace_back(std::make_unique<D3D11DescriptorSet>(*set));
        return capturedSets.back().get();
    }

    void D3D11CommandList::begin() {
        commands.clear();
        capturedSets.clear();
        recordedGraphicsLayout = nullptr;
        recordedComputeLayout = nullptr;
    }

    void D3D11CommandList::end() { }

    void D3D11CommandList::barriers(RenderBarrierStages stages, const RenderBufferBarrier *bufferBarriers, uint32_t bufferBarriersCount, const RenderTextureBarrier *textureBarriers, uint32_t textureBarriersCount) {
        commands.emplace_back([](D3D11Context &c) {
            c.unbindOutputs();
        });
    }

    void D3D11CommandList::dispatch(uint32_t threadGroupCountX, uint32_t threadGroupCountY, uint32_t threadGroupCountZ) {
        commands.emplace_back([=](D3D11Context &c) {
            if (c.computePipeline == nullptr) {
                return;
            }

            c.flushCompute();
            c.ctx->Dispatch(threadGroupCountX, threadGroupCountY, threadGroupCountZ);
            c.graphics.bindingsDirty = true;
        });
    }

    void D3D11CommandList::traceRays(uint32_t width, uint32_t height, uint32_t depth, RenderBufferReference shaderBindingTable, const RenderShaderBindingGroupsInfo &shaderBindingGroupsInfo) {
        assert(false && "Raytracing is not supported in D3D11.");
    }

    void D3D11CommandList::drawInstanced(uint32_t vertexCountPerInstance, uint32_t instanceCount, uint32_t startVertexLocation, uint32_t startInstanceLocation) {
        commands.emplace_back([=](D3D11Context &c) {
            if (c.graphicsPipeline == nullptr) {
                return;
            }

            c.flushGraphics();
            c.ctx->DrawInstanced(vertexCountPerInstance, instanceCount, startVertexLocation, startInstanceLocation);
        });
    }

    void D3D11CommandList::drawIndexedInstanced(uint32_t indexCountPerInstance, uint32_t instanceCount, uint32_t startIndexLocation, int32_t baseVertexLocation, uint32_t startInstanceLocation) {
        commands.emplace_back([=](D3D11Context &c) {
            if (c.graphicsPipeline == nullptr) {
                return;
            }

            c.flushGraphics();
            c.ctx->DrawIndexedInstanced(indexCountPerInstance, instanceCount, startIndexLocation, baseVertexLocation, startInstanceLocation);
        });
    }

    void D3D11CommandList::setPipeline(const RenderPipeline *pipeline) {
        const D3D11Pipeline *interfacePipeline = static_cast<const D3D11Pipeline *>(pipeline);
        commands.emplace_back([interfacePipeline](D3D11Context &c) {
            // A pipeline that failed to create (e.g. compute on 10_x) arrives as null; the passes using it are skipped.
            if (interfacePipeline == nullptr) {
                c.computePipeline = nullptr;
                c.graphicsPipeline = nullptr;
            }
            else if (interfacePipeline->type == D3D11Pipeline::Type::Compute) {
                c.computePipeline = static_cast<const D3D11ComputePipeline *>(interfacePipeline);
                c.computePipelineDirty = true;
            }
            else {
                c.graphicsPipeline = static_cast<const D3D11GraphicsPipeline *>(interfacePipeline);
                c.graphicsPipelineDirty = true;
            }
        });
    }

    static void setLayout(D3D11BindPoint &bindPoint, const D3D11PipelineLayout *layout) {
        bindPoint.layout = layout;
        bindPoint.pushConstants.assign(layout->pushConstantSize, 0);
        bindPoint.pushDirty = true;
        bindPoint.bindingsDirty = true;
    }

    void D3D11CommandList::setComputePipelineLayout(const RenderPipelineLayout *pipelineLayout) {
        const D3D11PipelineLayout *layout = static_cast<const D3D11PipelineLayout *>(pipelineLayout);
        recordedComputeLayout = layout;
        commands.emplace_back([layout](D3D11Context &c) {
            setLayout(c.compute, layout);
        });
    }

    void D3D11CommandList::setPushConstants(bool compute, uint32_t rangeIndex, const void *data, uint32_t offset, uint32_t size) {
        const D3D11PipelineLayout *layout = compute ? recordedComputeLayout : recordedGraphicsLayout;
        assert((layout != nullptr) && (rangeIndex < layout->pushConstantRanges.size()));

        const RenderPushConstantRange &range = layout->pushConstantRanges[rangeIndex];
        const uint32_t bytes = (size > 0) ? size : range.size;
        const uint32_t dst = range.offset + offset;
        const uint8_t *src = static_cast<const uint8_t *>(data);
        commands.emplace_back([compute, dst, copy = std::vector<uint8_t>(src, src + bytes)](D3D11Context &c) {
            D3D11BindPoint &bindPoint = compute ? c.compute : c.graphics;
            if ((dst + copy.size()) <= bindPoint.pushConstants.size()) {
                memcpy(&bindPoint.pushConstants[dst], copy.data(), copy.size());
                bindPoint.pushDirty = true;
            }
        });
    }

    void D3D11CommandList::setComputePushConstants(uint32_t rangeIndex, const void *data, uint32_t offset, uint32_t size) {
        setPushConstants(true, rangeIndex, data, offset, size);
    }

    void D3D11CommandList::setComputeDescriptorSet(RenderDescriptorSet *descriptorSet, uint32_t setIndex) {
        D3D11DescriptorSet *set = captureSet(static_cast<D3D11DescriptorSet *>(descriptorSet));
        commands.emplace_back([set, setIndex](D3D11Context &c) {
            assert(setIndex < MaxDescriptorSets);
            c.compute.sets[setIndex] = set;
            c.compute.bindingsDirty = true;
        });
    }

    void D3D11CommandList::setGraphicsPipelineLayout(const RenderPipelineLayout *pipelineLayout) {
        const D3D11PipelineLayout *layout = static_cast<const D3D11PipelineLayout *>(pipelineLayout);
        recordedGraphicsLayout = layout;
        commands.emplace_back([layout](D3D11Context &c) {
            setLayout(c.graphics, layout);
        });
    }

    void D3D11CommandList::setGraphicsPushConstants(uint32_t rangeIndex, const void *data, uint32_t offset, uint32_t size) {
        setPushConstants(false, rangeIndex, data, offset, size);
    }

    void D3D11CommandList::setGraphicsDescriptorSet(RenderDescriptorSet *descriptorSet, uint32_t setIndex) {
        D3D11DescriptorSet *set = captureSet(static_cast<D3D11DescriptorSet *>(descriptorSet));
        commands.emplace_back([set, setIndex](D3D11Context &c) {
            assert(setIndex < MaxDescriptorSets);
            c.graphics.sets[setIndex] = set;
            c.graphics.bindingsDirty = true;
        });
    }

    void D3D11CommandList::setGraphicsRootDescriptor(RenderBufferReference bufferReference, uint32_t rootDescriptorIndex) {
        assert(false && "Root descriptors are only supported in D3D12.");
    }

    void D3D11CommandList::setRaytracingPipelineLayout(const RenderPipelineLayout *pipelineLayout) {
        assert(false && "Raytracing is not supported in D3D11.");
    }

    void D3D11CommandList::setRaytracingPushConstants(uint32_t rangeIndex, const void *data, uint32_t offset, uint32_t size) {
        assert(false && "Raytracing is not supported in D3D11.");
    }

    void D3D11CommandList::setRaytracingDescriptorSet(RenderDescriptorSet *descriptorSet, uint32_t setIndex) {
        assert(false && "Raytracing is not supported in D3D11.");
    }

    void D3D11CommandList::setIndexBuffer(const RenderIndexBufferView *view) {
        if (view == nullptr) {
            commands.emplace_back([](D3D11Context &c) {
                c.indexBuffer = nullptr;
                c.ctx->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);
            });

            return;
        }

        const D3D11Buffer *buffer = static_cast<const D3D11Buffer *>(view->buffer.ref);
        const UINT offset = UINT(view->buffer.offset);
        const DXGI_FORMAT format = toDXGI(view->format);
        commands.emplace_back([buffer, offset, format](D3D11Context &c) {
            c.indexBuffer = buffer;
            c.ctx->IASetIndexBuffer((buffer != nullptr) ? buffer->d3d.Get() : nullptr, format, offset);
        });
    }

    void D3D11CommandList::setVertexBuffers(uint32_t startSlot, const RenderVertexBufferView *views, uint32_t viewCount, const RenderInputSlot *inputSlots) {
        struct Slot {
            const D3D11Buffer *buffer;
            UINT stride;
            UINT offset;
        };

        std::vector<Slot> slots(viewCount);
        for (uint32_t i = 0; i < viewCount; i++) {
            slots[i] = { static_cast<const D3D11Buffer *>(views[i].buffer.ref), (inputSlots != nullptr) ? inputSlots[i].stride : 0, UINT(views[i].buffer.offset) };
        }

        commands.emplace_back([startSlot, slots = std::move(slots)](D3D11Context &c) {
            ID3D11Buffer *buffers[D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT] = {};
            UINT strides[D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT] = {};
            UINT offsets[D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT] = {};
            for (size_t i = 0; i < slots.size(); i++) {
                buffers[i] = (slots[i].buffer != nullptr) ? slots[i].buffer->d3d.Get() : nullptr;
                strides[i] = slots[i].stride;
                offsets[i] = slots[i].offset;
                c.vertexBuffers[startSlot + i] = slots[i].buffer;
            }

            c.ctx->IASetVertexBuffers(startSlot, UINT(slots.size()), buffers, strides, offsets);
        });
    }

    void D3D11CommandList::setViewports(const RenderViewport *viewports, uint32_t count) {
        std::vector<D3D11_VIEWPORT> d3dViewports(count);
        for (uint32_t i = 0; i < count; i++) {
            d3dViewports[i] = { viewports[i].x, viewports[i].y, viewports[i].width, viewports[i].height, viewports[i].minDepth, viewports[i].maxDepth };
        }

        commands.emplace_back([d3dViewports = std::move(d3dViewports)](D3D11Context &c) {
            c.viewports = d3dViewports;
            c.ctx->RSSetViewports(UINT(d3dViewports.size()), d3dViewports.data());
        });
    }

    void D3D11CommandList::setScissors(const RenderRect *scissorRects, uint32_t count) {
        std::vector<D3D11_RECT> rects(count);
        for (uint32_t i = 0; i < count; i++) {
            rects[i] = { scissorRects[i].left, scissorRects[i].top, scissorRects[i].right, scissorRects[i].bottom };
        }

        commands.emplace_back([rects = std::move(rects)](D3D11Context &c) {
            c.scissors = rects;
            c.ctx->RSSetScissorRects(UINT(rects.size()), rects.data());
        });
    }

    void D3D11CommandList::setFramebuffer(const RenderFramebuffer *framebuffer) {
        const D3D11Framebuffer *interfaceFramebuffer = static_cast<const D3D11Framebuffer *>(framebuffer);
        commands.emplace_back([interfaceFramebuffer](D3D11Context &c) {
            c.framebuffer = interfaceFramebuffer;
            c.framebufferDirty = true;
        });
    }

    void D3D11CommandList::setDepthBias(float depthBias, float depthBiasClamp, float slopeScaledDepthBias) {
        assert(false && "Dynamic depth bias is not supported in D3D11.");
    }

    void D3D11CommandList::clearColor(uint32_t attachmentIndex, RenderColor colorValue, const RenderRect *clearRects, uint32_t clearRectsCount) {
        std::vector<D3D11_RECT> rects(clearRectsCount);
        for (uint32_t i = 0; i < clearRectsCount; i++) {
            rects[i] = { clearRects[i].left, clearRects[i].top, clearRects[i].right, clearRects[i].bottom };
        }

        commands.emplace_back([attachmentIndex, colorValue, rects = std::move(rects)](D3D11Context &c) {
            if ((c.framebuffer == nullptr) || (attachmentIndex >= c.framebuffer->colorViews.size())) {
                return;
            }

            ID3D11RenderTargetView *rtv = c.framebuffer->colorViews[attachmentIndex].Get();
            if (rects.empty()) {
                c.ctx->ClearRenderTargetView(rtv, colorValue.rgba);
            }
            else {
                c.ctx->ClearView(rtv, colorValue.rgba, rects.data(), UINT(rects.size()));
            }
        });
    }

    void D3D11CommandList::clearDepthStencil(bool clearDepth, bool clearStencil, float depthValue, uint32_t stencilValue, const RenderRect *clearRects, uint32_t clearRectsCount) {
        std::vector<D3D11_RECT> rects(clearRectsCount);
        for (uint32_t i = 0; i < clearRectsCount; i++) {
            rects[i] = { clearRects[i].left, clearRects[i].top, clearRects[i].right, clearRects[i].bottom };
        }

        commands.emplace_back([=, rects = std::move(rects)](D3D11Context &c) {
            const D3D11Framebuffer *framebuffer = c.framebuffer;
            if ((framebuffer == nullptr) || (framebuffer->depthView == nullptr)) {
                return;
            }

            bool partial = false;
            for (const D3D11_RECT &rect : rects) {
                partial = partial || (rect.left > 0) || (rect.top > 0) || (rect.right < LONG(framebuffer->width)) || (rect.bottom < LONG(framebuffer->height));
            }

            if (partial) {
                c.clearDepthRects(clearDepth, clearStencil, depthValue, stencilValue, rects);
                return;
            }

            const UINT flags = (clearDepth ? D3D11_CLEAR_DEPTH : 0) | (clearStencil ? D3D11_CLEAR_STENCIL : 0);
            c.ctx->ClearDepthStencilView(framebuffer->depthView.Get(), flags, depthValue, UINT8(stencilValue));
        });
    }

    static void copyBufferBytes(D3D11Context &c, const D3D11Buffer *dst, uint64_t dstOffset, const D3D11Buffer *src, uint64_t srcOffset, uint64_t size) {
        if (src->isUpload()) {
            if (dst->isReadback()) {
                memcpy(&dst->shadow[size_t(dstOffset)], &src->shadow[size_t(srcOffset)], size_t(size));
            }
            else if (dst->d3d != nullptr) {
                D3D11_BOX box = { UINT(dstOffset), 0, 0, UINT(dstOffset + size), 1, 1 };
                c.ctx->UpdateSubresource(dst->d3d.Get(), 0, &box, &src->shadow[size_t(srcOffset)], 0, 0);
            }

            return;
        }

        if (src->d3d == nullptr) {
            return;
        }

        if (dst->isReadback()) {
            D3D11_BUFFER_DESC stagingDesc = {};
            stagingDesc.ByteWidth = UINT(roundUp(size, 16));
            stagingDesc.Usage = D3D11_USAGE_STAGING;
            stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            D3D11Ptr<ID3D11Buffer> staging;
            if (FAILED(c.device->d3d->CreateBuffer(&stagingDesc, nullptr, staging.GetAddressOf()))) {
                return;
            }

            D3D11_BOX box = { UINT(srcOffset), 0, 0, UINT(srcOffset + size), 1, 1 };
            c.ctx->CopySubresourceRegion(staging.Get(), 0, 0, 0, 0, src->d3d.Get(), 0, &box);
            D3D11Buffer::PendingReadback pending;
            pending.staging = staging;
            pending.dstOffset = dstOffset;
            pending.rowBytes = UINT(size);
            pending.rowPitch = UINT(size);
            pending.rows = 1;
            dst->pendingReadbacks.push_back(pending);
            return;
        }

        if (dst->d3d != nullptr) {
            D3D11_BOX box = { UINT(srcOffset), 0, 0, UINT(srcOffset + size), 1, 1 };
            c.ctx->CopySubresourceRegion(dst->d3d.Get(), 0, UINT(dstOffset), 0, 0, src->d3d.Get(), 0, &box);
        }
    }

    void D3D11CommandList::copyBufferRegion(RenderBufferReference dstBuffer, RenderBufferReference srcBuffer, uint64_t size) {
        const D3D11Buffer *dst = static_cast<const D3D11Buffer *>(dstBuffer.ref);
        const D3D11Buffer *src = static_cast<const D3D11Buffer *>(srcBuffer.ref);
        const uint64_t dstOffset = dstBuffer.offset;
        const uint64_t srcOffset = srcBuffer.offset;
        commands.emplace_back([=](D3D11Context &c) {
            copyBufferBytes(c, dst, dstOffset, src, srcOffset, size);
        });
    }

    void D3D11CommandList::copyTextureRegion(const RenderTextureCopyLocation &dstLocation, const RenderTextureCopyLocation &srcLocation, uint32_t dstX, uint32_t dstY, uint32_t dstZ, const RenderBox *srcBox) {
        const RenderTextureCopyLocation dst = dstLocation;
        const RenderTextureCopyLocation src = srcLocation;
        const bool hasBox = (srcBox != nullptr);
        const RenderBox box = hasBox ? *srcBox : RenderBox();
        commands.emplace_back([=](D3D11Context &c) {
            if ((dst.type == RenderTextureCopyType::SUBRESOURCE) && (src.type == RenderTextureCopyType::PLACED_FOOTPRINT)) {
                const D3D11Texture *dstTexture = static_cast<const D3D11Texture *>(dst.texture);
                const D3D11Buffer *srcBuffer = static_cast<const D3D11Buffer *>(src.buffer);
                const uint32_t blockWidth = RenderFormatBlockWidth(src.placedFootprint.format);
                const uint32_t rowPitch = ((src.placedFootprint.rowWidth + blockWidth - 1) / blockWidth) * RenderFormatSize(src.placedFootprint.format);
                const uint32_t rows = (src.placedFootprint.height + blockWidth - 1) / blockWidth;
                if (!srcBuffer->isUpload()) {
                    warnOnce("Buffer to texture copies need an upload buffer source in D3D11.");
                    return;
                }

                D3D11_BOX dstBox = { dstX, dstY, dstZ, dstX + src.placedFootprint.width, dstY + src.placedFootprint.height, dstZ + std::max(src.placedFootprint.depth, 1U) };
                const uint32_t subresource = dstTexture->subresourceIndex(dst.subresource.mipLevel, dst.subresource.arrayIndex);
                c.ctx->UpdateSubresource(dstTexture->d3d.Get(), subresource, &dstBox, &srcBuffer->shadow[size_t(src.placedFootprint.offset)], rowPitch, rowPitch * rows);
            }
            else if ((dst.type == RenderTextureCopyType::PLACED_FOOTPRINT) && (src.type == RenderTextureCopyType::SUBRESOURCE)) {
                const D3D11Buffer *dstBuffer = static_cast<const D3D11Buffer *>(dst.buffer);
                const D3D11Texture *srcTexture = static_cast<const D3D11Texture *>(src.texture);
                const uint32_t blockWidth = RenderFormatBlockWidth(dst.placedFootprint.format);
                const uint32_t rowPitch = ((dst.placedFootprint.rowWidth + blockWidth - 1) / blockWidth) * RenderFormatSize(dst.placedFootprint.format);
                const uint32_t rowBytes = ((dst.placedFootprint.width + blockWidth - 1) / blockWidth) * RenderFormatSize(dst.placedFootprint.format);
                const uint32_t rows = (dst.placedFootprint.height + blockWidth - 1) / blockWidth;
                const uint32_t depth = std::max(dst.placedFootprint.depth, 1U);
                if (!dstBuffer->isReadback()) {
                    warnOnce("Texture to buffer copies need a readback buffer destination in D3D11.");
                    return;
                }

                D3D11Ptr<ID3D11Resource> staging;
                const DXGI_FORMAT format = srcTexture->resourceFormat;
                if (srcTexture->desc.dimension == RenderTextureDimension::TEXTURE_3D) {
                    D3D11_TEXTURE3D_DESC stagingDesc = { dst.placedFootprint.width, dst.placedFootprint.height, depth, 1, format, D3D11_USAGE_STAGING, 0, D3D11_CPU_ACCESS_READ, 0 };
                    ID3D11Texture3D *texture = nullptr;
                    c.device->d3d->CreateTexture3D(&stagingDesc, nullptr, &texture);
                    staging.Attach(texture);
                }
                else if (srcTexture->desc.dimension == RenderTextureDimension::TEXTURE_1D) {
                    D3D11_TEXTURE1D_DESC stagingDesc = { dst.placedFootprint.width, 1, 1, format, D3D11_USAGE_STAGING, 0, D3D11_CPU_ACCESS_READ, 0 };
                    ID3D11Texture1D *texture = nullptr;
                    c.device->d3d->CreateTexture1D(&stagingDesc, nullptr, &texture);
                    staging.Attach(texture);
                }
                else {
                    D3D11_TEXTURE2D_DESC stagingDesc = { dst.placedFootprint.width, dst.placedFootprint.height, 1, 1, format, { 1, 0 }, D3D11_USAGE_STAGING, 0, D3D11_CPU_ACCESS_READ, 0 };
                    ID3D11Texture2D *texture = nullptr;
                    c.device->d3d->CreateTexture2D(&stagingDesc, nullptr, &texture);
                    staging.Attach(texture);
                }

                if (staging == nullptr) {
                    return;
                }

                D3D11_BOX d3dBox = hasBox ? D3D11_BOX{ UINT(box.left), UINT(box.top), UINT(box.front), UINT(box.right), UINT(box.bottom), UINT(box.back) } : D3D11_BOX{ 0, 0, 0, dst.placedFootprint.width, dst.placedFootprint.height, depth };
                const uint32_t subresource = srcTexture->subresourceIndex(src.subresource.mipLevel, src.subresource.arrayIndex);
                c.ctx->CopySubresourceRegion(staging.Get(), 0, 0, 0, 0, srcTexture->d3d.Get(), subresource, &d3dBox);

                D3D11Buffer::PendingReadback pending;
                pending.staging = staging;
                pending.dstOffset = dst.placedFootprint.offset;
                pending.rowBytes = rowBytes;
                pending.rowPitch = rowPitch;
                pending.rows = rows;
                pending.depth = depth;
                dstBuffer->pendingReadbacks.push_back(pending);
            }
            else if ((dst.type == RenderTextureCopyType::SUBRESOURCE) && (src.type == RenderTextureCopyType::SUBRESOURCE)) {
                const D3D11Texture *dstTexture = static_cast<const D3D11Texture *>(dst.texture);
                const D3D11Texture *srcTexture = static_cast<const D3D11Texture *>(src.texture);
                const D3D11_BOX d3dBox = { UINT(box.left), UINT(box.top), UINT(box.front), UINT(box.right), UINT(box.bottom), UINT(box.back) };
                const uint32_t dstSubresource = dstTexture->subresourceIndex(dst.subresource.mipLevel, dst.subresource.arrayIndex);
                const uint32_t srcSubresource = srcTexture->subresourceIndex(src.subresource.mipLevel, src.subresource.arrayIndex);
                c.ctx->CopySubresourceRegion(dstTexture->d3d.Get(), dstSubresource, dstX, dstY, dstZ, srcTexture->d3d.Get(), srcSubresource, hasBox ? &d3dBox : nullptr);
            }
            else {
                warnOnce("Unsupported texture copy combination.");
            }
        });
    }

    void D3D11CommandList::copyBuffer(const RenderBuffer *dstBuffer, const RenderBuffer *srcBuffer) {
        const D3D11Buffer *dst = static_cast<const D3D11Buffer *>(dstBuffer);
        const D3D11Buffer *src = static_cast<const D3D11Buffer *>(srcBuffer);
        commands.emplace_back([=](D3D11Context &c) {
            copyBufferBytes(c, dst, 0, src, 0, std::min(dst->desc.size, src->desc.size));
        });
    }

    void D3D11CommandList::copyTexture(const RenderTexture *dstTexture, const RenderTexture *srcTexture) {
        const D3D11Texture *dst = static_cast<const D3D11Texture *>(dstTexture);
        const D3D11Texture *src = static_cast<const D3D11Texture *>(srcTexture);
        commands.emplace_back([=](D3D11Context &c) {
            c.ctx->CopyResource(dst->d3d.Get(), src->d3d.Get());
        });
    }

    void D3D11CommandList::resolveTexture(const RenderTexture *dstTexture, const RenderTexture *srcTexture) {
        const D3D11Texture *dst = static_cast<const D3D11Texture *>(dstTexture);
        const D3D11Texture *src = static_cast<const D3D11Texture *>(srcTexture);
        commands.emplace_back([=](D3D11Context &c) {
            c.ctx->ResolveSubresource(dst->d3d.Get(), 0, src->d3d.Get(), 0, toDXGI(dst->desc.format));
        });
    }

    void D3D11CommandList::resolveTextureRegion(const RenderTexture *dstTexture, uint32_t dstX, uint32_t dstY, const RenderTexture *srcTexture, const RenderRect *srcRect, RenderResolveMode resolveMode) {
        const D3D11Texture *src = static_cast<const D3D11Texture *>(srcTexture);
        const bool full = (dstX == 0) && (dstY == 0) && ((srcRect == nullptr) || ((srcRect->left == 0) && (srcRect->top == 0) && (uint32_t(srcRect->right) == src->desc.width) && (uint32_t(srcRect->bottom) == src->desc.height)));
        if (!full) {
            warnOnce("Region resolves are not supported in D3D11; resolving the whole texture.");
        }

        resolveTexture(dstTexture, srcTexture);
    }

    void D3D11CommandList::buildBottomLevelAS(const RenderAccelerationStructure *dstAccelerationStructure, RenderBufferReference scratchBuffer, const RenderBottomLevelASBuildInfo &buildInfo) {
        assert(false && "Raytracing is not supported in D3D11.");
    }

    void D3D11CommandList::buildTopLevelAS(const RenderAccelerationStructure *dstAccelerationStructure, RenderBufferReference scratchBuffer, RenderBufferReference instancesBuffer, const RenderTopLevelASBuildInfo &buildInfo) {
        assert(false && "Raytracing is not supported in D3D11.");
    }

    void D3D11CommandList::discardTexture(const RenderTexture *texture) {
        const D3D11Texture *interfaceTexture = static_cast<const D3D11Texture *>(texture);
        commands.emplace_back([interfaceTexture](D3D11Context &c) {
            c.ctx->DiscardResource(interfaceTexture->d3d.Get());
        });
    }

    void D3D11CommandList::resetQueryPool(const RenderQueryPool *queryPool, uint32_t queryFirstIndex, uint32_t queryCount) { }

    void D3D11CommandList::writeTimestamp(const RenderQueryPool *queryPool, uint32_t queryIndex) { }

    // D3D11CommandFence

    D3D11CommandFence::D3D11CommandFence(D3D11Device *device) {
        D3D11_QUERY_DESC queryDesc = { D3D11_QUERY_EVENT, 0 };
        device->d3d->CreateQuery(&queryDesc, query.GetAddressOf());
    }

    // D3D11SwapChain

    D3D11SwapChain::D3D11SwapChain(D3D11Device *device, const RenderSwapChainDesc &desc) {
        this->device = device;
        this->desc = desc;
        nativeFormat = toDXGI(desc.format);
        getWindowSize(width, height);

        swapChainFlags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
        if (device->renderInterface->allowTearing) {
            swapChainFlags |= DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
        }

        DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
        swapChainDesc.BufferCount = std::max(desc.textureCount, 2U);
        swapChainDesc.Width = width;
        swapChainDesc.Height = height;
        swapChainDesc.Format = nativeFormat;
        swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        swapChainDesc.SampleDesc.Count = 1;
        swapChainDesc.Flags = swapChainFlags;

        IDXGIFactory2 *dxgiFactory = device->renderInterface->dxgiFactory.Get();
        HRESULT res = dxgiFactory->CreateSwapChainForHwnd(device->d3d.Get(), desc.renderWindow, &swapChainDesc, nullptr, nullptr, d3d.GetAddressOf());
        if (FAILED(res)) {
            // FLIP_DISCARD needs Windows 10; fall back to the Windows 8 flip model.
            swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
            swapChainFlags &= ~DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
            swapChainDesc.Flags = swapChainFlags;
            res = dxgiFactory->CreateSwapChainForHwnd(device->d3d.Get(), desc.renderWindow, &swapChainDesc, nullptr, nullptr, d3d.GetAddressOf());
        }

        if (FAILED(res)) {
            fprintf(stderr, "[plume-d3d11] CreateSwapChainForHwnd failed with error code 0x%lX.\n", res);
            return;
        }

        dxgiFactory->MakeWindowAssociation(desc.renderWindow, DXGI_MWA_NO_ALT_ENTER);

        D3D11Ptr<IDXGISwapChain2> swapChain2;
        if (SUCCEEDED(d3d.As(&swapChain2))) {
            swapChain2->SetMaximumFrameLatency(std::max(desc.maxFrameLatency, 1U));
            waitableObject = swapChain2->GetFrameLatencyWaitableObject();
        }

        textures.resize(desc.textureCount);
        for (uint32_t i = 0; i < desc.textureCount; i++) {
            textures[i] = std::make_unique<D3D11Texture>();
            textures[i]->device = device;
            textures[i]->desc.dimension = RenderTextureDimension::TEXTURE_2D;
            textures[i]->desc.format = desc.format;
            textures[i]->desc.depth = 1;
            textures[i]->desc.mipLevels = 1;
            textures[i]->desc.arraySize = 1;
            textures[i]->desc.flags = RenderTextureFlag::RENDER_TARGET;
            textures[i]->resourceFormat = nativeFormat;
        }

        setTextures();
    }

    D3D11SwapChain::~D3D11SwapChain() {
        if (waitableObject != nullptr) {
            CloseHandle(waitableObject);
        }
    }

    bool D3D11SwapChain::present(uint32_t textureIndex, RenderCommandSemaphore **waitSemaphores, uint32_t waitSemaphoreCount) {
        const bool tearingAllowed = (swapChainFlags & DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING) != 0U;
        const UINT syncInterval = (vsyncEnabled && !desc.enablePresentWait) ? 1 : 0;
        const UINT flags = (!vsyncEnabled && tearingAllowed) ? DXGI_PRESENT_ALLOW_TEARING : 0;
        std::scoped_lock lock(device->contextMutex);
        return SUCCEEDED(d3d->Present(syncInterval, flags));
    }

    void D3D11SwapChain::wait() {
        if (waitableObject != nullptr) {
            WaitForSingleObject(waitableObject, INFINITE);
        }
    }

    bool D3D11SwapChain::resize() {
        getWindowSize(width, height);
        if ((width == 0) || (height == 0)) {
            return false;
        }

        std::scoped_lock lock(device->contextMutex);
        for (std::unique_ptr<D3D11Texture> &texture : textures) {
            texture->d3d.Reset();
            texture->defaultSRV.Reset();
        }

        device->immediateContext->ClearState();
        device->immediateContext->Flush();
        HRESULT res = d3d->ResizeBuffers(0, 0, 0, DXGI_FORMAT_UNKNOWN, swapChainFlags);
        if (FAILED(res)) {
            // Keep presenting at the old size rather than leaving the textures empty.
            fprintf(stderr, "[plume-d3d11] ResizeBuffers failed with error code 0x%lX.\n", res);
            DXGI_SWAP_CHAIN_DESC1 currentDesc;
            d3d->GetDesc1(&currentDesc);
            width = currentDesc.Width;
            height = currentDesc.Height;
            setTextures();
            return false;
        }

        setTextures();
        return true;
    }

    bool D3D11SwapChain::needsResize() const {
        uint32_t windowWidth, windowHeight;
        getWindowSize(windowWidth, windowHeight);
        return (d3d == nullptr) || (windowWidth != width) || (windowHeight != height);
    }

    void D3D11SwapChain::setVsyncEnabled(bool vsyncEnabled) {
        this->vsyncEnabled = vsyncEnabled;
    }

    bool D3D11SwapChain::isVsyncEnabled() const {
        return vsyncEnabled;
    }

    uint32_t D3D11SwapChain::getWidth() const {
        return width;
    }

    uint32_t D3D11SwapChain::getHeight() const {
        return height;
    }

    void D3D11SwapChain::getWindowSize(uint32_t &dstWidth, uint32_t &dstHeight) const {
        RECT rect;
        GetClientRect(desc.renderWindow, &rect);
        dstWidth = rect.right - rect.left;
        dstHeight = rect.bottom - rect.top;
    }

    // D3D11 only exposes buffer 0, which always names the current back buffer, so every index aliases it.
    void D3D11SwapChain::setTextures() {
        D3D11Ptr<ID3D11Texture2D> backBuffer;
        d3d->GetBuffer(0, IID_PPV_ARGS(backBuffer.GetAddressOf()));
        for (std::unique_ptr<D3D11Texture> &texture : textures) {
            texture->d3d = backBuffer;
            texture->desc.width = width;
            texture->desc.height = height;
        }
    }

    RenderTexture *D3D11SwapChain::getTexture(uint32_t textureIndex) {
        return textures[textureIndex].get();
    }

    uint32_t D3D11SwapChain::getTextureCount() const {
        return uint32_t(textures.size());
    }

    bool D3D11SwapChain::acquireTexture(RenderCommandSemaphore *signalSemaphore, uint32_t *textureIndex) {
        assert(textureIndex != nullptr);
        *textureIndex = nextTexture;
        nextTexture = (nextTexture + 1) % std::max(uint32_t(textures.size()), 1U);
        return true;
    }

    RenderWindow D3D11SwapChain::getWindow() const {
        return desc.renderWindow;
    }

    bool D3D11SwapChain::isEmpty() const {
        return (d3d == nullptr) || (width == 0) || (height == 0);
    }

    uint32_t D3D11SwapChain::getRefreshRate() const {
        return 0;
    }

    // D3D11CommandQueue

    D3D11CommandQueue::D3D11CommandQueue(D3D11Device *device) {
        this->device = device;
    }

    std::unique_ptr<RenderCommandList> D3D11CommandQueue::createCommandList() {
        return std::make_unique<D3D11CommandList>(device);
    }

    std::unique_ptr<RenderSwapChain> D3D11CommandQueue::createSwapChain(const RenderSwapChainDesc &desc) {
        return std::make_unique<D3D11SwapChain>(device, desc);
    }

    // There is one immediate context, so submissions serialize and semaphores are implied.
    void D3D11CommandQueue::executeCommandLists(const RenderCommandList **commandLists, uint32_t commandListCount, RenderCommandSemaphore **waitSemaphores, uint32_t waitSemaphoreCount, RenderCommandSemaphore **signalSemaphores, uint32_t signalSemaphoreCount, RenderCommandFence *signalFence) {
        std::scoped_lock lock(device->contextMutex);
        D3D11Context &context = device->context;
        context.submission++;
        for (uint32_t i = 0; i < commandListCount; i++) {
            const D3D11CommandList *commandList = static_cast<const D3D11CommandList *>(commandLists[i]);
            context.reset();
            for (const D3D11Command &command : commandList->commands) {
                command(context);
            }
        }

        context.reset();
        if (signalFence != nullptr) {
            D3D11CommandFence *fence = static_cast<D3D11CommandFence *>(signalFence);
            context.ctx->End(fence->query.Get());
            fence->pending = true;
        }

        context.ctx->Flush();
        device->printDebugMessages();
    }

    void D3D11CommandQueue::waitForCommandFence(RenderCommandFence *fence) {
        D3D11CommandFence *interfaceFence = static_cast<D3D11CommandFence *>(fence);
        while (interfaceFence->pending) {
            {
                std::scoped_lock lock(device->contextMutex);
                if (device->immediateContext->GetData(interfaceFence->query.Get(), nullptr, 0, 0) == S_OK) {
                    interfaceFence->pending = false;
                    break;
                }
            }

            std::this_thread::yield();
        }
    }

    // D3D11Pool

    D3D11Pool::D3D11Pool(D3D11Device *device) {
        this->device = device;
    }

    std::unique_ptr<RenderBuffer> D3D11Pool::createBuffer(const RenderBufferDesc &desc) {
        return device->createBuffer(desc);
    }

    std::unique_ptr<RenderTexture> D3D11Pool::createTexture(const RenderTextureDesc &desc) {
        return device->createTexture(desc);
    }

    // D3D11QueryPool

    D3D11QueryPool::D3D11QueryPool(uint32_t queryCount) {
        results.resize(queryCount, 0);
    }

    void D3D11QueryPool::queryResults() { }

    const uint64_t *D3D11QueryPool::getResults() const {
        return results.data();
    }

    uint32_t D3D11QueryPool::getCount() const {
        return uint32_t(results.size());
    }

    // D3D11Device

    D3D11Device::D3D11Device(D3D11Interface *renderInterface, const std::string &preferredDeviceName) {
        this->renderInterface = renderInterface;

        D3D11Ptr<IDXGIAdapter1> adapterOption;
        for (UINT adapterIndex = 0; renderInterface->dxgiFactory->EnumAdapters1(adapterIndex, adapterOption.ReleaseAndGetAddressOf()) != DXGI_ERROR_NOT_FOUND; adapterIndex++) {
            DXGI_ADAPTER_DESC1 adapterDesc;
            adapterOption->GetDesc1(&adapterDesc);
            if (adapterDesc.Flags & (DXGI_ADAPTER_FLAG_REMOTE | DXGI_ADAPTER_FLAG_SOFTWARE)) {
                continue;
            }

            const std::string deviceName = Utf16ToUtf8(adapterDesc.Description);
            const bool preferUserChoice = (preferredDeviceName == deviceName);
            const bool preferOption = (adapter == nullptr) || (adapterDesc.DedicatedVideoMemory > description.dedicatedVideoMemory) || preferUserChoice;
            if (!preferOption) {
                continue;
            }

            // D3D10-class GPUs get a 10_x device (no compute). PLUME_D3D11_FEATURE_LEVEL=10_0|10_1 caps the device, so that path can be developed on any GPU.
            std::vector<D3D_FEATURE_LEVEL> featureLevels = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0 };
            if (const char *levelCap = std::getenv("PLUME_D3D11_FEATURE_LEVEL")) {
                featureLevels = { (strcmp(levelCap, "10_1") == 0) ? D3D_FEATURE_LEVEL_10_1 : D3D_FEATURE_LEVEL_10_0 };
            }

            UINT flags = 0;
            if (std::getenv("PLUME_D3D11_DEBUG") != nullptr) {
                flags |= D3D11_CREATE_DEVICE_DEBUG;
            }

            D3D11Ptr<ID3D11Device> deviceOption;
            D3D11Ptr<ID3D11DeviceContext> contextOption;
            D3D_FEATURE_LEVEL levelOption;
            HRESULT res = D3D11CreateDevice(adapterOption.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, flags, featureLevels.data(), UINT(featureLevels.size()), D3D11_SDK_VERSION, deviceOption.GetAddressOf(), &levelOption, contextOption.GetAddressOf());
            if ((res == E_INVALIDARG) && (featureLevels.size() > 1)) {
                res = D3D11CreateDevice(adapterOption.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, flags, featureLevels.data() + 1, UINT(featureLevels.size() - 1), D3D11_SDK_VERSION, deviceOption.GetAddressOf(), &levelOption, contextOption.GetAddressOf());
            }

            D3D11Ptr<ID3D11Device1> device1;
            D3D11Ptr<ID3D11DeviceContext1> context1;
            if (FAILED(res) || FAILED(deviceOption.As(&device1)) || FAILED(contextOption.As(&context1))) {
                continue;
            }

            adapter = adapterOption;
            d3d = device1;
            immediateContext = context1;
            featureLevel = levelOption;
            description.name = deviceName;
            description.dedicatedVideoMemory = adapterDesc.DedicatedVideoMemory;
            description.vendor = RenderDeviceVendor(adapterDesc.VendorId);
            description.type = RenderDeviceType::DISCRETE;

            LARGE_INTEGER adapterVersion = {};
            if (SUCCEEDED(adapter->CheckInterfaceSupport(__uuidof(IDXGIDevice), &adapterVersion))) {
                description.driverVersion = adapterVersion.QuadPart;
            }

            if (preferUserChoice) {
                break;
            }
        }

        // No hardware adapter (VMs, CI runners): fall back to the WARP software rasterizer.
        if (d3d == nullptr) {
            const D3D_FEATURE_LEVEL warpLevel = D3D_FEATURE_LEVEL_11_0;
            D3D11Ptr<ID3D11Device> warpDevice;
            D3D11Ptr<ID3D11DeviceContext> warpContext;
            if (SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, &warpLevel, 1, D3D11_SDK_VERSION, warpDevice.GetAddressOf(), &featureLevel, warpContext.GetAddressOf()))) {
                warpDevice.As(&d3d);
                warpContext.As(&immediateContext);
                description.name = "Microsoft Basic Render Driver (WARP)";
                description.type = RenderDeviceType::CPU;
            }
        }

        if (d3d == nullptr) {
            fprintf(stderr, "[plume-d3d11] Unable to create a D3D11 device with feature level 11_0.\n");
            return;
        }

        d3d.As(&infoQueue);
        capabilities.computeShaders = (featureLevel >= D3D_FEATURE_LEVEL_11_0);
        capabilities.geometryShader = true;
        capabilities.samplerMirrorClampToEdge = true;
        capabilities.maxTextureSize = D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION;
        context.device = this;
        context.ctx = immediateContext.Get();

        const char *cacheDirectory = std::getenv("PLUME_D3D11_SHADER_CACHE");
        shaderCacheDirectory = (cacheDirectory != nullptr) ? cacheDirectory : "d3d11_shader_cache";
        CreateDirectoryA(shaderCacheDirectory.c_str(), nullptr);
    }

    D3D11Device::~D3D11Device() {
        if (immediateContext != nullptr) {
            immediateContext->ClearState();
            immediateContext->Flush();
        }
    }

    std::unique_ptr<RenderDescriptorSet> D3D11Device::createDescriptorSet(const RenderDescriptorSetDesc &desc) {
        return std::make_unique<D3D11DescriptorSet>(this, desc);
    }

    std::unique_ptr<RenderShader> D3D11Device::createShader(const void *data, uint64_t size, const char *entryPointName, RenderShaderFormat format) {
        return std::make_unique<D3D11Shader>(data, size, entryPointName, format);
    }

    std::unique_ptr<RenderSampler> D3D11Device::createSampler(const RenderSamplerDesc &desc) {
        return std::make_unique<D3D11Sampler>(this, desc);
    }

    std::unique_ptr<RenderPipeline> D3D11Device::createComputePipeline(const RenderComputePipelineDesc &desc) {
        std::unique_ptr<D3D11ComputePipeline> pipeline = std::make_unique<D3D11ComputePipeline>(this, desc);
        if (!pipeline->valid) {
            fprintf(stderr, "[plume-d3d11] Compute pipeline creation failed.\n");
            return nullptr;
        }

        return pipeline;
    }

    std::unique_ptr<RenderPipeline> D3D11Device::createGraphicsPipeline(const RenderGraphicsPipelineDesc &desc) {
        std::unique_ptr<D3D11GraphicsPipeline> pipeline = std::make_unique<D3D11GraphicsPipeline>(this, desc);
        if (!pipeline->valid) {
            fprintf(stderr, "[plume-d3d11] Graphics pipeline creation failed.\n");
            return nullptr;
        }

        return pipeline;
    }

    std::unique_ptr<RenderPipeline> D3D11Device::createRaytracingPipeline(const RenderRaytracingPipelineDesc &desc, const RenderPipeline *previousPipeline) {
        return nullptr;
    }

    std::unique_ptr<RenderCommandQueue> D3D11Device::createCommandQueue(RenderCommandListType type) {
        return std::make_unique<D3D11CommandQueue>(this);
    }

    std::unique_ptr<RenderBuffer> D3D11Device::createBuffer(const RenderBufferDesc &desc) {
        return std::make_unique<D3D11Buffer>(this, desc);
    }

    std::unique_ptr<RenderTexture> D3D11Device::createTexture(const RenderTextureDesc &desc) {
        return std::make_unique<D3D11Texture>(this, desc);
    }

    std::unique_ptr<RenderAccelerationStructure> D3D11Device::createAccelerationStructure(const RenderAccelerationStructureDesc &desc) {
        return nullptr;
    }

    std::unique_ptr<RenderPool> D3D11Device::createPool(const RenderPoolDesc &desc) {
        return std::make_unique<D3D11Pool>(this);
    }

    std::unique_ptr<RenderPipelineLayout> D3D11Device::createPipelineLayout(const RenderPipelineLayoutDesc &desc) {
        return std::make_unique<D3D11PipelineLayout>(this, desc);
    }

    std::unique_ptr<RenderCommandFence> D3D11Device::createCommandFence() {
        return std::make_unique<D3D11CommandFence>(this);
    }

    std::unique_ptr<RenderCommandSemaphore> D3D11Device::createCommandSemaphore() {
        return std::make_unique<D3D11CommandSemaphore>();
    }

    std::unique_ptr<RenderFramebuffer> D3D11Device::createFramebuffer(const RenderFramebufferDesc &desc) {
        return std::make_unique<D3D11Framebuffer>(this, desc);
    }

    std::unique_ptr<RenderQueryPool> D3D11Device::createQueryPool(uint32_t queryCount) {
        return std::make_unique<D3D11QueryPool>(queryCount);
    }

    void D3D11Device::setBottomLevelASBuildInfo(RenderBottomLevelASBuildInfo &buildInfo, const RenderBottomLevelASMesh *meshes, uint32_t meshCount, bool preferFastBuild, bool preferFastTrace) {
        assert(false && "Raytracing is not supported in D3D11.");
    }

    void D3D11Device::setTopLevelASBuildInfo(RenderTopLevelASBuildInfo &buildInfo, const RenderTopLevelASInstance *instances, uint32_t instanceCount, bool preferFastBuild, bool preferFastTrace) {
        assert(false && "Raytracing is not supported in D3D11.");
    }

    void D3D11Device::setShaderBindingTableInfo(RenderShaderBindingTableInfo &tableInfo, const RenderShaderBindingGroups &groups, const RenderPipeline *pipeline, RenderDescriptorSet **descriptorSets, uint32_t descriptorSetCount) {
        assert(false && "Raytracing is not supported in D3D11.");
    }

    const RenderDeviceCapabilities &D3D11Device::getCapabilities() const {
        return capabilities;
    }

    const RenderDeviceDescription &D3D11Device::getDescription() const {
        return description;
    }

    RenderSampleCounts D3D11Device::getSampleCountsSupported(RenderFormat format) const {
        RenderSampleCounts counts = RenderSampleCount::COUNT_1;
        const DXGI_FORMAT dxgiFormat = toDXGI(format);
        for (UINT count = 2; count <= 8; count *= 2) {
            UINT quality = 0;
            if (SUCCEEDED(d3d->CheckMultisampleQualityLevels(dxgiFormat, count, &quality)) && (quality > 0)) {
                counts |= count;
            }
        }

        return counts;
    }

    bool D3D11Device::isLost() const {
        return (d3d != nullptr) && (d3d->GetDeviceRemovedReason() != S_OK);
    }

    // The debug layer only reports through the debugger, so PLUME_D3D11_DEBUG copies its messages to stderr.
    void D3D11Device::printDebugMessages() {
        if (infoQueue == nullptr) {
            return;
        }

        const UINT64 count = infoQueue->GetNumStoredMessages();
        for (UINT64 i = 0; i < count; i++) {
            SIZE_T length = 0;
            infoQueue->GetMessage(i, nullptr, &length);
            std::vector<uint8_t> storage(length);
            D3D11_MESSAGE *message = reinterpret_cast<D3D11_MESSAGE *>(storage.data());
            if (SUCCEEDED(infoQueue->GetMessage(i, message, &length)) && (message->Severity <= D3D11_MESSAGE_SEVERITY_WARNING)) {
                fprintf(stderr, "[plume-d3d11] debug: %.*s\n", int(message->DescriptionByteLength), message->pDescription);
            }
        }

        infoQueue->ClearStoredMessages();
    }

    bool D3D11Device::beginCapture() {
        return false;
    }

    bool D3D11Device::endCapture() {
        return false;
    }

    // Shader translation: SPIR-V -> SPIRV-Cross HLSL (SM 5.0, backend-assigned registers) -> D3DCompile, cached on disk.

    static const uint32_t ShaderCacheVersion = 5;

    static bool readCache(const std::string &path, D3D11StageProgram &program, uint32_t &stage) {
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) {
            return false;
        }

        uint32_t header[4] = {};
        file.read(reinterpret_cast<char *>(header), sizeof(header));
        if (!file || (header[0] != ShaderCacheVersion)) {
            return false;
        }

        stage = header[1];
        program.bindings.resize(header[2]);
        program.bytecode.resize(header[3]);
        file.read(reinterpret_cast<char *>(program.bindings.data()), program.bindings.size() * sizeof(D3D11ShaderBinding));
        file.read(reinterpret_cast<char *>(program.bytecode.data()), program.bytecode.size());
        return bool(file);
    }

    static void writeCache(const std::string &path, const D3D11StageProgram &program, uint32_t stage) {
        const std::string tempPath = path + ".tmp" + std::to_string(GetCurrentThreadId());
        {
            std::ofstream file(tempPath, std::ios::binary);
            const uint32_t header[4] = { ShaderCacheVersion, stage, uint32_t(program.bindings.size()), uint32_t(program.bytecode.size()) };
            file.write(reinterpret_cast<const char *>(header), sizeof(header));
            file.write(reinterpret_cast<const char *>(program.bindings.data()), program.bindings.size() * sizeof(D3D11ShaderBinding));
            file.write(reinterpret_cast<const char *>(program.bytecode.data()), program.bytecode.size());
        }

        MoveFileExA(tempPath.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING);
    }

    // D3D11 links stages by register order, so SV_Position goes first and a PS reading fewer TEXCOORDs still lines up with the VS.
    static void movePositionFirst(std::string &hlsl, const char *structName) {
        const size_t structStart = hlsl.find(structName);
        if (structStart == std::string::npos) {
            return;
        }

        const size_t bodyStart = hlsl.find("{\n", structStart);
        const size_t bodyEnd = hlsl.find("};", structStart);
        const size_t semantic = hlsl.find(": SV_Position;", structStart);
        if (bodyStart == std::string::npos) {
            return;
        }

        if ((semantic == std::string::npos) || (semantic > bodyEnd)) {
            hlsl.insert(bodyStart + 2, "    float4 plume_position : SV_Position;\n");
            return;
        }

        const size_t lineStart = hlsl.rfind('\n', semantic) + 1;
        const size_t lineEnd = hlsl.find('\n', semantic) + 1;
        const std::string line = hlsl.substr(lineStart, lineEnd - lineStart);
        hlsl.erase(lineStart, lineEnd - lineStart);
        hlsl.insert(bodyStart + 2, line);
    }

    static uint32_t imageViewDimension(const spirv_cross::SPIRType::ImageType &image) {
        switch (image.dim) {
        case spv::Dim1D: return image.arrayed ? D3D11_SRV_DIMENSION_TEXTURE1DARRAY : D3D11_SRV_DIMENSION_TEXTURE1D;
        case spv::Dim2D:
            if (image.ms) {
                return image.arrayed ? D3D11_SRV_DIMENSION_TEXTURE2DMSARRAY : D3D11_SRV_DIMENSION_TEXTURE2DMS;
            }

            return image.arrayed ? D3D11_SRV_DIMENSION_TEXTURE2DARRAY : D3D11_SRV_DIMENSION_TEXTURE2D;
        case spv::Dim3D: return D3D11_SRV_DIMENSION_TEXTURE3D;
        case spv::DimCube: return image.arrayed ? D3D11_SRV_DIMENSION_TEXTURECUBEARRAY : D3D11_SRV_DIMENSION_TEXTURECUBE;
        case spv::DimBuffer: return D3D11_SRV_DIMENSION_BUFFER;
        default: return 0;
        }
    }

    static bool isIdentifierChar(char c) {
        return ((c >= 'a') && (c <= 'z')) || ((c >= 'A') && (c <= 'Z')) || ((c >= '0') && (c <= '9')) || (c == '_');
    }

    static void replaceCalls(std::string &hlsl, const std::string &from, const std::string &to) {
        for (size_t pos = hlsl.find(from); pos != std::string::npos; pos = hlsl.find(from, pos)) {
            if ((pos > 0) && isIdentifierChar(hlsl[pos - 1])) {
                pos += from.size();
                continue;
            }

            hlsl.replace(pos, from.size(), to);
            pos += to.size();
        }
    }

    // SM 4 graphics shaders can't read raw buffers, so read-only storage buffers become Buffer<uint> (bound as R32_UINT views) with word-indexed loads.
    static void useTypedBufferLoads(std::string &hlsl) {
        static const std::string declaration = "\nByteAddressBuffer ";
        size_t lastDeclarationEnd = std::string::npos;
        std::vector<std::string> names;
        for (size_t pos = hlsl.find(declaration); pos != std::string::npos; pos = hlsl.find(declaration, pos)) {
            const size_t nameStart = pos + declaration.size();
            size_t nameEnd = nameStart;
            while ((nameEnd < hlsl.size()) && isIdentifierChar(hlsl[nameEnd])) {
                nameEnd++;
            }

            names.emplace_back(hlsl.substr(nameStart, nameEnd - nameStart));
            hlsl.replace(pos + 1, declaration.size() - 1, "Buffer<uint> ");
            lastDeclarationEnd = hlsl.find('\n', pos + 1);
            pos = lastDeclarationEnd;
        }

        if (names.empty()) {
            return;
        }

        hlsl.insert(lastDeclarationEnd + 1,
            "\nuint plume_load1(Buffer<uint> b, uint o) { return b.Load(o >> 2); }\n"
            "uint2 plume_load2(Buffer<uint> b, uint o) { uint i = o >> 2; return uint2(b.Load(i), b.Load(i + 1)); }\n"
            "uint3 plume_load3(Buffer<uint> b, uint o) { uint i = o >> 2; return uint3(b.Load(i), b.Load(i + 1), b.Load(i + 2)); }\n"
            "uint4 plume_load4(Buffer<uint> b, uint o) { uint i = o >> 2; return uint4(b.Load(i), b.Load(i + 1), b.Load(i + 2), b.Load(i + 3)); }\n");

        for (const std::string &name : names) {
            replaceCalls(hlsl, name + ".Load4(", "plume_load4(" + name + ", ");
            replaceCalls(hlsl, name + ".Load3(", "plume_load3(" + name + ", ");
            replaceCalls(hlsl, name + ".Load2(", "plume_load2(" + name + ", ");
            replaceCalls(hlsl, name + ".Load(", "plume_load1(" + name + ", ");
        }
    }

    bool D3D11Device::translateShader(const RenderShader *shader, const RenderSpecConstant *specConstants, uint32_t specConstantsCount, uint32_t uavBase, D3D11StageProgram &program, uint32_t &stage) {
        const D3D11Shader *interfaceShader = static_cast<const D3D11Shader *>(shader);
        if (interfaceShader == nullptr) {
            return false;
        }

        // Feature level 10_x devices only take shader model 4; 4.0 for 10_1 too, so one precompiled set covers both.
        const uint32_t shaderModel = (featureLevel >= D3D_FEATURE_LEVEL_11_0) ? 50 : 40;
        const char *targetSuffix = (shaderModel == 50) ? "_5_0" : "_4_0";
        uint64_t hash = 0xCBF29CE484222325ULL;
        if (shaderModel != 50) {
            hash = hashBytes(hash, &shaderModel, sizeof(shaderModel));
        }

        hash = hashBytes(hash, interfaceShader->spirv.data(), interfaceShader->spirv.size() * sizeof(uint32_t));
        hash = hashBytes(hash, interfaceShader->entryPointName.data(), interfaceShader->entryPointName.size());
        hash = hashBytes(hash, &uavBase, sizeof(uavBase));
        for (uint32_t i = 0; i < specConstantsCount; i++) {
            hash = hashBytes(hash, &specConstants[i], sizeof(RenderSpecConstant));
        }

        char hashName[32];
        snprintf(hashName, sizeof(hashName), "%016llx.dxbc", (unsigned long long)(hash));
        const std::string cachePath = shaderCacheDirectory + "/" + hashName;
        if (readCache(cachePath, program, stage)) {
            return true;
        }

        try {
            spirv_cross::CompilerHLSL compiler(interfaceShader->spirv);
            spv::ExecutionModel model = spv::ExecutionModelMax;
            for (const spirv_cross::EntryPoint &entryPoint : compiler.get_entry_points_and_stages()) {
                if ((entryPoint.name == interfaceShader->entryPointName) || (model == spv::ExecutionModelMax)) {
                    model = entryPoint.execution_model;
                    compiler.set_entry_point(entryPoint.name, model);
                }
            }

            std::string target;
            switch (model) {
            case spv::ExecutionModelVertex: stage = StageVertex; target = std::string("vs") + targetSuffix; break;
            case spv::ExecutionModelGeometry: stage = StageGeometry; target = std::string("gs") + targetSuffix; break;
            case spv::ExecutionModelFragment: stage = StagePixel; target = std::string("ps") + targetSuffix; break;
            case spv::ExecutionModelGLCompute: stage = StageCompute; target = std::string("cs") + targetSuffix; break;
            default:
                fprintf(stderr, "[plume-d3d11] Unsupported shader stage for %s.\n", interfaceShader->entryPointName.c_str());
                return false;
            }

            for (const spirv_cross::SpecializationConstant &constant : compiler.get_specialization_constants()) {
                for (uint32_t i = 0; i < specConstantsCount; i++) {
                    if (specConstants[i].index == constant.constant_id) {
                        compiler.get_constant(constant.id).m.c[0].r[0].u32 = specConstants[i].value;
                    }
                }
            }

            spirv_cross::CompilerGLSL::Options commonOptions = compiler.get_common_options();
            // shortcut: assumes the SPIR-V position output was y-inverted for Vulkan (DXC -fvk-invert-y), as RT64's is.
            commonOptions.vertex.flip_vert_y = (model == spv::ExecutionModelVertex) || (model == spv::ExecutionModelGeometry);
            compiler.set_common_options(commonOptions);

            spirv_cross::CompilerHLSL::Options hlslOptions;
            hlslOptions.shader_model = shaderModel;
            compiler.set_hlsl_options(hlslOptions);

            uint32_t srvRegister = 0, uavRegister = uavBase, cbvRegister = 0, samplerRegister = 0;
            program.bindings.clear();
            auto addBinding = [&](const spirv_cross::Resource &resource, D3D11BindingKind kind) -> bool {
                const spirv_cross::SPIRType &type = compiler.get_type(resource.type_id);
                uint32_t count = 1;
                if (!type.array.empty()) {
                    count = type.array[0];
                    if (count == 0) {
                        fprintf(stderr, "[plume-d3d11] Unbounded resource array %s isn't supported in D3D11.\n", resource.name.c_str());
                        return false;
                    }
                }

                D3D11ShaderBinding binding;
                binding.set = compiler.get_decoration(resource.id, spv::DecorationDescriptorSet);
                binding.binding = compiler.get_decoration(resource.id, spv::DecorationBinding);
                binding.count = count;
                binding.kind = kind;
                uint32_t *counter = nullptr;
                switch (kind) {
                case D3D11BindingKind::SRV: counter = &srvRegister; break;
                case D3D11BindingKind::UAV: counter = &uavRegister; break;
                case D3D11BindingKind::SAMPLER: counter = &samplerRegister; break;
                default: counter = &cbvRegister; break;
                }

                binding.reg = *counter;
                if ((kind == D3D11BindingKind::SRV) && (type.basetype == spirv_cross::SPIRType::Image)) {
                    binding.viewDimension = imageViewDimension(type.image);
                }

                *counter += count;

                spirv_cross::HLSLResourceBinding remap;
                remap.stage = model;
                remap.desc_set = binding.set;
                remap.binding = binding.binding;
                if (kind == D3D11BindingKind::PUSH_CONSTANTS) {
                    remap.desc_set = spirv_cross::ResourceBindingPushConstantDescriptorSet;
                    remap.binding = spirv_cross::ResourceBindingPushConstantBinding;
                }

                remap.cbv.register_binding = binding.reg;
                remap.uav.register_binding = binding.reg;
                remap.srv.register_binding = binding.reg;
                remap.sampler.register_binding = binding.reg;
                compiler.add_hlsl_resource_binding(remap);
                program.bindings.push_back(binding);
                return true;
            };

            // Only resources the entry point uses get registers; RT64 declares 18 samplers but D3D11 has 16 slots.
            // Stage inputs and outputs all stay, since D3D11 links VS and PS by declaration order.
            auto activeVariables = compiler.get_active_interface_variables();
            const spirv_cross::ShaderResources allResources = compiler.get_shader_resources();
            for (const spirv_cross::Resource &resource : allResources.stage_inputs) {
                activeVariables.insert(resource.id);
            }

            for (const spirv_cross::Resource &resource : allResources.stage_outputs) {
                activeVariables.insert(resource.id);
            }

            compiler.set_enabled_interface_variables(activeVariables);
            const spirv_cross::ShaderResources resources = compiler.get_shader_resources(activeVariables);
            bool ok = true;
            for (const spirv_cross::Resource &resource : resources.uniform_buffers) {
                ok = ok && addBinding(resource, D3D11BindingKind::CBV);
            }

            for (const spirv_cross::Resource &resource : resources.push_constant_buffers) {
                ok = ok && addBinding(resource, D3D11BindingKind::PUSH_CONSTANTS);
            }

            for (const spirv_cross::Resource &resource : resources.storage_buffers) {
                const bool readOnly = compiler.get_buffer_block_flags(resource.id).get(spv::DecorationNonWritable);
                ok = ok && addBinding(resource, readOnly ? D3D11BindingKind::SRV : D3D11BindingKind::UAV);
            }

            for (const spirv_cross::Resource &resource : resources.separate_images) {
                ok = ok && addBinding(resource, D3D11BindingKind::SRV);
            }

            for (const spirv_cross::Resource &resource : resources.storage_images) {
                ok = ok && addBinding(resource, D3D11BindingKind::UAV);
            }

            for (const spirv_cross::Resource &resource : resources.separate_samplers) {
                ok = ok && addBinding(resource, D3D11BindingKind::SAMPLER);
            }

            if (!resources.sampled_images.empty() || !resources.acceleration_structures.empty()) {
                fprintf(stderr, "[plume-d3d11] Combined image samplers and acceleration structures aren't supported.\n");
                ok = false;
            }

            if (!ok) {
                return false;
            }

            std::string hlsl = compiler.compile();
            movePositionFirst(hlsl, (model == spv::ExecutionModelFragment) ? "struct SPIRV_Cross_Input" : "struct SPIRV_Cross_Output");
            if ((shaderModel < 50) && (model != spv::ExecutionModelGLCompute)) {
                useTypedBufferLoads(hlsl);
            }
            static const char *dumpDirectory = std::getenv("PLUME_D3D11_DUMP_HLSL");
            if (dumpDirectory != nullptr) {
                std::ofstream(std::string(dumpDirectory) + "/" + hashName + ".hlsl") << "// " << interfaceShader->entryPointName << "\n" << hlsl;
            }
            D3D11Ptr<ID3DBlob> blob;
            D3D11Ptr<ID3DBlob> errors;
            // Concurrent D3DCompile calls crash inside d3dcompiler_47 (random access violations with 16 precompile threads), so compiles run one at a time.
            static std::mutex compileMutex;
            HRESULT res;
            {
                std::scoped_lock compileLock(compileMutex);
                res = D3DCompile(hlsl.data(), hlsl.size(), interfaceShader->entryPointName.c_str(), nullptr, nullptr, "main", target.c_str(), D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, blob.GetAddressOf(), errors.GetAddressOf());
            }
            if (FAILED(res)) {
                fprintf(stderr, "[plume-d3d11] D3DCompile failed for %s:\n%s\n", interfaceShader->entryPointName.c_str(), (errors != nullptr) ? static_cast<const char *>(errors->GetBufferPointer()) : "");
                return false;
            }

            const uint8_t *bytecode = static_cast<const uint8_t *>(blob->GetBufferPointer());
            program.bytecode.assign(bytecode, bytecode + blob->GetBufferSize());
        }
        catch (const std::exception &e) {
            fprintf(stderr, "[plume-d3d11] SPIRV-Cross failed for %s: %s\n", interfaceShader->entryPointName.c_str(), e.what());
            return false;
        }

        writeCache(cachePath, program, stage);
        return true;
    }

    // D3D11Interface

    D3D11Interface::D3D11Interface() {
        if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(dxgiFactory.GetAddressOf())))) {
            fprintf(stderr, "[plume-d3d11] CreateDXGIFactory1 failed.\n");
            return;
        }

        D3D11Ptr<IDXGIFactory5> dxgiFactory5;
        if (SUCCEEDED(dxgiFactory.As(&dxgiFactory5))) {
            BOOL allowTearingBool = FALSE;
            allowTearing = SUCCEEDED(dxgiFactory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allowTearingBool, sizeof(allowTearingBool))) && allowTearingBool;
        }

        capabilities.shaderFormat = RenderShaderFormat::SPIRV;

        D3D11Ptr<IDXGIAdapter1> adapterOption;
        for (UINT adapterIndex = 0; dxgiFactory->EnumAdapters1(adapterIndex, adapterOption.ReleaseAndGetAddressOf()) != DXGI_ERROR_NOT_FOUND; adapterIndex++) {
            DXGI_ADAPTER_DESC1 adapterDesc;
            adapterOption->GetDesc1(&adapterDesc);
            if ((adapterDesc.Flags & (DXGI_ADAPTER_FLAG_REMOTE | DXGI_ADAPTER_FLAG_SOFTWARE)) == 0) {
                deviceNames.emplace_back(Utf16ToUtf8(adapterDesc.Description));
            }
        }
    }

    std::unique_ptr<RenderDevice> D3D11Interface::createDevice(const std::string &preferredDeviceName) {
        std::unique_ptr<D3D11Device> createdDevice = std::make_unique<D3D11Device>(this, preferredDeviceName);
        return (createdDevice->d3d != nullptr) ? std::move(createdDevice) : nullptr;
    }

    const RenderInterfaceCapabilities &D3D11Interface::getCapabilities() const {
        return capabilities;
    }

    const std::vector<std::string> &D3D11Interface::getDeviceNames() const {
        return deviceNames;
    }

    bool D3D11Interface::isValid() const {
        return dxgiFactory != nullptr;
    }

    // Global creation function.

    std::unique_ptr<RenderInterface> CreateD3D11Interface() {
        std::unique_ptr<D3D11Interface> createdInterface = std::make_unique<D3D11Interface>();
        return createdInterface->isValid() ? std::move(createdInterface) : nullptr;
    }
};
