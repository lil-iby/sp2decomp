// nvn_bridge.cpp — NVN → host-graphics bridge implementation.
//
// Stub backend (default): every proc is replaced with a thin logger.
// Vulkan backend: compile with -DNVN_BACKEND_VULKAN (requires Vulkan SDK).
//
// See nvn/nvn_bridge.hpp for the design rationale.
#include "nvn/nvn_bridge.hpp"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <vector>

// ---- optional Vulkan include ---------------------------------------------
#if defined(NVN_BACKEND_VULKAN)
#  if defined(_WIN32)
#    define WIN32_LEAN_AND_MEAN
#    include <windows.h>
#    define VK_USE_PLATFORM_WIN32_KHR
#  endif
#  include <vulkan/vulkan.h>
#endif

namespace nvn::bridge {

// ---- module state --------------------------------------------------------
namespace {

#if defined(NVN_BACKEND_VULKAN)
constexpr Backend kActiveBackend = Backend::Vulkan;
#else
constexpr Backend kActiveBackend = Backend::Stub;
#endif

std::atomic<std::uint64_t> g_nvnCalls{0};
std::atomic<std::uint64_t> g_nvnUnimpl{0};
std::atomic<std::uint64_t> g_frames{0};
std::atomic<std::uint64_t> g_drawCalls{0};

SurfaceDesc g_surface{};
bool        g_surfaceCreated{false};

// ---- Vulkan state --------------------------------------------------------
#if defined(NVN_BACKEND_VULKAN)

static constexpr uint32_t kMaxSwapImages = 4;

struct VkState {
    VkInstance       instance     = VK_NULL_HANDLE;
    VkPhysicalDevice physDevice   = VK_NULL_HANDLE;
    VkDevice         device       = VK_NULL_HANDLE;
    VkQueue          graphicsQ    = VK_NULL_HANDLE;
    uint32_t         graphicsQFam = UINT32_MAX;

    VkSurfaceKHR     surface    = VK_NULL_HANDLE;
    VkSwapchainKHR   swapchain  = VK_NULL_HANDLE;
    VkFormat         swapFormat = VK_FORMAT_UNDEFINED;
    VkExtent2D       swapExtent = {0, 0};

    uint32_t     imageCount   = 0;
    uint32_t     currentImage = 0;
    uint32_t     currentFence = 0;
    uint64_t     frameIdx     = 0;

    VkImage       swapImages[kMaxSwapImages]   = {};
    VkImageView   swapViews[kMaxSwapImages]    = {};
    VkFramebuffer framebuffers[kMaxSwapImages] = {};
    VkCommandBuffer cmdBufs[kMaxSwapImages]    = {};
    VkSemaphore   imgAvail[kMaxSwapImages]     = {};
    VkSemaphore   renderDone[kMaxSwapImages]   = {};
    VkFence       inFlight[kMaxSwapImages]     = {};

    VkRenderPass renderPass = VK_NULL_HANDLE;
    VkCommandPool cmdPool   = VK_NULL_HANDLE;
};

VkState g_vk;
bool    g_vkReady = false;

// ---- Vulkan helpers -------------------------------------------------------

static bool vkCreateInstanceHelper() {
    VkApplicationInfo app{};
    app.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName   = "sp2decomp";
    app.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
    app.apiVersion         = VK_API_VERSION_1_2;

    const char* exts[] = {
        VK_KHR_SURFACE_EXTENSION_NAME,
#if defined(VK_USE_PLATFORM_WIN32_KHR)
        VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
#endif
    };

    VkInstanceCreateInfo ci{};
    ci.sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ci.pApplicationInfo        = &app;
    ci.enabledExtensionCount   = static_cast<uint32_t>(sizeof(exts) / sizeof(*exts));
    ci.ppEnabledExtensionNames = exts;

    VkResult r = vkCreateInstance(&ci, nullptr, &g_vk.instance);
    if (r != VK_SUCCESS) {
        std::printf("nvn::bridge: vkCreateInstance failed (%d)\n", (int)r);
        return false;
    }
    return true;
}

static bool pickPhysDevice() {
    uint32_t count = 0;
    vkEnumeratePhysicalDevices(g_vk.instance, &count, nullptr);
    if (count == 0) { std::puts("nvn::bridge: no Vulkan-capable GPU found"); return false; }

    std::vector<VkPhysicalDevice> devs(count);
    vkEnumeratePhysicalDevices(g_vk.instance, &count, devs.data());

    for (VkPhysicalDevice pd : devs) {
        uint32_t qCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(pd, &qCount, nullptr);
        std::vector<VkQueueFamilyProperties> qProps(qCount);
        vkGetPhysicalDeviceQueueFamilyProperties(pd, &qCount, qProps.data());

        for (uint32_t i = 0; i < qCount; ++i) {
            if (qProps[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                g_vk.physDevice   = pd;
                g_vk.graphicsQFam = i;
                VkPhysicalDeviceProperties props{};
                vkGetPhysicalDeviceProperties(pd, &props);
                std::printf("nvn::bridge: GPU: %s\n", props.deviceName);
                return true;
            }
        }
    }
    std::puts("nvn::bridge: no GPU with a graphics queue");
    return false;
}

static bool createDevice() {
    const float prio = 1.0f;
    VkDeviceQueueCreateInfo qci{};
    qci.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qci.queueFamilyIndex = g_vk.graphicsQFam;
    qci.queueCount       = 1;
    qci.pQueuePriorities = &prio;

    const char* devExts[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };

    VkDeviceCreateInfo ci{};
    ci.sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    ci.queueCreateInfoCount    = 1;
    ci.pQueueCreateInfos       = &qci;
    ci.enabledExtensionCount   = 1;
    ci.ppEnabledExtensionNames = devExts;

    VkResult r = vkCreateDevice(g_vk.physDevice, &ci, nullptr, &g_vk.device);
    if (r != VK_SUCCESS) {
        std::printf("nvn::bridge: vkCreateDevice failed (%d)\n", (int)r);
        return false;
    }
    vkGetDeviceQueue(g_vk.device, g_vk.graphicsQFam, 0, &g_vk.graphicsQ);
    return true;
}

static bool createSwapchain(void* nativeWindow, uint32_t w, uint32_t h) {
    // ---- Win32 surface -------------------------------------------------
#if defined(VK_USE_PLATFORM_WIN32_KHR)
    {
        VkWin32SurfaceCreateInfoKHR sci{};
        sci.sType     = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
        sci.hinstance = GetModuleHandleW(nullptr);
        sci.hwnd      = static_cast<HWND>(nativeWindow);
        VkResult r = vkCreateWin32SurfaceKHR(g_vk.instance, &sci, nullptr, &g_vk.surface);
        if (r != VK_SUCCESS) {
            std::printf("nvn::bridge: vkCreateWin32SurfaceKHR failed (%d)\n", (int)r);
            return false;
        }
    }
#else
    (void)nativeWindow;
    std::puts("nvn::bridge: no surface platform compiled in");
    return false;
#endif

    // ---- surface capabilities ----------------------------------------
    VkSurfaceCapabilitiesKHR caps{};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(g_vk.physDevice, g_vk.surface, &caps);
    VkExtent2D extent = caps.currentExtent;
    if (extent.width == UINT32_MAX) extent = {w, h};
    g_vk.swapExtent = extent;

    // ---- surface format -----------------------------------------------
    uint32_t fmtCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(g_vk.physDevice, g_vk.surface, &fmtCount, nullptr);
    std::vector<VkSurfaceFormatKHR> fmts(fmtCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(g_vk.physDevice, g_vk.surface, &fmtCount, fmts.data());
    g_vk.swapFormat = fmts[0].format;
    VkColorSpaceKHR cs = fmts[0].colorSpace;
    for (const auto& f : fmts) {
        if (f.format == VK_FORMAT_B8G8R8A8_SRGB) { g_vk.swapFormat = f.format; cs = f.colorSpace; break; }
    }

    // ---- swapchain -------------------------------------------------------
    uint32_t imgCount = 2;
    if (caps.maxImageCount > 0 && imgCount > caps.maxImageCount) imgCount = caps.maxImageCount;
    if (imgCount < caps.minImageCount) imgCount = caps.minImageCount;

    VkSwapchainCreateInfoKHR swci{};
    swci.sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swci.surface          = g_vk.surface;
    swci.minImageCount    = imgCount;
    swci.imageFormat      = g_vk.swapFormat;
    swci.imageColorSpace  = cs;
    swci.imageExtent      = extent;
    swci.imageArrayLayers = 1;
    swci.imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    swci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    swci.preTransform     = caps.currentTransform;
    swci.compositeAlpha   = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    swci.presentMode      = VK_PRESENT_MODE_FIFO_KHR;
    swci.clipped          = VK_TRUE;

    VkResult r = vkCreateSwapchainKHR(g_vk.device, &swci, nullptr, &g_vk.swapchain);
    if (r != VK_SUCCESS) {
        std::printf("nvn::bridge: vkCreateSwapchainKHR failed (%d)\n", (int)r);
        return false;
    }

    // ---- images & views -----------------------------------------------
    vkGetSwapchainImagesKHR(g_vk.device, g_vk.swapchain, &g_vk.imageCount, nullptr);
    if (g_vk.imageCount > kMaxSwapImages) g_vk.imageCount = kMaxSwapImages;
    vkGetSwapchainImagesKHR(g_vk.device, g_vk.swapchain, &g_vk.imageCount, g_vk.swapImages);

    for (uint32_t i = 0; i < g_vk.imageCount; ++i) {
        VkImageViewCreateInfo ivci{};
        ivci.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        ivci.image    = g_vk.swapImages[i];
        ivci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        ivci.format   = g_vk.swapFormat;
        ivci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCreateImageView(g_vk.device, &ivci, nullptr, &g_vk.swapViews[i]);
    }

    // ---- render pass ---------------------------------------------------
    VkAttachmentDescription att{};
    att.format         = g_vk.swapFormat;
    att.samples        = VK_SAMPLE_COUNT_1_BIT;
    att.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    att.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    att.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    att.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    att.finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference ref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments    = &ref;

    VkSubpassDependency dep{};
    dep.srcSubpass    = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass    = 0;
    dep.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.srcAccessMask = 0;
    dep.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo rpci{};
    rpci.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpci.attachmentCount = 1;
    rpci.pAttachments    = &att;
    rpci.subpassCount    = 1;
    rpci.pSubpasses      = &subpass;
    rpci.dependencyCount = 1;
    rpci.pDependencies   = &dep;
    vkCreateRenderPass(g_vk.device, &rpci, nullptr, &g_vk.renderPass);

    // ---- framebuffers -------------------------------------------------
    for (uint32_t i = 0; i < g_vk.imageCount; ++i) {
        VkFramebufferCreateInfo fbci{};
        fbci.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbci.renderPass      = g_vk.renderPass;
        fbci.attachmentCount = 1;
        fbci.pAttachments    = &g_vk.swapViews[i];
        fbci.width           = extent.width;
        fbci.height          = extent.height;
        fbci.layers          = 1;
        vkCreateFramebuffer(g_vk.device, &fbci, nullptr, &g_vk.framebuffers[i]);
    }

    // ---- command pool + buffers ---------------------------------------
    VkCommandPoolCreateInfo cpci{};
    cpci.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cpci.queueFamilyIndex = g_vk.graphicsQFam;
    cpci.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    vkCreateCommandPool(g_vk.device, &cpci, nullptr, &g_vk.cmdPool);

    VkCommandBufferAllocateInfo cbai{};
    cbai.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cbai.commandPool        = g_vk.cmdPool;
    cbai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbai.commandBufferCount = g_vk.imageCount;
    vkAllocateCommandBuffers(g_vk.device, &cbai, g_vk.cmdBufs);

    // ---- sync objects -------------------------------------------------
    VkSemaphoreCreateInfo sci2{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, nullptr, 0};
    VkFenceCreateInfo fci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, nullptr,
                          VK_FENCE_CREATE_SIGNALED_BIT};
    for (uint32_t i = 0; i < g_vk.imageCount; ++i) {
        vkCreateSemaphore(g_vk.device, &sci2, nullptr, &g_vk.imgAvail[i]);
        vkCreateSemaphore(g_vk.device, &sci2, nullptr, &g_vk.renderDone[i]);
        vkCreateFence(g_vk.device, &fci, nullptr, &g_vk.inFlight[i]);
    }

    std::printf("nvn::bridge: swapchain %ux%u fmt=%d images=%u\n",
                extent.width, extent.height, (int)g_vk.swapFormat, g_vk.imageCount);
    return true;
}

#endif // NVN_BACKEND_VULKAN

} // namespace

// ---- lifecycle -----------------------------------------------------------

void Initialize() {
#if defined(NVN_BACKEND_VULKAN)
    std::puts("nvn::bridge: Vulkan backend initializing...");
    if (!vkCreateInstanceHelper()) return;
    if (!pickPhysDevice())         return;
    if (!createDevice())           return;
    std::puts("nvn::bridge: Vulkan device ready (call CreateSurface to show a window)");
#else
    std::puts("nvn::bridge: Stub backend active");
#endif
}

void Finalize() {
#if defined(NVN_BACKEND_VULKAN)
    if (g_vk.device) {
        vkDeviceWaitIdle(g_vk.device);
        for (uint32_t i = 0; i < g_vk.imageCount; ++i) {
            if (g_vk.inFlight[i])     vkDestroyFence(g_vk.device, g_vk.inFlight[i], nullptr);
            if (g_vk.renderDone[i])   vkDestroySemaphore(g_vk.device, g_vk.renderDone[i], nullptr);
            if (g_vk.imgAvail[i])     vkDestroySemaphore(g_vk.device, g_vk.imgAvail[i], nullptr);
            if (g_vk.framebuffers[i]) vkDestroyFramebuffer(g_vk.device, g_vk.framebuffers[i], nullptr);
            if (g_vk.swapViews[i])    vkDestroyImageView(g_vk.device, g_vk.swapViews[i], nullptr);
        }
        if (g_vk.cmdPool)   vkDestroyCommandPool(g_vk.device, g_vk.cmdPool, nullptr);
        if (g_vk.renderPass) vkDestroyRenderPass(g_vk.device, g_vk.renderPass, nullptr);
        if (g_vk.swapchain) vkDestroySwapchainKHR(g_vk.device, g_vk.swapchain, nullptr);
        if (g_vk.surface)   vkDestroySurfaceKHR(g_vk.instance, g_vk.surface, nullptr);
        vkDestroyDevice(g_vk.device, nullptr);
    }
    if (g_vk.instance) vkDestroyInstance(g_vk.instance, nullptr);
    g_vk      = VkState{};
    g_vkReady = false;
#endif
    g_surfaceCreated = false;
}

// ---- backend query -------------------------------------------------------

Backend     GetActiveBackend()     { return kActiveBackend; }
const char* GetActiveBackendName() {
    return (kActiveBackend == Backend::Vulkan) ? "Vulkan" : "Stub";
}

// ---- proc injection ------------------------------------------------------

struct StubContext { std::size_t index; };
static constexpr std::size_t kMaxProcs = 512;
static StubContext g_stubCtx[kMaxProcs];

static void GenericStubProc() {
    g_nvnCalls.fetch_add(1, std::memory_order_relaxed);
}

static void UnbackedStubProc() {
    g_nvnUnimpl.fetch_add(1, std::memory_order_relaxed);
    g_nvnCalls.fetch_add(1,  std::memory_order_relaxed);
}

void InjectProcs(void** procTable, std::size_t count) {
    if (procTable == nullptr || count == 0) return;
    const std::size_t n = (count < kMaxProcs) ? count : kMaxProcs;
    for (std::size_t i = 0; i < n; ++i) {
        if (procTable[i] == nullptr) {
            procTable[i] = reinterpret_cast<void*>(&UnbackedStubProc);
        } else {
#if defined(NVN_BACKEND_VULKAN)
            // NVN→Vulkan mappings will replace slots here as they are implemented.
            (void)i;
#else
            procTable[i] = reinterpret_cast<void*>(&GenericStubProc);
#endif
        }
    }
    std::printf("nvn::bridge: injected into %zu NVN proc slots (%s backend)\n",
                n, GetActiveBackendName());
}

// ---- surface / swapchain -------------------------------------------------

bool CreateSurface(const SurfaceDesc& desc) {
    g_surface        = desc;
    g_surfaceCreated = true;
    std::printf("nvn::bridge: surface %ux%u back=%u vsync=%d (window=%p)\n",
                desc.width, desc.height, desc.backbufferCount, (int)desc.vsync,
                desc.nativeWindowHandle);
#if defined(NVN_BACKEND_VULKAN)
    if (!g_vk.device) {
        std::puts("nvn::bridge: CreateSurface called before Initialize -- ignored");
        return false;
    }
    if (!createSwapchain(desc.nativeWindowHandle, desc.width, desc.height)) return false;
    g_vkReady = true;
#endif
    return true;
}

void DestroySurface() {
#if defined(NVN_BACKEND_VULKAN)
    if (g_vk.device) {
        vkDeviceWaitIdle(g_vk.device);
        for (uint32_t i = 0; i < g_vk.imageCount; ++i) {
            if (g_vk.framebuffers[i]) { vkDestroyFramebuffer(g_vk.device, g_vk.framebuffers[i], nullptr); g_vk.framebuffers[i] = VK_NULL_HANDLE; }
            if (g_vk.swapViews[i])    { vkDestroyImageView(g_vk.device, g_vk.swapViews[i], nullptr);    g_vk.swapViews[i]    = VK_NULL_HANDLE; }
        }
        if (g_vk.swapchain) { vkDestroySwapchainKHR(g_vk.device, g_vk.swapchain, nullptr); g_vk.swapchain = VK_NULL_HANDLE; }
        if (g_vk.surface)   { vkDestroySurfaceKHR(g_vk.instance, g_vk.surface, nullptr);   g_vk.surface   = VK_NULL_HANDLE; }
    }
    g_vkReady = false;
#endif
    g_surfaceCreated = false;
}

// ---- frame boundary ------------------------------------------------------

void BeginFrame() {
#if defined(NVN_BACKEND_VULKAN)
    if (!g_vkReady) return;

    uint32_t fi = static_cast<uint32_t>(g_vk.frameIdx % g_vk.imageCount);

    // Wait for the previous use of this frame slot
    vkWaitForFences(g_vk.device, 1, &g_vk.inFlight[fi], VK_TRUE, UINT64_MAX);
    vkResetFences(g_vk.device, 1, &g_vk.inFlight[fi]);
    g_vk.currentFence = fi;

    // Acquire the next swapchain image
    vkAcquireNextImageKHR(g_vk.device, g_vk.swapchain, UINT64_MAX,
                          g_vk.imgAvail[fi], VK_NULL_HANDLE, &g_vk.currentImage);

    // Record clear command for this image
    VkCommandBuffer cb = g_vk.cmdBufs[g_vk.currentImage];
    vkResetCommandBuffer(cb, 0);

    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    vkBeginCommandBuffer(cb, &begin);

    // Clear to Splatoon teal
    VkClearValue clearVal{};
    clearVal.color.float32[0] = 0.00f;
    clearVal.color.float32[1] = 0.18f;
    clearVal.color.float32[2] = 0.28f;
    clearVal.color.float32[3] = 1.00f;

    VkRenderPassBeginInfo rp{};
    rp.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rp.renderPass        = g_vk.renderPass;
    rp.framebuffer       = g_vk.framebuffers[g_vk.currentImage];
    rp.renderArea.offset = {0, 0};
    rp.renderArea.extent = g_vk.swapExtent;
    rp.clearValueCount   = 1;
    rp.pClearValues      = &clearVal;
    vkCmdBeginRenderPass(cb, &rp, VK_SUBPASS_CONTENTS_INLINE);
    // NVN draw calls will be dispatched into this render pass as InjectProcs
    // is expanded with real Vulkan-backed function pointers.
    vkCmdEndRenderPass(cb);
    vkEndCommandBuffer(cb);
#endif
}

void EndFrame() {
    g_frames.fetch_add(1, std::memory_order_relaxed);
    g_drawCalls.store(0, std::memory_order_relaxed);
#if defined(NVN_BACKEND_VULKAN)
    if (!g_vkReady) return;

    uint32_t fi = g_vk.currentFence;
    VkCommandBuffer cb = g_vk.cmdBufs[g_vk.currentImage];

    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submit{};
    submit.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.waitSemaphoreCount   = 1;
    submit.pWaitSemaphores      = &g_vk.imgAvail[fi];
    submit.pWaitDstStageMask    = &waitStage;
    submit.commandBufferCount   = 1;
    submit.pCommandBuffers      = &cb;
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores    = &g_vk.renderDone[fi];
    vkQueueSubmit(g_vk.graphicsQ, 1, &submit, g_vk.inFlight[fi]);

    VkPresentInfoKHR present{};
    present.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores    = &g_vk.renderDone[fi];
    present.swapchainCount     = 1;
    present.pSwapchains        = &g_vk.swapchain;
    present.pImageIndices      = &g_vk.currentImage;
    vkQueuePresentKHR(g_vk.graphicsQ, &present);

    g_vk.frameIdx++;
#endif
}

// ---- stats ---------------------------------------------------------------

Stats GetStats() {
    Stats s;
    s.framesRendered      = g_frames.load(std::memory_order_relaxed);
    s.drawCallsLastFrame  = g_drawCalls.load(std::memory_order_relaxed);
    s.nvnCallsTotal       = g_nvnCalls.load(std::memory_order_relaxed);
    s.nvnUnimplCallsTotal = g_nvnUnimpl.load(std::memory_order_relaxed);
    return s;
}

void ResetStats() {
    g_nvnCalls.store(0,  std::memory_order_relaxed);
    g_nvnUnimpl.store(0, std::memory_order_relaxed);
    g_frames.store(0,    std::memory_order_relaxed);
    g_drawCalls.store(0, std::memory_order_relaxed);
}

} // namespace nvn::bridge
