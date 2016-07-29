#include <algorithm>
#include <cassert>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

#define SDL_MAIN_HANDLED
#include "sdl/SDL.h"
#include "sdl/SDL_syswm.h"

#include "common.h"
#include "device_initialization.h"
#include "swapchain_initialization.h"
#include "vulkan_definitions.h"

enum
{
    window_width = 640,
    window_height = 480,
};

struct VulkanState
{
    VkInstance instance;
    VkDevice device;
    VkQueue graphicsQueue;
    VkQueue presentQueue;
    VkSurfaceKHR surface;
    SwapchainInfo swapchainInfo;
    VkSemaphore imageAvailableSemaphore;
    VkSemaphore renderingFinishedSemaphore;
    VkCommandPool presentQueueCommandPool;
    std::vector<VkCommandBuffer> presentQueueCommandBuffers;

    VulkanState()
        : instance(VK_NULL_HANDLE)
        , device(VK_NULL_HANDLE)
        , graphicsQueue(VK_NULL_HANDLE)
        , presentQueue(VK_NULL_HANDLE)
        , surface(VK_NULL_HANDLE)
        , imageAvailableSemaphore(VK_NULL_HANDLE)
        , renderingFinishedSemaphore(VK_NULL_HANDLE)
        , presentQueueCommandPool(VK_NULL_HANDLE)
    {}
};

VulkanState vulkanState;

VkRenderPass renderPass = VK_NULL_HANDLE;
std::vector<VkFramebuffer> framebuffers;

VkSurfaceKHR CreateSurface(VkInstance instance, HWND hwnd)
{
    VkWin32SurfaceCreateInfoKHR surfaceCreateInfo;
    surfaceCreateInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    surfaceCreateInfo.pNext = nullptr;
    surfaceCreateInfo.flags = 0;
    surfaceCreateInfo.hinstance = ::GetModuleHandle(nullptr);
    surfaceCreateInfo.hwnd = hwnd;

    VkSurfaceKHR surface;
    VkResult result = vkCreateWin32SurfaceKHR(instance, &surfaceCreateInfo, nullptr, &surface);
    CheckVkResult(result, "vkCreateWin32SurfaceKHR");
    return surface;
}

VkRenderPass CreateRenderPass()
{
    VkAttachmentDescription attachmentDescription;
    attachmentDescription.flags = 0;
    attachmentDescription.format = vulkanState.swapchainInfo.imageFormat;
    attachmentDescription.samples = VK_SAMPLE_COUNT_1_BIT;
    attachmentDescription.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachmentDescription.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachmentDescription.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachmentDescription.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachmentDescription.initialLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    attachmentDescription.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference attachmentReference;
    attachmentReference.attachment = 0;
    attachmentReference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpassDescription;
    subpassDescription.flags = 0;
    subpassDescription.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpassDescription.inputAttachmentCount = 0;
    subpassDescription.pInputAttachments = nullptr;
    subpassDescription.colorAttachmentCount = 1;
    subpassDescription.pColorAttachments = &attachmentReference;
    subpassDescription.pResolveAttachments = nullptr;
    subpassDescription.pDepthStencilAttachment = nullptr;
    subpassDescription.preserveAttachmentCount = 0;
    subpassDescription.pPreserveAttachments = nullptr;

    VkRenderPassCreateInfo renderPassCreateInfo;
    renderPassCreateInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassCreateInfo.pNext = nullptr;
    renderPassCreateInfo.flags = 0;
    renderPassCreateInfo.attachmentCount = 1;
    renderPassCreateInfo.pAttachments = &attachmentDescription;
    renderPassCreateInfo.subpassCount = 1;
    renderPassCreateInfo.pSubpasses = &subpassDescription;
    renderPassCreateInfo.dependencyCount = 0;
    renderPassCreateInfo.pDependencies = nullptr;

    VkRenderPass renderPass;
    VkResult result = vkCreateRenderPass(vulkanState.device, &renderPassCreateInfo, nullptr, &renderPass);
    CheckVkResult(result, "vkCreateRenderPass");
    return renderPass;
}

void CleanupVulkanResources()
{
    VkResult result = vkDeviceWaitIdle(vulkanState.device);
    CheckVkResult(result, "vkDeviceWaitIdle");

    for (size_t i = 0; i < framebuffers.size(); i++)
    {
        vkDestroyFramebuffer(vulkanState.device, framebuffers[i], nullptr);
    }
    framebuffers.clear();

    auto& swapchainImageViews = vulkanState.swapchainInfo.imageViews;
    for (size_t i = 0; i < swapchainImageViews.size(); i++)
    {
        vkDestroyImageView(vulkanState.device, swapchainImageViews[i], nullptr);
    }
    swapchainImageViews.clear();

    vkDestroyRenderPass(vulkanState.device, renderPass, nullptr);
    renderPass = VK_NULL_HANDLE;

    vkDestroySemaphore(vulkanState.device, vulkanState.imageAvailableSemaphore, nullptr);
    vulkanState.imageAvailableSemaphore = VK_NULL_HANDLE;

    vkDestroySemaphore(vulkanState.device, vulkanState.renderingFinishedSemaphore, nullptr);
    vulkanState.renderingFinishedSemaphore = VK_NULL_HANDLE;

    vkDestroyCommandPool(vulkanState.device, vulkanState.presentQueueCommandPool, nullptr);
    vulkanState.presentQueueCommandPool = VK_NULL_HANDLE;

    vkDestroySwapchainKHR(vulkanState.device, vulkanState.swapchainInfo.handle, nullptr);
    vulkanState.swapchainInfo = SwapchainInfo();

    vkDestroySurfaceKHR(vulkanState.instance, vulkanState.surface, nullptr);
    vulkanState.surface = VK_NULL_HANDLE;

    vkDestroyDevice(vulkanState.device, nullptr);
    vulkanState.device = VK_NULL_HANDLE;

    vkDestroyInstance(vulkanState.instance, nullptr);
    vulkanState.instance = VK_NULL_HANDLE;
}

void RunFrame()
{
    uint32_t imageIndex;

    VkResult result = vkAcquireNextImageKHR(
        vulkanState.device,
        vulkanState.swapchainInfo.handle,
        UINT64_MAX,
        vulkanState.imageAvailableSemaphore,
        VK_NULL_HANDLE,
        &imageIndex);

    CheckVkResult(result, "vkAcquireNextImageKHR");

    VkPipelineStageFlags waitDstStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;

    VkSubmitInfo submitInfo;
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.pNext = nullptr;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = &vulkanState.imageAvailableSemaphore;
    submitInfo.pWaitDstStageMask = &waitDstStageMask;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &vulkanState.presentQueueCommandBuffers[imageIndex];
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &vulkanState.renderingFinishedSemaphore;

    result = vkQueueSubmit(vulkanState.presentQueue, 1, &submitInfo, VK_NULL_HANDLE);
    CheckVkResult(result, "vkQueueSubmit");

    VkPresentInfoKHR presentInfo;
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.pNext = nullptr;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &vulkanState.renderingFinishedSemaphore;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &vulkanState.swapchainInfo.handle;
    presentInfo.pImageIndices = &imageIndex;
    presentInfo.pResults = nullptr;

    result = vkQueuePresentKHR(vulkanState.presentQueue, &presentInfo);
    CheckVkResult(result, "vkQueuePresentKHR");
}

void RunMainLoop()
{
    SDL_Event event;
    bool running = true;
    while (running)
    {
        while (SDL_PollEvent(&event))
        {
            if (event.type == SDL_QUIT)
                running = false;
        }
        if (running)
        {
            RunFrame();
            SDL_Delay(1);
        }
    }
}

int main()
{
    // create SDL window
    if (SDL_Init(SDL_INIT_VIDEO) != 0)
        Error("SDL_Init error");

    SDL_Window* window = SDL_CreateWindow("Vulkan app", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
        window_width, window_height, SDL_WINDOW_SHOWN);
    if (window == nullptr)
    {
        SDL_Quit();
        Error("failed to create SDL window");
    }

    SDL_SysWMinfo wmInfo;
    SDL_VERSION(&wmInfo.version)
        if (SDL_GetWindowWMInfo(window, &wmInfo) == SDL_FALSE)
        {
            SDL_Quit();
            Error("failed to gt platform specific window information");
        }

    VkResult result;

    vulkanState.instance = CreateInstance();
    VkPhysicalDevice physicalDevice = SelectPhysicalDevice(vulkanState.instance);

    vulkanState.surface = CreateSurface(vulkanState.instance, wmInfo.info.win.window);;

    uint32_t graphicsQueueFamilyIndex;
    uint32_t presentationQueueFamilyIndex;

    if (!SelectQueueFamilies(physicalDevice, vulkanState.surface,
        graphicsQueueFamilyIndex, presentationQueueFamilyIndex))
    {
        Error("failed to find matching queue families");
    }

    std::vector<QueueInfo> queueInfos;
    queueInfos.push_back(QueueInfo(graphicsQueueFamilyIndex, 1));
    if (presentationQueueFamilyIndex != graphicsQueueFamilyIndex)
        queueInfos.push_back(QueueInfo(presentationQueueFamilyIndex, 1));

    vulkanState.device = CreateDevice(physicalDevice, queueInfos);

    vkGetDeviceQueue(vulkanState.device, graphicsQueueFamilyIndex, 0, &vulkanState.graphicsQueue);
    vkGetDeviceQueue(vulkanState.device, presentationQueueFamilyIndex, 0, &vulkanState.presentQueue);

    vulkanState.swapchainInfo = CreateSwapchain(physicalDevice, vulkanState.device, vulkanState.surface);

    uint32_t imagesCount = static_cast<uint32_t>(vulkanState.swapchainInfo.images.size());

    renderPass = CreateRenderPass();

    framebuffers.resize(imagesCount);
    for (uint32_t i = 0; i < imagesCount; i++)
    {
        VkFramebufferCreateInfo framebufferCreateInfo;
        framebufferCreateInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferCreateInfo.pNext = nullptr;
        framebufferCreateInfo.flags = 0;
        framebufferCreateInfo.renderPass = renderPass;
        framebufferCreateInfo.attachmentCount = 1;
        framebufferCreateInfo.pAttachments = &vulkanState.swapchainInfo.imageViews[i];
        framebufferCreateInfo.width = window_width;
        framebufferCreateInfo.height = window_height;
        framebufferCreateInfo.layers = 1;

        result = vkCreateFramebuffer(vulkanState.device, &framebufferCreateInfo, nullptr, &framebuffers[i]);
        CheckVkResult(result, "vkCreateFramebuffer");
    }

    VkSemaphoreCreateInfo semaphoreCreateInfo;
    semaphoreCreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    semaphoreCreateInfo.pNext = nullptr;
    semaphoreCreateInfo.flags = 0;

    result = vkCreateSemaphore(vulkanState.device, &semaphoreCreateInfo, nullptr, &vulkanState.imageAvailableSemaphore);
    CheckVkResult(result, "vkCreateSemaphore");

    result = vkCreateSemaphore(vulkanState.device, &semaphoreCreateInfo, nullptr, &vulkanState.renderingFinishedSemaphore);
    CheckVkResult(result, "vkCreateSemaphore");

    VkCommandPoolCreateInfo commandPoolCreateInfo;
    commandPoolCreateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    commandPoolCreateInfo.pNext = nullptr;
    commandPoolCreateInfo.flags = 0;
    commandPoolCreateInfo.queueFamilyIndex = presentationQueueFamilyIndex;

    result = vkCreateCommandPool(vulkanState.device, &commandPoolCreateInfo, nullptr, &vulkanState.presentQueueCommandPool);
    CheckVkResult(result, "vkCreateCommandPool");

    vulkanState.presentQueueCommandBuffers.resize(imagesCount);

    VkCommandBufferAllocateInfo commandBufferAllocateInfo;
    commandBufferAllocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    commandBufferAllocateInfo.pNext = nullptr;
    commandBufferAllocateInfo.commandPool = vulkanState.presentQueueCommandPool;
    commandBufferAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    commandBufferAllocateInfo.commandBufferCount = imagesCount;

    result = vkAllocateCommandBuffers(vulkanState.device, &commandBufferAllocateInfo, vulkanState.presentQueueCommandBuffers.data());
    CheckVkResult(result, "vkAllocateCommandBuffers");

    VkCommandBufferBeginInfo commandBufferBeginInfo;
    commandBufferBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    commandBufferBeginInfo.pNext = nullptr;
    commandBufferBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
    commandBufferBeginInfo.pInheritanceInfo = nullptr;

    VkClearColorValue clearColor = {1.0f, 0.8f, 0.4f, 0.0f};

    VkImageSubresourceRange imageSubresourceRange;
    imageSubresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    imageSubresourceRange.baseMipLevel = 0;
    imageSubresourceRange.levelCount = 1;
    imageSubresourceRange.baseArrayLayer = 0;
    imageSubresourceRange.layerCount = 1;

    for (uint32_t i = 0; i < imagesCount; ++i)
    {
        VkImageMemoryBarrier barrierFromPresentToClear;
        barrierFromPresentToClear.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrierFromPresentToClear.pNext = nullptr;
        barrierFromPresentToClear.srcAccessMask = 0;
        barrierFromPresentToClear.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrierFromPresentToClear.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrierFromPresentToClear.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrierFromPresentToClear.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrierFromPresentToClear.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrierFromPresentToClear.image = vulkanState.swapchainInfo.images[i];
        barrierFromPresentToClear.subresourceRange = imageSubresourceRange;

        VkImageMemoryBarrier barrierFromClearToPresent;
        barrierFromClearToPresent.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrierFromClearToPresent.pNext = nullptr;
        barrierFromClearToPresent.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrierFromClearToPresent.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
        barrierFromClearToPresent.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrierFromClearToPresent.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        barrierFromClearToPresent.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrierFromClearToPresent.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrierFromClearToPresent.image = vulkanState.swapchainInfo.images[i];
        barrierFromClearToPresent.subresourceRange = imageSubresourceRange;

        vkBeginCommandBuffer(vulkanState.presentQueueCommandBuffers[i], &commandBufferBeginInfo);
        vkCmdPipelineBarrier(
            vulkanState.presentQueueCommandBuffers[i],
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            0,
            0,
            nullptr,
            0,
            nullptr,
            1,
            &barrierFromPresentToClear
            );

        vkCmdClearColorImage(vulkanState.presentQueueCommandBuffers[i], vulkanState.swapchainInfo.images[i],
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearColor, 1, &imageSubresourceRange);

        vkCmdPipelineBarrier(
            vulkanState.presentQueueCommandBuffers[i],
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
            0,
            0,
            nullptr,
            0,
            nullptr,
            1,
            &barrierFromClearToPresent);

        result = vkEndCommandBuffer(vulkanState.presentQueueCommandBuffers[i]);
        CheckVkResult(result, "vkEndCommandBuffer");
    }

    RunMainLoop();
    CleanupVulkanResources();
    SDL_Quit();
    return 0;
}
