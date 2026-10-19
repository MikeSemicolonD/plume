//
// plume
//
// Copyright (c) 2024 renderbag and contributors. All rights reserved.
// Licensed under the MIT license. See LICENSE file for details.
//

#pragma once

#include "plume_render_interface.h"

#include <atomic>
#include <functional>
#include <map>
#include <mutex>

#include <d3d11_1.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

namespace plume {
    template <typename T>
    using D3D11Ptr = Microsoft::WRL::ComPtr<T>;

    struct D3D11Buffer;
    struct D3D11Context;
    struct D3D11Device;
    struct D3D11Interface;
    struct D3D11PipelineLayout;
    struct D3D11Texture;

    enum class D3D11BindingKind : uint8_t {
        SRV,
        UAV,
        CBV,
        SAMPLER,
        PUSH_CONSTANTS
    };

    struct D3D11ShaderBinding {
        uint32_t set = 0;
        uint32_t binding = 0;
        uint32_t count = 1;
        D3D11BindingKind kind = D3D11BindingKind::SRV;
        uint32_t reg = 0;

        // D3D11_SRV_DIMENSION the shader declares for a texture; views of another type are left unbound. 0 skips the check.
        uint32_t viewDimension = 0;
    };

    struct D3D11Descriptor {
        D3D11Ptr<ID3D11ShaderResourceView> srv;
        D3D11Ptr<ID3D11UnorderedAccessView> uav;
        D3D11Ptr<ID3D11SamplerState> sampler;
        const D3D11Buffer *buffer = nullptr;
        bool constantBuffer = false;
        uint32_t srvDimension = 0;
    };

    struct D3D11DescriptorSet : RenderDescriptorSet {
        struct Range {
            RenderDescriptorRangeType type = RenderDescriptorRangeType::UNKNOWN;
            uint32_t binding = 0;
            uint32_t count = 0;
            uint32_t first = 0;
        };

        D3D11Device *device = nullptr;
        std::vector<Range> ranges;
        std::vector<D3D11Descriptor> descriptors;

        D3D11DescriptorSet(D3D11Device *device, const RenderDescriptorSetDesc &desc);
        void setBuffer(uint32_t descriptorIndex, const RenderBuffer *buffer, uint64_t bufferSize, const RenderBufferStructuredView *bufferStructuredView, const RenderBufferFormattedView *bufferFormattedView) override;
        void setTexture(uint32_t descriptorIndex, const RenderTexture *texture, RenderTextureLayout textureLayout, const RenderTextureView *textureView) override;
        void setSampler(uint32_t descriptorIndex, const RenderSampler *sampler) override;
        void setAccelerationStructure(uint32_t descriptorIndex, const RenderAccelerationStructure *accelerationStructure) override;
        const Range *rangeOf(uint32_t descriptorIndex) const;
        const D3D11Descriptor *find(uint32_t binding, uint32_t element) const;
    };

    struct D3D11Buffer : RenderBuffer {
        struct PendingReadback {
            D3D11Ptr<ID3D11Resource> staging;
            uint64_t dstOffset = 0;
            uint32_t rowBytes = 0;
            uint32_t rowPitch = 0;
            uint32_t rows = 0;
            uint32_t depth = 1;
        };

        D3D11Device *device = nullptr;
        RenderBufferDesc desc;
        D3D11Ptr<ID3D11Buffer> d3d;
        mutable D3D11Ptr<ID3D11Buffer> cbuffer;
        mutable std::vector<uint8_t> shadow;
        mutable std::vector<PendingReadback> pendingReadbacks;
        // Writers on other threads bump the generation on map and unmap, so a sync that raced a write is redone by the next submission.
        mutable std::atomic<bool> mapped = false;
        mutable std::atomic<uint64_t> writeGeneration = 0;
        mutable uint64_t syncedGeneration = UINT64_MAX;
        mutable uint64_t syncedSubmission = UINT64_MAX;

        D3D11Buffer(D3D11Device *device, const RenderBufferDesc &desc);
        void *map(uint32_t subresource, const RenderRange *readRange) override;
        void unmap(uint32_t subresource, const RenderRange *writtenRange) override;
        std::unique_ptr<RenderBufferFormattedView> createBufferFormattedView(RenderFormat format) override;
        void setName(const std::string &name) override;
        uint64_t getDeviceAddress() const override;
        bool isUpload() const;
        bool isReadback() const;
        void sync(D3D11Context &context) const;
        ID3D11Buffer *constantBuffer(D3D11Context &context) const;
    };

    struct D3D11BufferFormattedView : RenderBufferFormattedView {
        RenderFormat format = RenderFormat::UNKNOWN;

        D3D11BufferFormattedView(RenderFormat format);
    };

    struct D3D11Texture : RenderTexture {
        D3D11Device *device = nullptr;
        RenderTextureDesc desc;
        D3D11Ptr<ID3D11Resource> d3d;
        DXGI_FORMAT resourceFormat = DXGI_FORMAT_UNKNOWN;
        mutable std::mutex viewMutex;
        mutable D3D11Ptr<ID3D11ShaderResourceView> defaultSRV;

        D3D11Texture() = default;
        D3D11Texture(D3D11Device *device, const RenderTextureDesc &desc);
        std::unique_ptr<RenderTextureView> createTextureView(const RenderTextureViewDesc &desc) const override;
        void setName(const std::string &name) override;
        ID3D11ShaderResourceView *getDefaultSRV() const;
        uint32_t subresourceIndex(uint32_t mipLevel, uint32_t arrayIndex) const;
    };

    struct D3D11TextureView : RenderTextureView {
        const D3D11Texture *texture = nullptr;
        RenderTextureViewDesc desc;
        D3D11Ptr<ID3D11ShaderResourceView> srv;

        D3D11TextureView(const D3D11Texture *texture, const RenderTextureViewDesc &desc);
    };

    struct D3D11Shader : RenderShader {
        std::vector<uint32_t> spirv;
        std::string entryPointName;
        RenderShaderFormat format = RenderShaderFormat::UNKNOWN;

        D3D11Shader(const void *data, uint64_t size, const char *entryPointName, RenderShaderFormat format);
        void setName(const std::string &name) override;
    };

    struct D3D11Sampler : RenderSampler {
        D3D11Ptr<ID3D11SamplerState> d3d;

        D3D11Sampler(D3D11Device *device, const RenderSamplerDesc &desc);
    };

    struct D3D11StageProgram {
        D3D11Ptr<ID3D11DeviceChild> shader;
        std::vector<uint8_t> bytecode;
        std::vector<D3D11ShaderBinding> bindings;
    };

    struct D3D11Pipeline : RenderPipeline {
        enum class Type {
            Unknown,
            Compute,
            Graphics
        };

        Type type = Type::Unknown;
        bool valid = false;

        D3D11Pipeline(Type type);
        void setName(const std::string &name) override;
        RenderPipelineProgram getProgram(const std::string &name) const override;
    };

    struct D3D11ComputePipeline : D3D11Pipeline {
        D3D11StageProgram compute;

        D3D11ComputePipeline(D3D11Device *device, const RenderComputePipelineDesc &desc);
    };

    struct D3D11GraphicsPipeline : D3D11Pipeline {
        D3D11StageProgram vertex;
        D3D11StageProgram geometry;
        D3D11StageProgram pixel;
        D3D11Ptr<ID3D11InputLayout> inputLayout;
        D3D11Ptr<ID3D11RasterizerState> rasterizerState;
        D3D11Ptr<ID3D11BlendState> blendState;
        D3D11Ptr<ID3D11DepthStencilState> depthStencilState;
        D3D11_PRIMITIVE_TOPOLOGY topology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
        uint32_t stencilRef = 0;
        uint32_t renderTargetCount = 0;

        D3D11GraphicsPipeline(D3D11Device *device, const RenderGraphicsPipelineDesc &desc);
    };

    struct D3D11PipelineLayout : RenderPipelineLayout {
        std::vector<RenderPushConstantRange> pushConstantRanges;
        uint32_t pushConstantSize = 0;
        std::map<uint64_t, D3D11Ptr<ID3D11SamplerState>> immutableSamplers;

        D3D11PipelineLayout(D3D11Device *device, const RenderPipelineLayoutDesc &desc);
        ID3D11SamplerState *immutableSampler(uint32_t set, uint32_t binding, uint32_t element) const;
    };

    struct D3D11Framebuffer : RenderFramebuffer {
        uint32_t width = 0;
        uint32_t height = 0;
        std::vector<D3D11Ptr<ID3D11RenderTargetView>> colorViews;
        D3D11Ptr<ID3D11DepthStencilView> depthView;

        D3D11Framebuffer(D3D11Device *device, const RenderFramebufferDesc &desc);
        uint32_t getWidth() const override;
        uint32_t getHeight() const override;
    };

    struct D3D11BindPoint {
        const D3D11PipelineLayout *layout = nullptr;
        D3D11DescriptorSet *sets[8] = {};
        std::vector<uint8_t> pushConstants;
        D3D11Ptr<ID3D11Buffer> pushBuffer;
        uint32_t pushBufferSize = 0;
        bool pushDirty = false;
        bool bindingsDirty = true;
    };

    // Recorded commands replay on the immediate context at submission.
    struct D3D11Context {
        D3D11Device *device = nullptr;
        ID3D11DeviceContext1 *ctx = nullptr;
        D3D11BindPoint graphics;
        D3D11BindPoint compute;
        const D3D11GraphicsPipeline *graphicsPipeline = nullptr;
        const D3D11ComputePipeline *computePipeline = nullptr;
        const D3D11Framebuffer *framebuffer = nullptr;
        bool graphicsPipelineDirty = true;
        bool computePipelineDirty = true;
        bool framebufferDirty = true;
        const D3D11Buffer *vertexBuffers[D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT] = {};
        const D3D11Buffer *indexBuffer = nullptr;
        std::vector<D3D11_VIEWPORT> viewports;
        std::vector<D3D11_RECT> scissors;
        uint64_t submission = 0;

        void reset();
        void unbindOutputs();
        void flushGraphics();
        void applyFramebuffer(ID3D11UnorderedAccessView **psUAVs = nullptr, uint32_t psUAVCount = 0);
        void clearDepthRects(bool clearDepth, bool clearStencil, float depthValue, uint32_t stencilValue, const std::vector<D3D11_RECT> &rects);
        void flushCompute();
        void applyBindings(D3D11BindPoint &bindPoint, const D3D11StageProgram &program, uint32_t stage, ID3D11UnorderedAccessView **psUAVs, uint32_t *psUAVCount);
        void updatePushConstants(D3D11BindPoint &bindPoint);
    };

    typedef std::function<void(D3D11Context &)> D3D11Command;

    struct D3D11CommandList : RenderCommandList {
        D3D11Device *device = nullptr;
        std::vector<D3D11Command> commands;
        const D3D11PipelineLayout *recordedGraphicsLayout = nullptr;
        const D3D11PipelineLayout *recordedComputeLayout = nullptr;
        std::vector<std::unique_ptr<D3D11DescriptorSet>> capturedSets;

        D3D11CommandList(D3D11Device *device);
        D3D11DescriptorSet *captureSet(D3D11DescriptorSet *set);
        void begin() override;
        void end() override;
        void barriers(RenderBarrierStages stages, const RenderBufferBarrier *bufferBarriers, uint32_t bufferBarriersCount, const RenderTextureBarrier *textureBarriers, uint32_t textureBarriersCount) override;
        void dispatch(uint32_t threadGroupCountX, uint32_t threadGroupCountY, uint32_t threadGroupCountZ) override;
        void traceRays(uint32_t width, uint32_t height, uint32_t depth, RenderBufferReference shaderBindingTable, const RenderShaderBindingGroupsInfo &shaderBindingGroupsInfo) override;
        void drawInstanced(uint32_t vertexCountPerInstance, uint32_t instanceCount, uint32_t startVertexLocation, uint32_t startInstanceLocation) override;
        void drawIndexedInstanced(uint32_t indexCountPerInstance, uint32_t instanceCount, uint32_t startIndexLocation, int32_t baseVertexLocation, uint32_t startInstanceLocation) override;
        void setPipeline(const RenderPipeline *pipeline) override;
        void setComputePipelineLayout(const RenderPipelineLayout *pipelineLayout) override;
        void setComputePushConstants(uint32_t rangeIndex, const void *data, uint32_t offset, uint32_t size) override;
        void setComputeDescriptorSet(RenderDescriptorSet *descriptorSet, uint32_t setIndex) override;
        void setGraphicsPipelineLayout(const RenderPipelineLayout *pipelineLayout) override;
        void setGraphicsPushConstants(uint32_t rangeIndex, const void *data, uint32_t offset, uint32_t size) override;
        void setGraphicsDescriptorSet(RenderDescriptorSet *descriptorSet, uint32_t setIndex) override;
        void setGraphicsRootDescriptor(RenderBufferReference bufferReference, uint32_t rootDescriptorIndex) override;
        void setRaytracingPipelineLayout(const RenderPipelineLayout *pipelineLayout) override;
        void setRaytracingPushConstants(uint32_t rangeIndex, const void *data, uint32_t offset, uint32_t size) override;
        void setRaytracingDescriptorSet(RenderDescriptorSet *descriptorSet, uint32_t setIndex) override;
        void setIndexBuffer(const RenderIndexBufferView *view) override;
        void setVertexBuffers(uint32_t startSlot, const RenderVertexBufferView *views, uint32_t viewCount, const RenderInputSlot *inputSlots) override;
        void setViewports(const RenderViewport *viewports, uint32_t count) override;
        void setScissors(const RenderRect *scissorRects, uint32_t count) override;
        void setFramebuffer(const RenderFramebuffer *framebuffer) override;
        void setDepthBias(float depthBias, float depthBiasClamp, float slopeScaledDepthBias) override;
        void clearColor(uint32_t attachmentIndex, RenderColor colorValue, const RenderRect *clearRects, uint32_t clearRectsCount) override;
        void clearDepthStencil(bool clearDepth, bool clearStencil, float depthValue, uint32_t stencilValue, const RenderRect *clearRects, uint32_t clearRectsCount) override;
        void copyBufferRegion(RenderBufferReference dstBuffer, RenderBufferReference srcBuffer, uint64_t size) override;
        void copyTextureRegion(const RenderTextureCopyLocation &dstLocation, const RenderTextureCopyLocation &srcLocation, uint32_t dstX, uint32_t dstY, uint32_t dstZ, const RenderBox *srcBox) override;
        void copyBuffer(const RenderBuffer *dstBuffer, const RenderBuffer *srcBuffer) override;
        void copyTexture(const RenderTexture *dstTexture, const RenderTexture *srcTexture) override;
        void resolveTexture(const RenderTexture *dstTexture, const RenderTexture *srcTexture) override;
        void resolveTextureRegion(const RenderTexture *dstTexture, uint32_t dstX, uint32_t dstY, const RenderTexture *srcTexture, const RenderRect *srcRect, RenderResolveMode resolveMode) override;
        void buildBottomLevelAS(const RenderAccelerationStructure *dstAccelerationStructure, RenderBufferReference scratchBuffer, const RenderBottomLevelASBuildInfo &buildInfo) override;
        void buildTopLevelAS(const RenderAccelerationStructure *dstAccelerationStructure, RenderBufferReference scratchBuffer, RenderBufferReference instancesBuffer, const RenderTopLevelASBuildInfo &buildInfo) override;
        void discardTexture(const RenderTexture *texture) override;
        void resetQueryPool(const RenderQueryPool *queryPool, uint32_t queryFirstIndex, uint32_t queryCount) override;
        void writeTimestamp(const RenderQueryPool *queryPool, uint32_t queryIndex) override;
        void setPushConstants(bool compute, uint32_t rangeIndex, const void *data, uint32_t offset, uint32_t size);
    };

    struct D3D11CommandFence : RenderCommandFence {
        D3D11Ptr<ID3D11Query> query;
        bool pending = false;

        D3D11CommandFence(D3D11Device *device);
    };

    struct D3D11CommandSemaphore : RenderCommandSemaphore {
    };

    struct D3D11SwapChain : RenderSwapChain {
        D3D11Device *device = nullptr;
        RenderSwapChainDesc desc;
        D3D11Ptr<IDXGISwapChain1> d3d;
        HANDLE waitableObject = nullptr;
        std::vector<std::unique_ptr<D3D11Texture>> textures;
        DXGI_FORMAT nativeFormat = DXGI_FORMAT_UNKNOWN;
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t nextTexture = 0;
        bool vsyncEnabled = true;
        UINT swapChainFlags = 0;

        D3D11SwapChain(D3D11Device *device, const RenderSwapChainDesc &desc);
        ~D3D11SwapChain() override;
        bool present(uint32_t textureIndex, RenderCommandSemaphore **waitSemaphores, uint32_t waitSemaphoreCount) override;
        void wait() override;
        bool resize() override;
        bool needsResize() const override;
        void setVsyncEnabled(bool vsyncEnabled) override;
        bool isVsyncEnabled() const override;
        uint32_t getWidth() const override;
        uint32_t getHeight() const override;
        RenderTexture *getTexture(uint32_t textureIndex) override;
        uint32_t getTextureCount() const override;
        bool acquireTexture(RenderCommandSemaphore *signalSemaphore, uint32_t *textureIndex) override;
        RenderWindow getWindow() const override;
        bool isEmpty() const override;
        uint32_t getRefreshRate() const override;
        void getWindowSize(uint32_t &dstWidth, uint32_t &dstHeight) const;
        void setTextures();
    };

    struct D3D11CommandQueue : RenderCommandQueue {
        D3D11Device *device = nullptr;

        D3D11CommandQueue(D3D11Device *device);
        std::unique_ptr<RenderCommandList> createCommandList() override;
        std::unique_ptr<RenderSwapChain> createSwapChain(const RenderSwapChainDesc &desc) override;
        void executeCommandLists(const RenderCommandList **commandLists, uint32_t commandListCount, RenderCommandSemaphore **waitSemaphores, uint32_t waitSemaphoreCount, RenderCommandSemaphore **signalSemaphores, uint32_t signalSemaphoreCount, RenderCommandFence *signalFence) override;
        void waitForCommandFence(RenderCommandFence *fence) override;
    };

    struct D3D11Pool : RenderPool {
        D3D11Device *device = nullptr;

        D3D11Pool(D3D11Device *device);
        std::unique_ptr<RenderBuffer> createBuffer(const RenderBufferDesc &desc) override;
        std::unique_ptr<RenderTexture> createTexture(const RenderTextureDesc &desc) override;
    };

    struct D3D11QueryPool : RenderQueryPool {
        std::vector<uint64_t> results;

        D3D11QueryPool(uint32_t queryCount);
        void queryResults() override;
        const uint64_t *getResults() const override;
        uint32_t getCount() const override;
    };

    struct D3D11Device : RenderDevice {
        D3D11Interface *renderInterface = nullptr;
        D3D11Ptr<IDXGIAdapter1> adapter;
        D3D11Ptr<ID3D11Device1> d3d;
        D3D11Ptr<ID3D11DeviceContext1> immediateContext;
        D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_0;
        std::recursive_mutex contextMutex;
        D3D11Context context;
        RenderDeviceCapabilities capabilities;
        RenderDeviceDescription description;
        std::string shaderCacheDirectory;
        D3D11Ptr<ID3D11InfoQueue> infoQueue;

        // Draw-based clear for depth rects, which D3D11 can't clear directly.
        struct {
            D3D11Ptr<ID3D11VertexShader> vertexShader;
            D3D11Ptr<ID3D11PixelShader> pixelShader;
            D3D11Ptr<ID3D11Buffer> constants;
            D3D11Ptr<ID3D11RasterizerState> rasterizerState;
            D3D11Ptr<ID3D11BlendState> blendState;
            D3D11Ptr<ID3D11DepthStencilState> depthStencilStates[3];
            bool created = false;
        } rectClear;

        D3D11Device(D3D11Interface *renderInterface, const std::string &preferredDeviceName);
        ~D3D11Device() override;
        std::unique_ptr<RenderDescriptorSet> createDescriptorSet(const RenderDescriptorSetDesc &desc) override;
        std::unique_ptr<RenderShader> createShader(const void *data, uint64_t size, const char *entryPointName, RenderShaderFormat format) override;
        std::unique_ptr<RenderSampler> createSampler(const RenderSamplerDesc &desc) override;
        std::unique_ptr<RenderPipeline> createComputePipeline(const RenderComputePipelineDesc &desc) override;
        std::unique_ptr<RenderPipeline> createGraphicsPipeline(const RenderGraphicsPipelineDesc &desc) override;
        std::unique_ptr<RenderPipeline> createRaytracingPipeline(const RenderRaytracingPipelineDesc &desc, const RenderPipeline *previousPipeline) override;
        std::unique_ptr<RenderCommandQueue> createCommandQueue(RenderCommandListType type) override;
        std::unique_ptr<RenderBuffer> createBuffer(const RenderBufferDesc &desc) override;
        std::unique_ptr<RenderTexture> createTexture(const RenderTextureDesc &desc) override;
        std::unique_ptr<RenderAccelerationStructure> createAccelerationStructure(const RenderAccelerationStructureDesc &desc) override;
        std::unique_ptr<RenderPool> createPool(const RenderPoolDesc &desc) override;
        std::unique_ptr<RenderPipelineLayout> createPipelineLayout(const RenderPipelineLayoutDesc &desc) override;
        std::unique_ptr<RenderCommandFence> createCommandFence() override;
        std::unique_ptr<RenderCommandSemaphore> createCommandSemaphore() override;
        std::unique_ptr<RenderFramebuffer> createFramebuffer(const RenderFramebufferDesc &desc) override;
        std::unique_ptr<RenderQueryPool> createQueryPool(uint32_t queryCount) override;
        void setBottomLevelASBuildInfo(RenderBottomLevelASBuildInfo &buildInfo, const RenderBottomLevelASMesh *meshes, uint32_t meshCount, bool preferFastBuild, bool preferFastTrace) override;
        void setTopLevelASBuildInfo(RenderTopLevelASBuildInfo &buildInfo, const RenderTopLevelASInstance *instances, uint32_t instanceCount, bool preferFastBuild, bool preferFastTrace) override;
        void setShaderBindingTableInfo(RenderShaderBindingTableInfo &tableInfo, const RenderShaderBindingGroups &groups, const RenderPipeline *pipeline, RenderDescriptorSet **descriptorSets, uint32_t descriptorSetCount) override;
        const RenderDeviceCapabilities &getCapabilities() const override;
        const RenderDeviceDescription &getDescription() const override;
        RenderSampleCounts getSampleCountsSupported(RenderFormat format) const override;
        bool isLost() const override;
        bool beginCapture() override;
        bool endCapture() override;
        void printDebugMessages();
        bool translateShader(const RenderShader *shader, const RenderSpecConstant *specConstants, uint32_t specConstantsCount, uint32_t uavBase, D3D11StageProgram &program, uint32_t &stage);
    };

    struct D3D11Interface : RenderInterface {
        D3D11Ptr<IDXGIFactory2> dxgiFactory;
        RenderInterfaceCapabilities capabilities;
        std::vector<std::string> deviceNames;
        bool allowTearing = false;

        D3D11Interface();
        std::unique_ptr<RenderDevice> createDevice(const std::string &preferredDeviceName) override;
        const RenderInterfaceCapabilities &getCapabilities() const override;
        const std::vector<std::string> &getDeviceNames() const override;
        bool isValid() const;
    };
};
