// nvn_bridge.cpp — NVN → host-graphics bridge implementation.
//
// Stub backend (default): every proc is replaced with a thin logger.
// Vulkan backend (-DNVN_BACKEND_VULKAN): Vulkan is loaded at run time through volk, so the
// build needs no SDK and a machine without a Vulkan driver degrades to the stub behaviour.
// Frames currently clear the window; NVN draw calls will be translated into them.
//
// See nvn/nvn_bridge.hpp for the design rationale.
#include "nvn/nvn_bridge.hpp"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <vector>

#if defined(NVN_BACKEND_VULKAN)
#  if defined(_WIN32)
#    define WIN32_LEAN_AND_MEAN
#    include <windows.h>
#  endif
#  include <volk.h> // VK_NO_PROTOTYPES and the platform define come from the sp2_volk target
#endif

namespace nvn::bridge {

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

#if defined(NVN_BACKEND_VULKAN)

constexpr std::uint32_t kMaxSwapImages = 8;
constexpr std::uint32_t kFramesInFlight = 2;

struct VkState {
    bool             loaderReady  = false;
    VkInstance       instance     = VK_NULL_HANDLE;
    VkPhysicalDevice physDevice   = VK_NULL_HANDLE;
    VkDevice         device       = VK_NULL_HANDLE;
    VkQueue          graphicsQ    = VK_NULL_HANDLE;
    std::uint32_t    graphicsQFam = UINT32_MAX;

    void            *window     = nullptr;
    VkSurfaceKHR     surface    = VK_NULL_HANDLE;
    VkSwapchainKHR   swapchain  = VK_NULL_HANDLE;
    VkFormat         swapFormat = VK_FORMAT_UNDEFINED;
    VkColorSpaceKHR  colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    VkExtent2D       swapExtent = {0, 0};
    VkRenderPass     renderPass = VK_NULL_HANDLE;

    std::uint32_t imageCount = 0;
    VkImage       swapImages[kMaxSwapImages]   = {};
    VkImageView   swapViews[kMaxSwapImages]    = {};
    VkFramebuffer framebuffers[kMaxSwapImages] = {};
    // Indexed by swapchain image: the presentation engine may still hold the semaphore
    // signalled for an image until that image is acquired again.
    VkSemaphore   renderDone[kMaxSwapImages]   = {};

    // Indexed by frame slot, each slot guarded by its fence.
    VkCommandPool   cmdPool = VK_NULL_HANDLE;
    VkCommandBuffer cmdBufs[kFramesInFlight]  = {};
    VkSemaphore     imgAvail[kFramesInFlight] = {};
    VkFence         inFlight[kFramesInFlight] = {};

    std::uint64_t frameIdx     = 0;
    std::uint32_t frameSlot    = 0;
    std::uint32_t currentImage = 0;
    bool          frameActive  = false; // BeginFrame acquired an image EndFrame must submit
    bool          needsRebuild = false; // swapchain out of date (resize, minimise)
};

VkState g_vk;
bool    g_vkReady = false; // device + swapchain exist

bool createInstance() {
    VkApplicationInfo app{};
    app.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName   = "sp2decomp";
    app.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
    app.apiVersion         = VK_API_VERSION_1_2;

    const char *exts[] = {
        VK_KHR_SURFACE_EXTENSION_NAME,
#if defined(VK_USE_PLATFORM_WIN32_KHR)
        VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
#endif
    };

    VkInstanceCreateInfo ci{};
    ci.sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ci.pApplicationInfo        = &app;
    ci.enabledExtensionCount   = static_cast<std::uint32_t>(sizeof(exts) / sizeof(*exts));
    ci.ppEnabledExtensionNames = exts;

    const VkResult r = vkCreateInstance(&ci, nullptr, &g_vk.instance);
    if (r != VK_SUCCESS) {
        std::printf("nvn::bridge: vkCreateInstance failed (%d)\n", static_cast<int>(r));
        return false;
    }
    volkLoadInstance(g_vk.instance);
    return true;
}

bool pickPhysDevice() {
    std::uint32_t count = 0;
    vkEnumeratePhysicalDevices(g_vk.instance, &count, nullptr);
    if (count == 0) { std::puts("nvn::bridge: no Vulkan-capable GPU found"); return false; }

    std::vector<VkPhysicalDevice> devs(count);
    vkEnumeratePhysicalDevices(g_vk.instance, &count, devs.data());

    // Prefer a discrete GPU; otherwise take the first one with a graphics queue.
    VkPhysicalDevice chosen = VK_NULL_HANDLE;
    std::uint32_t chosenFam = UINT32_MAX;
    for (VkPhysicalDevice pd : devs) {
        std::uint32_t qCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(pd, &qCount, nullptr);
        std::vector<VkQueueFamilyProperties> qProps(qCount);
        vkGetPhysicalDeviceQueueFamilyProperties(pd, &qCount, qProps.data());
        for (std::uint32_t i = 0; i < qCount; ++i) {
            if ((qProps[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) == 0) continue;
            VkPhysicalDeviceProperties props{};
            vkGetPhysicalDeviceProperties(pd, &props);
            if (chosen == VK_NULL_HANDLE || props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
                chosen = pd;
                chosenFam = i;
            }
            break;
        }
    }
    if (chosen == VK_NULL_HANDLE) { std::puts("nvn::bridge: no GPU with a graphics queue"); return false; }

    g_vk.physDevice   = chosen;
    g_vk.graphicsQFam = chosenFam;
    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(chosen, &props);
    std::printf("nvn::bridge: GPU: %s\n", props.deviceName);
    return true;
}

bool createDevice() {
    const float prio = 1.0f;
    VkDeviceQueueCreateInfo qci{};
    qci.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qci.queueFamilyIndex = g_vk.graphicsQFam;
    qci.queueCount       = 1;
    qci.pQueuePriorities = &prio;

    const char *devExts[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};

    VkDeviceCreateInfo ci{};
    ci.sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    ci.queueCreateInfoCount    = 1;
    ci.pQueueCreateInfos       = &qci;
    ci.enabledExtensionCount   = 1;
    ci.ppEnabledExtensionNames = devExts;

    const VkResult r = vkCreateDevice(g_vk.physDevice, &ci, nullptr, &g_vk.device);
    if (r != VK_SUCCESS) {
        std::printf("nvn::bridge: vkCreateDevice failed (%d)\n", static_cast<int>(r));
        return false;
    }
    volkLoadDevice(g_vk.device);
    vkGetDeviceQueue(g_vk.device, g_vk.graphicsQFam, 0, &g_vk.graphicsQ);
    return true;
}

// Per-frame-slot objects; independent of the swapchain, so built once per surface.
bool createFrameResources() {
    VkCommandPoolCreateInfo cpci{};
    cpci.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cpci.queueFamilyIndex = g_vk.graphicsQFam;
    cpci.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    if (vkCreateCommandPool(g_vk.device, &cpci, nullptr, &g_vk.cmdPool) != VK_SUCCESS) return false;

    VkCommandBufferAllocateInfo cbai{};
    cbai.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cbai.commandPool        = g_vk.cmdPool;
    cbai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbai.commandBufferCount = kFramesInFlight;
    if (vkAllocateCommandBuffers(g_vk.device, &cbai, g_vk.cmdBufs) != VK_SUCCESS) return false;

    VkSemaphoreCreateInfo sci{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, nullptr, 0};
    VkFenceCreateInfo fci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, nullptr, VK_FENCE_CREATE_SIGNALED_BIT};
    for (std::uint32_t i = 0; i < kFramesInFlight; ++i) {
        if (vkCreateSemaphore(g_vk.device, &sci, nullptr, &g_vk.imgAvail[i]) != VK_SUCCESS) return false;
        if (vkCreateFence(g_vk.device, &fci, nullptr, &g_vk.inFlight[i]) != VK_SUCCESS) return false;
    }
    return true;
}

void destroySwapchainImages() {
    for (std::uint32_t i = 0; i < kMaxSwapImages; ++i) {
        if (g_vk.framebuffers[i]) vkDestroyFramebuffer(g_vk.device, g_vk.framebuffers[i], nullptr);
        if (g_vk.swapViews[i])    vkDestroyImageView(g_vk.device, g_vk.swapViews[i], nullptr);
        if (g_vk.renderDone[i])   vkDestroySemaphore(g_vk.device, g_vk.renderDone[i], nullptr);
        g_vk.framebuffers[i] = VK_NULL_HANDLE;
        g_vk.swapViews[i]    = VK_NULL_HANDLE;
        g_vk.renderDone[i]   = VK_NULL_HANDLE;
        g_vk.swapImages[i]   = VK_NULL_HANDLE;
    }
    g_vk.imageCount = 0;
}

// Builds (or rebuilds, after a resize) the swapchain at the window's current size.
// Returns false without error when the window is minimised (zero extent): the caller retries.
bool buildSwapchain(std::uint32_t fallbackW, std::uint32_t fallbackH) {
    VkSurfaceCapabilitiesKHR caps{};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(g_vk.physDevice, g_vk.surface, &caps);
    VkExtent2D extent = caps.currentExtent;
    if (extent.width == UINT32_MAX) extent = {fallbackW, fallbackH};
    if (extent.width == 0 || extent.height == 0) return false;

    if (g_vk.swapFormat == VK_FORMAT_UNDEFINED) {
        std::uint32_t fmtCount = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(g_vk.physDevice, g_vk.surface, &fmtCount, nullptr);
        std::vector<VkSurfaceFormatKHR> fmts(fmtCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(g_vk.physDevice, g_vk.surface, &fmtCount, fmts.data());
        if (fmts.empty()) return false;
        g_vk.swapFormat = fmts[0].format;
        g_vk.colorSpace = fmts[0].colorSpace;
        for (const auto &f : fmts) {
            if (f.format == VK_FORMAT_B8G8R8A8_SRGB) { g_vk.swapFormat = f.format; g_vk.colorSpace = f.colorSpace; break; }
        }
    }

    std::uint32_t imgCount = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && imgCount > caps.maxImageCount) imgCount = caps.maxImageCount;

    VkSwapchainKHR old = g_vk.swapchain;
    VkSwapchainCreateInfoKHR swci{};
    swci.sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swci.surface          = g_vk.surface;
    swci.minImageCount    = imgCount;
    swci.imageFormat      = g_vk.swapFormat;
    swci.imageColorSpace  = g_vk.colorSpace;
    swci.imageExtent      = extent;
    swci.imageArrayLayers = 1;
    swci.imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    swci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    swci.preTransform     = caps.currentTransform;
    swci.compositeAlpha   = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    swci.presentMode      = g_surface.vsync ? VK_PRESENT_MODE_FIFO_KHR : VK_PRESENT_MODE_IMMEDIATE_KHR;
    swci.clipped          = VK_TRUE;
    swci.oldSwapchain     = old;
    if (!g_surface.vsync) {
        // IMMEDIATE is optional; FIFO is the only mode every driver must offer.
        std::uint32_t n = 0;
        vkGetPhysicalDeviceSurfacePresentModesKHR(g_vk.physDevice, g_vk.surface, &n, nullptr);
        std::vector<VkPresentModeKHR> modes(n);
        vkGetPhysicalDeviceSurfacePresentModesKHR(g_vk.physDevice, g_vk.surface, &n, modes.data());
        bool found = false;
        for (VkPresentModeKHR m : modes) found |= (m == VK_PRESENT_MODE_IMMEDIATE_KHR);
        if (!found) swci.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    }

    const VkResult r = vkCreateSwapchainKHR(g_vk.device, &swci, nullptr, &g_vk.swapchain);
    destroySwapchainImages();
    if (old != VK_NULL_HANDLE) vkDestroySwapchainKHR(g_vk.device, old, nullptr);
    if (r != VK_SUCCESS) {
        g_vk.swapchain = VK_NULL_HANDLE;
        std::printf("nvn::bridge: vkCreateSwapchainKHR failed (%d)\n", static_cast<int>(r));
        return false;
    }
    g_vk.swapExtent = extent;

    if (g_vk.renderPass == VK_NULL_HANDLE) {
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
        if (vkCreateRenderPass(g_vk.device, &rpci, nullptr, &g_vk.renderPass) != VK_SUCCESS) return false;
    }

    vkGetSwapchainImagesKHR(g_vk.device, g_vk.swapchain, &g_vk.imageCount, nullptr);
    if (g_vk.imageCount > kMaxSwapImages) g_vk.imageCount = kMaxSwapImages;
    vkGetSwapchainImagesKHR(g_vk.device, g_vk.swapchain, &g_vk.imageCount, g_vk.swapImages);

    VkSemaphoreCreateInfo sci{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, nullptr, 0};
    for (std::uint32_t i = 0; i < g_vk.imageCount; ++i) {
        VkImageViewCreateInfo ivci{};
        ivci.sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        ivci.image            = g_vk.swapImages[i];
        ivci.viewType         = VK_IMAGE_VIEW_TYPE_2D;
        ivci.format           = g_vk.swapFormat;
        ivci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        if (vkCreateImageView(g_vk.device, &ivci, nullptr, &g_vk.swapViews[i]) != VK_SUCCESS) return false;

        VkFramebufferCreateInfo fbci{};
        fbci.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbci.renderPass      = g_vk.renderPass;
        fbci.attachmentCount = 1;
        fbci.pAttachments    = &g_vk.swapViews[i];
        fbci.width           = extent.width;
        fbci.height          = extent.height;
        fbci.layers          = 1;
        if (vkCreateFramebuffer(g_vk.device, &fbci, nullptr, &g_vk.framebuffers[i]) != VK_SUCCESS) return false;
        if (vkCreateSemaphore(g_vk.device, &sci, nullptr, &g_vk.renderDone[i]) != VK_SUCCESS) return false;
    }

    std::printf("nvn::bridge: swapchain %ux%u fmt=%d images=%u\n", extent.width, extent.height,
                static_cast<int>(g_vk.swapFormat), g_vk.imageCount);
    return true;
}

// Called when acquire/present report the swapchain out of date.
void rebuildSwapchain() {
    vkDeviceWaitIdle(g_vk.device);
    g_vk.needsRebuild = !buildSwapchain(g_surface.width, g_surface.height);
}

void destroySurfaceObjects() {
    if (g_vk.device == VK_NULL_HANDLE) return;
    vkDeviceWaitIdle(g_vk.device);
    destroySwapchainImages();
    for (std::uint32_t i = 0; i < kFramesInFlight; ++i) {
        if (g_vk.inFlight[i]) vkDestroyFence(g_vk.device, g_vk.inFlight[i], nullptr);
        if (g_vk.imgAvail[i]) vkDestroySemaphore(g_vk.device, g_vk.imgAvail[i], nullptr);
        g_vk.inFlight[i] = VK_NULL_HANDLE;
        g_vk.imgAvail[i] = VK_NULL_HANDLE;
        g_vk.cmdBufs[i]  = VK_NULL_HANDLE;
    }
    if (g_vk.cmdPool)    vkDestroyCommandPool(g_vk.device, g_vk.cmdPool, nullptr);
    if (g_vk.renderPass) vkDestroyRenderPass(g_vk.device, g_vk.renderPass, nullptr);
    if (g_vk.swapchain)  vkDestroySwapchainKHR(g_vk.device, g_vk.swapchain, nullptr);
    if (g_vk.surface)    vkDestroySurfaceKHR(g_vk.instance, g_vk.surface, nullptr);
    g_vk.cmdPool      = VK_NULL_HANDLE;
    g_vk.renderPass   = VK_NULL_HANDLE;
    g_vk.swapchain    = VK_NULL_HANDLE;
    g_vk.surface      = VK_NULL_HANDLE;
    g_vk.swapFormat   = VK_FORMAT_UNDEFINED;
    g_vk.window       = nullptr;
    g_vk.frameActive  = false;
    g_vk.needsRebuild = false;
    g_vkReady         = false;
}

#endif // NVN_BACKEND_VULKAN

} // namespace

// ---- lifecycle -----------------------------------------------------------

void Initialize() {
#if defined(NVN_BACKEND_VULKAN)
    Finalize(); // the boot path may run initializeGraphicsSystem more than once
    std::puts("nvn::bridge: Vulkan backend initializing...");
    if (!g_vk.loaderReady) {
        if (volkInitialize() != VK_SUCCESS) {
            std::puts("nvn::bridge: no Vulkan loader on this machine -- frames will not be shown");
            return;
        }
        g_vk.loaderReady = true;
    }
    if (!createInstance()) return;
    if (!pickPhysDevice()) return;
    if (!createDevice())   return;
    std::puts("nvn::bridge: Vulkan device ready");
#else
    std::puts("nvn::bridge: Stub backend active");
#endif
}

void Finalize() {
#if defined(NVN_BACKEND_VULKAN)
    destroySurfaceObjects();
    if (g_vk.device)   vkDestroyDevice(g_vk.device, nullptr);
    if (g_vk.instance) vkDestroyInstance(g_vk.instance, nullptr);
    const bool loader = g_vk.loaderReady;
    g_vk = VkState{};
    g_vk.loaderReady = loader;
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

static constexpr std::size_t kMaxProcs = 512;

#if !defined(NVN_BACKEND_VULKAN)
static void GenericStubProc() {
    g_nvnCalls.fetch_add(1, std::memory_order_relaxed);
}
#endif

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
#if !defined(NVN_BACKEND_VULKAN)
            procTable[i] = reinterpret_cast<void*>(&GenericStubProc);
#endif
            // Vulkan: NVN->Vulkan mappings will replace slots here as they are implemented.
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
                desc.width, desc.height, desc.backbufferCount, static_cast<int>(desc.vsync),
                desc.nativeWindowHandle);
#if defined(NVN_BACKEND_VULKAN)
    if (g_vk.device == VK_NULL_HANDLE) {
        std::puts("nvn::bridge: no Vulkan device -- surface not created");
        return false;
    }
    destroySurfaceObjects();
#  if defined(VK_USE_PLATFORM_WIN32_KHR)
    VkWin32SurfaceCreateInfoKHR sci{};
    sci.sType     = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    sci.hinstance = GetModuleHandleW(nullptr);
    sci.hwnd      = static_cast<HWND>(desc.nativeWindowHandle);
    if (sci.hwnd == nullptr || !IsWindow(sci.hwnd) ||
        vkCreateWin32SurfaceKHR(g_vk.instance, &sci, nullptr, &g_vk.surface) != VK_SUCCESS) {
        std::puts("nvn::bridge: no usable window -- surface not created");
        return false;
    }
#  else
    std::puts("nvn::bridge: no surface platform compiled in");
    return false;
#  endif
    VkBool32 canPresent = VK_FALSE;
    vkGetPhysicalDeviceSurfaceSupportKHR(g_vk.physDevice, g_vk.graphicsQFam, g_vk.surface, &canPresent);
    if (!canPresent) {
        std::puts("nvn::bridge: the graphics queue cannot present to this window");
        destroySurfaceObjects();
        return false;
    }
    g_vk.window = desc.nativeWindowHandle;
    if (!createFrameResources()) { destroySurfaceObjects(); return false; }
    g_vk.needsRebuild = !buildSwapchain(desc.width, desc.height);
    g_vkReady = true;
#endif
    return true;
}

void DestroySurface() {
#if defined(NVN_BACKEND_VULKAN)
    destroySurfaceObjects();
#endif
    g_surfaceCreated = false;
}

bool IsPresenting() {
#if defined(NVN_BACKEND_VULKAN)
    return g_vkReady;
#else
    return false;
#endif
}

// ---- frame boundary ------------------------------------------------------

void BeginFrame() {
#if defined(NVN_BACKEND_VULKAN)
    g_vk.frameActive = false;
    if (!g_vkReady) return;
    if (g_vk.needsRebuild) {
        rebuildSwapchain();
        if (g_vk.needsRebuild) return; // still minimised
    }

    const std::uint32_t slot = static_cast<std::uint32_t>(g_vk.frameIdx % kFramesInFlight);
    vkWaitForFences(g_vk.device, 1, &g_vk.inFlight[slot], VK_TRUE, UINT64_MAX);

    const VkResult acq = vkAcquireNextImageKHR(g_vk.device, g_vk.swapchain, UINT64_MAX,
                                               g_vk.imgAvail[slot], VK_NULL_HANDLE, &g_vk.currentImage);
    if (acq == VK_ERROR_OUT_OF_DATE_KHR) { g_vk.needsRebuild = true; return; }
    if (acq != VK_SUCCESS && acq != VK_SUBOPTIMAL_KHR) return;

    // Only reset once an image is acquired, so a skipped frame never leaves the fence
    // unsignalled for the next wait.
    vkResetFences(g_vk.device, 1, &g_vk.inFlight[slot]);
    g_vk.frameSlot = slot;

    VkCommandBuffer cb = g_vk.cmdBufs[slot];
    vkResetCommandBuffer(cb, 0);
    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cb, &begin);

    VkClearValue clearVal{};
    clearVal.color.float32[0] = 0.00f; // Splatoon teal
    clearVal.color.float32[1] = 0.18f;
    clearVal.color.float32[2] = 0.28f;
    clearVal.color.float32[3] = 1.00f;

    VkRenderPassBeginInfo rp{};
    rp.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rp.renderPass        = g_vk.renderPass;
    rp.framebuffer       = g_vk.framebuffers[g_vk.currentImage];
    rp.renderArea.extent = g_vk.swapExtent;
    rp.clearValueCount   = 1;
    rp.pClearValues      = &clearVal;
    vkCmdBeginRenderPass(cb, &rp, VK_SUBPASS_CONTENTS_INLINE);
    // NVN draw calls will be recorded here as InjectProcs gains Vulkan-backed procs.
    vkCmdEndRenderPass(cb);
    vkEndCommandBuffer(cb);
    g_vk.frameActive = true;
#endif
}

void EndFrame() {
    g_frames.fetch_add(1, std::memory_order_relaxed);
    g_drawCalls.store(0, std::memory_order_relaxed);
#if defined(NVN_BACKEND_VULKAN)
    if (!g_vkReady || !g_vk.frameActive) return;
    g_vk.frameActive = false;

    const std::uint32_t slot = g_vk.frameSlot;
    VkCommandBuffer cb = g_vk.cmdBufs[slot];

    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submit{};
    submit.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.waitSemaphoreCount   = 1;
    submit.pWaitSemaphores      = &g_vk.imgAvail[slot];
    submit.pWaitDstStageMask    = &waitStage;
    submit.commandBufferCount   = 1;
    submit.pCommandBuffers      = &cb;
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores    = &g_vk.renderDone[g_vk.currentImage];
    vkQueueSubmit(g_vk.graphicsQ, 1, &submit, g_vk.inFlight[slot]);

    VkPresentInfoKHR present{};
    present.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores    = &g_vk.renderDone[g_vk.currentImage];
    present.swapchainCount     = 1;
    present.pSwapchains        = &g_vk.swapchain;
    present.pImageIndices      = &g_vk.currentImage;
    const VkResult pr = vkQueuePresentKHR(g_vk.graphicsQ, &present);
    if (pr == VK_ERROR_OUT_OF_DATE_KHR || pr == VK_SUBOPTIMAL_KHR) g_vk.needsRebuild = true;

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
