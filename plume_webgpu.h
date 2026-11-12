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
#include <string>
#include <unordered_map>
#include <vector>

#include <webgpu/webgpu.h>

// WebGPU backend for the browser (Emscripten). WebGPU objects live on the main browser thread: every call that touches them runs there through
// webgpuRunOnMain, and command lists record on any thread and replay on the main thread at submission. Shaders arrive as SPIR-V and are turned
// into WGSL by a converter the host supplies. Push constants are a uniform buffer at set 0, binding 999 (the host's SPIR-V pass moves them there),
// fed from a per-submission ring with a dynamic offset.

namespace plume {
    struct WebGPUBuffer;
    struct WebGPUDevice;
    struct WebGPUInterface;
    struct WebGPUPipeline;
    struct WebGPUPipelineLayout;
    struct WebGPUTexture;

    // Converts SPIR-V to WGSL, specializing the given constants first. Runs on the main browser thread.
    typedef std::function<bool(const std::vector<uint32_t> &spirv, const RenderSpecConstant *specConstants, uint32_t specConstantsCount, std::string &wgsl, std::string &error)> WebGPUShaderConverter;

    constexpr uint32_t WebGPUPushConstantSet = 0;
    constexpr uint32_t WebGPUPushConstantBinding = 999;
    constexpr uint32_t WebGPUPushConstantStride = 256;

    // Runs the job on the main browser thread and blocks the caller until it returns; runs inline on the main thread.
    void webgpuRunOnMain(const std::function<void()> &job);

    // Like webgpuRunOnMain, but the job ends when it calls done (from a WebGPU callback, after the main thread yields). Not callable from the main thread.
    void webgpuRunOnMainAsync(const std::function<void(std::function<void()> done)> &job);

    struct WebGPUBuffer : RenderBuffer {
        WebGPUDevice *device = nullptr;
        RenderBufferDesc desc;
        WGPUBuffer buffer = nullptr;
        uint64_t allocatedSize = 0;
        std::vector<uint8_t> shadow;
        bool readback = false;

        WebGPUBuffer(WebGPUDevice *device, const RenderBufferDesc &desc);
        ~WebGPUBuffer() override;
        void *map(uint32_t subresource, const RenderRange *readRange) override;
        void unmap(uint32_t subresource, const RenderRange *writtenRange) override;
        std::unique_ptr<RenderBufferFormattedView> createBufferFormattedView(RenderFormat format) override;
        void setName(const std::string &name) override;
        uint64_t getDeviceAddress() const override;
    };

    struct WebGPUBufferFormattedView : RenderBufferFormattedView {
        RenderFormat format = RenderFormat::UNKNOWN;
    };

    struct WebGPUTexture : RenderTexture {
        WebGPUDevice *device = nullptr;
        RenderTextureDesc desc;
        WGPUTexture texture = nullptr;
        WGPUTextureFormat format = WGPUTextureFormat_Undefined;
        bool owned = true;
        mutable WGPUTextureView defaultView = nullptr;

        WebGPUTexture() = default;
        WebGPUTexture(WebGPUDevice *device, const RenderTextureDesc &desc);
        ~WebGPUTexture() override;
        std::unique_ptr<RenderTextureView> createTextureView(const RenderTextureViewDesc &desc) const override;
        void setName(const std::string &name) override;
        WGPUTextureView getDefaultView() const;
    };

    struct WebGPUTextureView : RenderTextureView {
        const WebGPUTexture *texture = nullptr;
        WGPUTextureView view = nullptr;

        WebGPUTextureView(const WebGPUTexture *texture, const RenderTextureViewDesc &desc);
        ~WebGPUTextureView() override;
    };

    struct WebGPUShader : RenderShader {
        std::vector<uint32_t> spirv;
        std::string entryPointName;

        WebGPUShader(const void *data, uint64_t size, const char *entryPointName);
        void setName(const std::string &name) override;
    };

    struct WebGPUSampler : RenderSampler {
        WGPUSampler sampler = nullptr;

        WebGPUSampler(WebGPUDevice *device, const RenderSamplerDesc &desc);
        ~WebGPUSampler() override;
    };

    struct WebGPUDescriptor {
        const WebGPUBuffer *buffer = nullptr;
        uint64_t offset = 0;
        uint64_t size = 0;
        WGPUTextureView view = nullptr;
        WGPUSampler sampler = nullptr;
    };

    struct WebGPUDescriptorSet : RenderDescriptorSet {
        struct Range {
            RenderDescriptorRangeType type = RenderDescriptorRangeType::UNKNOWN;
            uint32_t binding = 0;
            uint32_t count = 0;
            uint32_t first = 0;
        };

        struct CachedGroup {
            uint64_t generation = 0;
            uint64_t ringGeneration = 0;
            WGPUBindGroup group = nullptr;
        };

        WebGPUDevice *device = nullptr;
        std::vector<Range> ranges;
        std::vector<WebGPUDescriptor> descriptors;
        std::atomic<uint64_t> generation = 1;

        // Main thread only.
        std::unordered_map<WGPUBindGroupLayout, CachedGroup> groups;

        WebGPUDescriptorSet(WebGPUDevice *device, const RenderDescriptorSetDesc &desc);
        ~WebGPUDescriptorSet() override;
        void setBuffer(uint32_t descriptorIndex, const RenderBuffer *buffer, uint64_t bufferSize, const RenderBufferStructuredView *bufferStructuredView, const RenderBufferFormattedView *bufferFormattedView) override;
        void setTexture(uint32_t descriptorIndex, const RenderTexture *texture, RenderTextureLayout textureLayout, const RenderTextureView *textureView) override;
        void setSampler(uint32_t descriptorIndex, const RenderSampler *sampler) override;
        void setAccelerationStructure(uint32_t descriptorIndex, const RenderAccelerationStructure *accelerationStructure) override;
        const Range *rangeOf(uint32_t descriptorIndex) const;
        const WebGPUDescriptor *find(uint32_t binding) const;
    };

    struct WebGPUPipelineLayout : RenderPipelineLayout {
        std::vector<RenderPushConstantRange> pushConstantRanges;
        uint32_t pushConstantSize = 0;

        // Immutable samplers by (set << 32 | binding).
        std::map<uint64_t, WGPUSampler> immutableSamplers;

        WebGPUPipelineLayout(WebGPUDevice *device, const RenderPipelineLayoutDesc &desc);
    };

    // One resource a converted shader declares, read from its WGSL.
    struct WebGPUBinding {
        uint32_t group = 0;
        WGPUBindGroupLayoutEntry entry = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
    };

    struct WebGPUPipeline : RenderPipeline {
        enum class Type {
            Unknown,
            Compute,
            Graphics
        };

        Type type = Type::Unknown;
        bool valid = false;
        std::string error;
        WGPUComputePipeline compute = nullptr;
        WGPURenderPipeline render = nullptr;
        WGPUPipelineLayout layout = nullptr;
        std::vector<WGPUBindGroupLayout> groupLayouts;
        std::vector<std::vector<WGPUBindGroupLayoutEntry>> groupEntries;
        uint32_t stencilReference = 0;

        WebGPUPipeline(Type type);
        ~WebGPUPipeline() override;
        void setName(const std::string &name) override;
        RenderPipelineProgram getProgram(const std::string &name) const override;
    };

    struct WebGPUFramebuffer : RenderFramebuffer {
        uint32_t width = 0;
        uint32_t height = 0;
        std::vector<WGPUTextureView> colorViews;
        std::vector<WGPUTextureFormat> colorFormats;
        WGPUTextureView depthView = nullptr;
        WGPUTextureFormat depthFormat = WGPUTextureFormat_Undefined;
        uint32_t sampleCount = 1;
        bool depthReadOnly = false;
        std::vector<WGPUTextureView> ownedViews;

        WebGPUFramebuffer(WebGPUDevice *device, const RenderFramebufferDesc &desc);
        ~WebGPUFramebuffer() override;
        uint32_t getWidth() const override;
        uint32_t getHeight() const override;
    };

    struct WebGPUContext;
    typedef std::function<void(WebGPUContext &)> WebGPUCommand;

    struct WebGPUCommandList : RenderCommandList {
        WebGPUDevice *device = nullptr;
        std::vector<WebGPUCommand> commands;
        const WebGPUPipelineLayout *graphicsLayout = nullptr;
        const WebGPUPipelineLayout *computeLayout = nullptr;
        uint32_t pushFlushes = 0;

        WebGPUCommandList(WebGPUDevice *device);
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

    struct WebGPUCommandFence : RenderCommandFence {
        // 1 while a submission that signals it is in flight; waiters sleep on it.
        std::atomic<uint32_t> pending = 0;
    };

    struct WebGPUCommandSemaphore : RenderCommandSemaphore {
    };

    // Renders into its own textures and copies the presented one onto the page's #canvas, whose texture changes every frame.
    struct WebGPUSwapChain : RenderSwapChain {
        WebGPUDevice *device = nullptr;
        RenderSwapChainDesc desc;
        WGPUSurface surface = nullptr;
        WGPUTextureFormat format = WGPUTextureFormat_Undefined;
        std::vector<std::unique_ptr<RenderTexture>> textures;
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t nextTexture = 0;
        bool vsyncEnabled = true;

        WebGPUSwapChain(WebGPUDevice *device, const RenderSwapChainDesc &desc);
        ~WebGPUSwapChain() override;
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
        void canvasSize(uint32_t &canvasWidth, uint32_t &canvasHeight) const;
    };

    struct WebGPUCommandQueue : RenderCommandQueue {
        WebGPUDevice *device = nullptr;

        WebGPUCommandQueue(WebGPUDevice *device);
        std::unique_ptr<RenderCommandList> createCommandList() override;
        std::unique_ptr<RenderSwapChain> createSwapChain(const RenderSwapChainDesc &desc) override;
        void executeCommandLists(const RenderCommandList **commandLists, uint32_t commandListCount, RenderCommandSemaphore **waitSemaphores, uint32_t waitSemaphoreCount, RenderCommandSemaphore **signalSemaphores, uint32_t signalSemaphoreCount, RenderCommandFence *signalFence) override;
        void waitForCommandFence(RenderCommandFence *fence) override;
    };

    struct WebGPUPool : RenderPool {
        WebGPUDevice *device = nullptr;

        WebGPUPool(WebGPUDevice *device);
        std::unique_ptr<RenderBuffer> createBuffer(const RenderBufferDesc &desc) override;
        std::unique_ptr<RenderTexture> createTexture(const RenderTextureDesc &desc) override;
    };

    struct WebGPUQueryPool : RenderQueryPool {
        std::vector<uint64_t> results;

        WebGPUQueryPool(uint32_t queryCount);
        void queryResults() override;
        const uint64_t *getResults() const override;
        uint32_t getCount() const override;
    };

    // A converted shader module and the resources its WGSL declares.
    struct WebGPUModule {
        WGPUShaderModule module = nullptr;
        std::vector<WebGPUBinding> bindings;
        std::string error;
    };

    struct WebGPUDevice : RenderDevice {
        WebGPUInterface *renderInterface = nullptr;
        WGPUAdapter adapter = nullptr;
        WGPUDevice device = nullptr;
        WGPUQueue queue = nullptr;
        WGPULimits limits = WGPU_LIMITS_INIT;
        RenderDeviceCapabilities capabilities;
        RenderDeviceDescription description;
        bool dualSourceBlending = false;
        std::atomic<bool> lost = false;

        std::mutex errorMutex;
        std::string errors;

        // Buffers written on the CPU since the last submission, uploaded before it.
        std::mutex dirtyMutex;
        std::vector<WebGPUBuffer *> dirtyBuffers;

        // Main thread only.
        std::unordered_map<std::string, WebGPUModule> modules;
        std::unordered_map<WGPUBindGroupLayout, WebGPUDescriptorSet::CachedGroup> setlessGroups;

        // Partial clears draw a scissored triangle; WebGPU only clears whole attachments.
        struct {
            WGPUShaderModule module = nullptr;
            WGPUBindGroupLayout groupLayout = nullptr;
            WGPUPipelineLayout layout = nullptr;
            WebGPUDescriptorSet::CachedGroup group;
            std::unordered_map<std::string, WGPURenderPipeline> pipelines;
        } rectClear;

        WGPUBuffer pushRing = nullptr;
        uint64_t pushRingSize = 0;
        uint64_t pushRingGeneration = 0;

        WebGPUDevice(WebGPUInterface *renderInterface, const std::string &preferredDeviceName);
        ~WebGPUDevice() override;
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

        // Validation errors WebGPU reported outside an error scope since the last call.
        std::string takeErrors();
        void reportError(const std::string &message);
        void markDirty(WebGPUBuffer *buffer);
        void forgetBuffer(WebGPUBuffer *buffer);
        const WebGPUModule &getModule(const WebGPUShader *shader, const RenderSpecConstant *specConstants, uint32_t specConstantsCount, WGPUShaderStage stage);
    };

    struct WebGPUInterface : RenderInterface {
        WebGPUShaderConverter converter;
        WGPUInstance instance = nullptr;
        RenderInterfaceCapabilities capabilities;
        std::vector<std::string> deviceNames;

        WebGPUInterface(WebGPUShaderConverter converter);
        std::unique_ptr<RenderDevice> createDevice(const std::string &preferredDeviceName) override;
        const RenderInterfaceCapabilities &getCapabilities() const override;
        const std::vector<std::string> &getDeviceNames() const override;
    };

    std::unique_ptr<RenderInterface> CreateWebGPUInterface(WebGPUShaderConverter converter);
};
