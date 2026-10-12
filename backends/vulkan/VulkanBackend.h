// ArgonUI - backends/vulkan/VulkanBackend.h
//
// The Vulkan backend: ONE render pass, ONE descriptor set layout, TWO pipelines
// (premultiplied alpha / additive) and one 64-byte vertex format, exactly like
// the D3D11 backend. The maths lives in the shader, not on the CPU - see
// Draw/Vertex.h for why.
//
// Two things make this backend different from the others:
//
//   1. It consumes PRECOMPILED SPIR-V. A UI library cannot assume glslang or
//      glslc is installed on the machine it ships to, so nothing here shells
//      out to a compiler. The GLSL the SPIR-V is built from lives at the top
//      of VulkanBackend.cpp (kArgonVertGlsl / kArgonFragGlsl) and is a
//      line-by-line port of shaders/argonui.hlsl. Hosts may point the backend
//      at .spv files (SetShaderPaths / ARGONUI_VULKAN_SHADER_DIR) or hand it
//      blobs it already embedded (SetShaderSpv).
//
//   2. The WSI platform is chosen at compile time, before vulkan.h is even
//      parsed - see ARGON_VK_SURFACE_EXT at the top of the .cpp.
//
// Everything adopted from the host (device, instance, surface, swapchain) is
// used but never destroyed; everything we create is destroyed in Shutdown() in
// reverse creation order.
#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <vulkan/vulkan.h>

#include <cstddef>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "argonui/Backend/Backend.h"

namespace argon::ui {
class VulkanBackend final : public IArgonBackend {
public:
    VulkanBackend();
    ~VulkanBackend() override;

    // -- IArgonBackend -----------------------------------------------------
    bool Initialize(const DeviceDesc& desc) override;
    void Shutdown() override;

    bool CreateTarget(const TargetDesc& desc) override;
    bool AdoptTarget(const TargetDesc& desc) override;
    bool ResizeTarget(u32 width, u32 height) override;
    void DestroyTarget() override;
    bool HasTarget() const override { return !images_.empty(); }
    Vec2 TargetSize() const override { return targetSize_; }

    bool BeginFrame(const FrameState& state) override;
    void Render(const DrawList& list, const FrameState& state) override;
    bool Present() override;
    void EndFrame() override;

    u32 CreateTexture(const TextureDesc& desc, const void* pixels = nullptr) override;
    bool UpdateTexture(u32 handle, const TextureDesc& desc, const void* pixels) override;
    void DestroyTexture(u32 handle) override;

    const BackendInfo& Info() const override { return info_; }
    const char* Name() const override { return "Vulkan"; }
    const char* LastError() const override { return lastError_.c_str(); }

    // -- Shader loading ----------------------------------------------------
    /// Point the backend at two precompiled SPIR-V modules. Must be called
    /// before Initialize(). Either path may be null (the built-in search order
    /// is used for that stage), but calling this at all disables the
    /// executable-relative and working-directory candidates for the stage you
    /// named - the override wins outright.
    void SetShaderPaths(const char* vertexSpv, const char* fragmentSpv);

    /// Hand the backend SPIR-V directly, bypassing the filesystem entirely.
    /// Must be called before Initialize(). This wins over SetShaderPaths() and
    /// over every search location. A null pointer or a zero size clears that
    /// stage's blob and falls back to the path search.
    void SetShaderSpv(const void* vertex, size_t vertexSize,
                      const void* fragment, size_t fragmentSize);

    /// The GLSL the shipped SPIR-V is built from, for hosts that would rather
    /// run glslc themselves than distribute a .spv.
    static const char* VertexGlsl();
    static const char* FragmentGlsl();

    // -- Escape hatches for hosts that interleave their own draws ---------
    VkInstance Instance() const { return instance_; }
    VkPhysicalDevice PhysicalDevice() const { return physicalDevice_; }
    VkDevice Device() const { return device_; }
    VkQueue Queue() const { return queue_; }
    VkSurfaceKHR Surface() const { return surface_; }
    VkSwapchainKHR SwapChain() const { return swapChain_; }
    /// Command buffer of the swapchain image the current frame is recording
    /// into, or VK_NULL_HANDLE outside a frame. Valid after BeginFrame().
    VkCommandBuffer CommandBuffer() const { return currentCommandBuffer_; }
    VkFormat SurfaceFormat() const { return surfaceFormat_; }
    u32 ImageCount() const { return imageCount_; }
    u32 DrawCallCount() const { return drawCallCount_; }

private:
    /// One texture: the sampled image plus the descriptor set that points at
    /// it. Staging is transient (uploaded and freed inside UploadTextureData).
    struct TextureRecord {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
        TextureDesc desc{};
    };

    /// Everything that belongs to a single swapchain image. Vulkan is happier
    /// when the pieces stay together, and it makes "image 2's fence / image 1's
    /// command buffer" a bug you cannot write.
    struct ImageResource {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;      ///< only for images we own
        VkImageView view = VK_NULL_HANDLE;
        VkFramebuffer framebuffer = VK_NULL_HANDLE;
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        VkSemaphore acquireSemaphore = VK_NULL_HANDLE;   // VK_NULL_HANDLE offscreen
        VkSemaphore presentSemaphore = VK_NULL_HANDLE;
        VkFence fence = VK_NULL_HANDLE;
        VkBuffer viewportBuffer = VK_NULL_HANDLE;
        VkDeviceMemory viewportMemory = VK_NULL_HANDLE;
        VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
        void* viewportMapped = nullptr;
    };

    // -- Instance / device -------------------------------------------------
    bool CreateInstance(bool wantValidation);
    void CreateDebugMessenger();
    bool LoadInstanceFunctions();
    bool PickPhysicalDevice(bool requireSurface);
    bool CreateDevice();
    bool CreateSurface();
    bool FindQueueFamilyForQueue();
    bool CreateCommandPool();

    // -- Pipeline ----------------------------------------------------------
    bool ResolveShaders();
    bool CreateShaderModule(const std::vector<u8>& spirv, VkShaderModule& out,
                            const char* what);
    bool CreateStaticResources();   // set layout, descriptor pool, sampler, white
    bool CreateRenderPass(VkFormat format);
    void DestroyRenderPass();
    bool EnsurePipelines(VkFormat format);
    void DestroyPipelines();
    bool CreatePipeline(VkBlendFactor srcColor, VkBlendFactor dstColor, VkPipeline& out);

    // -- Target ------------------------------------------------------------
    void PickSurfaceFormat();
    void PickExtent(const VkSurfaceCapabilitiesKHR& caps, u32 width, u32 height);
    bool CreateSwapchainForWindow(u32 width, u32 height);
    bool CreateOffscreenTarget(u32 width, u32 height);
    bool BuildTargetResources(const std::vector<VkImage>& images);
    bool RecreateSwapchain(u32 width, u32 height);
    bool RecreateFramebuffers();
    void DestroyTargetResources();

    // -- Memory / buffers --------------------------------------------------
    u32 FindMemoryType(u32 typeBits, VkMemoryPropertyFlags required) const;
    bool AllocateMemory(VkDeviceSize size, u32 typeBits, VkMemoryPropertyFlags required,
                        VkDeviceMemory* out);
    bool CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                      VkMemoryPropertyFlags properties, VkBuffer* buffer,
                      VkDeviceMemory* memory);
    bool CreateHostVisibleBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                                 VkBuffer* buffer, VkDeviceMemory* memory, void** mapped);
    void DestroyBuffer(VkBuffer* buffer, VkDeviceMemory* memory);
    bool EnsureBufferCapacity(size_t vertexBytes, size_t indexBytes);
    /// Record `body` into a throwaway command buffer, submit it and block until
    /// it has finished. Used for the texture uploads, which happen outside the
    /// frame loop.
    bool RunOneTimeCommand(const std::function<void(VkCommandBuffer)>& body);

    // -- Textures ----------------------------------------------------------
    bool CreateTextureInternal(const TextureDesc& desc, const void* pixels, TextureRecord& out);
    bool UploadTextureData(const TextureDesc& desc, const void* pixels, TextureRecord& rec);
    void DestroyTextureRecord(TextureRecord& rec);
    TextureRecord* ResolveTexture(u32 handle);

    // -- Frame helpers -----------------------------------------------------
    /// Writes bindings 0/1 (frame uniform buffer + sampler) and binds the set,
    /// but only when something it depends on actually changed.
    bool BindTexture(VkCommandBuffer cmd, VkDescriptorSet set, VkImageView view, u32 slot);

    void Fail(const char* what, VkResult result);

    // -- Shader source / override state ------------------------------------
    std::string shaderVertPath_;
    std::string shaderFragPath_;
    std::vector<u8> shaderVertSpv_;
    std::vector<u8> shaderFragSpv_;

    // -- Instance / device -------------------------------------------------
    VkInstance instance_ = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debugMessenger_ = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue queue_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;

    VkPhysicalDeviceProperties deviceProperties_{};
    VkPhysicalDeviceMemoryProperties memoryProperties_{};

    bool ownsInstance_ = false;
    bool ownsSurface_ = false;
    bool ownsDevice_ = false;
    bool ownsSwapchain_ = false;
    bool validationEnabled_ = false;
    bool transparentBackground_ = false;
    bool enableVsync_ = true;
    u32 queueFamily_ = 0;
    void* windowHandle_ = nullptr;

    // WSI + KHR_swapchain entry points. They are extension functions, so they
    // are not in vulkan-1.lib and must be resolved through the loader.
    PFN_vkDestroySurfaceKHR pfnDestroySurfaceKHR = nullptr;
    PFN_vkGetPhysicalDeviceSurfaceSupportKHR pfnGetPhysicalDeviceSurfaceSupportKHR = nullptr;
    PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR pfnGetPhysicalDeviceSurfaceCapabilitiesKHR = nullptr;
    PFN_vkGetPhysicalDeviceSurfaceFormatsKHR pfnGetPhysicalDeviceSurfaceFormatsKHR = nullptr;
    PFN_vkGetPhysicalDeviceSurfacePresentModesKHR pfnGetPhysicalDeviceSurfacePresentModesKHR = nullptr;
    PFN_vkCreateSwapchainKHR pfnCreateSwapchainKHR = nullptr;
    PFN_vkDestroySwapchainKHR pfnDestroySwapchainKHR = nullptr;
    PFN_vkGetSwapchainImagesKHR pfnGetSwapchainImagesKHR = nullptr;
    PFN_vkAcquireNextImageKHR pfnAcquireNextImageKHR = nullptr;
    PFN_vkQueuePresentKHR pfnQueuePresentKHR = nullptr;
    PFN_vkCreateDebugUtilsMessengerEXT pfnCreateDebugUtilsMessengerEXT = nullptr;
    PFN_vkDestroyDebugUtilsMessengerEXT pfnDestroyDebugUtilsMessengerEXT = nullptr;
    PFN_vkGetPhysicalDeviceProperties2 pfnGetPhysicalDeviceProperties2 = nullptr;

    // -- Target ------------------------------------------------------------
    VkSwapchainKHR swapChain_ = VK_NULL_HANDLE;
    VkSwapchainKHR previousSwapChain_ = VK_NULL_HANDLE;   ///< passed as oldSwapchain
    VkFormat surfaceFormat_ = VK_FORMAT_UNDEFINED;
    VkColorSpaceKHR surfaceColorSpace_ = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    VkExtent2D targetExtent_{0, 0};
    u32 targetWidth_ = 0;
    u32 targetHeight_ = 0;
    Vec2 targetSize_{0.0f, 0.0f};
    bool offscreenTarget_ = false;
    /// CLEAR our own images, LOAD somebody else's - an overlay must not wipe
    /// the frame the host just drew.
    bool clearTarget_ = true;
    bool swapchainOutOfDate_ = false;

    std::vector<ImageResource> images_;
    bool ownsImages_ = false;                      ///< true only for the offscreen target
    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    VkRenderPass renderPass_ = VK_NULL_HANDLE;
    u32 imageCount_ = 0;
    u32 currentImage_ = 0;
    VkCommandBuffer currentCommandBuffer_ = VK_NULL_HANDLE;

    // -- Pipeline ----------------------------------------------------------
    VkShaderModule vsModule_ = VK_NULL_HANDLE;
    VkShaderModule fsModule_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout setLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline pipelineAlpha_ = VK_NULL_HANDLE;
    VkPipeline pipelineAdditive_ = VK_NULL_HANDLE;
    VkSampler sampler_ = VK_NULL_HANDLE;
    VkFormat pipelineFormat_ = VK_FORMAT_UNDEFINED;

    // -- Buffers -----------------------------------------------------------
    VkBuffer vertexBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory vertexMemory_ = VK_NULL_HANDLE;
    void* vertexMapped_ = nullptr;
    size_t vertexCapacity_ = 0;

    VkBuffer indexBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory indexMemory_ = VK_NULL_HANDLE;
    void* indexMapped_ = nullptr;
    size_t indexCapacity_ = 0;

    // -- Textures ----------------------------------------------------------
    std::unordered_map<u32, TextureRecord> textures_;
    VkImageView whiteView_ = VK_NULL_HANDLE;
    u32 nextTextureId_ = 2;   // id 1 is the built-in white 1x1

    // -- Frame -------------------------------------------------------------
    bool frameBegun_ = false;
    bool frameSubmitted_ = false;
    u32 drawCallCount_ = 0;
    VkDescriptorSet boundSet_ = VK_NULL_HANDLE;
    VkImageView boundView_ = VK_NULL_HANDLE;
    u32 boundSlot_ = 0;

    BackendInfo info_{};
    char deviceName_[256] = "";
    char driverVersion_[64] = "";
    std::string lastError_;
};

/// Registry entry point (see src/Backend/Registry.cpp).
IArgonBackend* CreateVulkanBackend();

} // namespace argon::ui
