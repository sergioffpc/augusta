#ifndef AUGUSTA_WINDOW_VULKAN_H_
#define AUGUSTA_WINDOW_VULKAN_H_

#include <span>

#include <vulkan/vulkan_core.h>

#include "augusta/window.h"

/// \file
/// What the Renderer's Vulkan device needs of the Window (ADR-0059): the
/// system's Vulkan loader and the window's Vulkan surface. Apart from window.h
/// so that only Vulkan code sees Vulkan's header: renderer.h, which includes
/// window.h, is built where the client and Vulkan are not.
namespace augusta::window {

/// The system's Vulkan loader, found at runtime, so the client links and ships
/// none: its vkGetInstanceProcAddr, or null if there is no loader.
[[nodiscard]] PFN_vkGetInstanceProcAddr VulkanLoader(const Window& window);

/// The instance extensions a Vulkan instance needs to present to window; empty
/// if the loader can't present to a window.
[[nodiscard]] std::span<const char* const> VulkanInstanceExtensions(const Window& window);

/// Makes surface, window's Vulkan surface, through instance, which was made
/// with VulkanInstanceExtensions. The caller destroys it, before instance and
/// window. Returns Vulkan's result; surface is set only on VK_SUCCESS.
[[nodiscard]] VkResult CreateVulkanSurface(const Window& window, VkInstance instance, VkSurfaceKHR& surface);

}  // namespace augusta::window

#endif  // AUGUSTA_WINDOW_VULKAN_H_
