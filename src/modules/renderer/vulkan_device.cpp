#include "vulkan_device.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <format>
#include <mutex>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <GLFW/glfw3.h>
#include <nvrhi/nvrhi.h>
#include <nvrhi/vulkan.h>
#include <vulkan/vulkan.hpp>

#include "augusta/logging.h"
#include "augusta/renderer.h"
#include "device_choice.h"
#include "gpu_crash_dumps.h"

// Vulkan-Hpp's dispatcher, which this module and NVRHI's Vulkan backend both
// call Vulkan through, once LoadVulkan has pointed it at the system's loader.
VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

namespace augusta::renderer {

namespace {

// Dynamic rendering and synchronization2, which NVRHI draws with, are core
// from Vulkan 1.3 on.
constexpr std::uint32_t kVulkanVersion = VK_API_VERSION_1_3;

constexpr std::string_view kValidationLayer = "VK_LAYER_KHRONOS_validation";

// Points the dispatcher at the loader GLFW found, the system's.
void LoadVulkan() {
  if (glfwVulkanSupported() != GLFW_TRUE) {
    throw std::runtime_error("No Vulkan loader found: the client needs a GPU driver with Vulkan 1.3");
  }
  VULKAN_HPP_DEFAULT_DISPATCHER.init(
      reinterpret_cast<PFN_vkGetInstanceProcAddr>(glfwGetInstanceProcAddress(nullptr, "vkGetInstanceProcAddr")));
  // Vulkan 1.0's loader has no vkEnumerateInstanceVersion.
  if (VULKAN_HPP_DEFAULT_DISPATCHER.vkEnumerateInstanceVersion == nullptr ||
      vk::enumerateInstanceVersion() < kVulkanVersion) {
    throw std::runtime_error("The system's Vulkan loader is older than Vulkan 1.3, which the client needs");
  }
}

bool HasValidationLayer() {
  return std::ranges::any_of(vk::enumerateInstanceLayerProperties(), [](const vk::LayerProperties& layer) {
    return std::string_view(layer.layerName.data()) == kValidationLayer;
  });
}

// Whether the instance loads the validation layer, as validation asks, given
// whether the loader finds it. Throws std::runtime_error if it is required and
// not found.
bool LoadsValidationLayer(Validation validation, bool layer_found) {
  switch (validation) {
    case Validation::kOff:
      return false;
    case Validation::kIfAvailable:
      if (!layer_found) {
        LOG_WARNING(
            "subsystem=renderer event=vulkan_validation_unavailable layer={} hint=\"add the folder of its JSON "
            "manifest "
            "to VK_ADD_LAYER_PATH\"",
            kValidationLayer);
      }
      return layer_found;
    case Validation::kRequired:
      if (!layer_found) {
        throw std::runtime_error(std::format(
            "Vulkan validation is required, but the loader finds no {}: add the folder of its JSON manifest, such as "
            "vcpkg's installed bin, to VK_ADD_LAYER_PATH",
            kValidationLayer));
      }
      return true;
  }
  return false;
}

DeviceType TypeOf(vk::PhysicalDeviceType type) {
  switch (type) {
    case vk::PhysicalDeviceType::eDiscreteGpu:
      return DeviceType::kDiscreteGpu;
    case vk::PhysicalDeviceType::eIntegratedGpu:
      return DeviceType::kIntegratedGpu;
    case vk::PhysicalDeviceType::eCpu:
      return DeviceType::kCpu;
    default:
      return DeviceType::kOther;
  }
}

// The first of device's queue families that draws and presents to surface.
std::optional<std::uint32_t> GraphicsQueueFamily(vk::PhysicalDevice device, vk::SurfaceKHR surface) {
  const std::vector<vk::QueueFamilyProperties> families = device.getQueueFamilyProperties();
  for (std::uint32_t i = 0; i < families.size(); ++i) {
    if ((families[i].queueFlags & vk::QueueFlagBits::eGraphics) && device.getSurfaceSupportKHR(i, surface) != 0U) {
      return i;
    }
  }
  return std::nullopt;
}

DeviceCandidate CandidateOf(vk::PhysicalDevice device, vk::SurfaceKHR surface) {
  const vk::PhysicalDeviceProperties properties = device.getProperties();
  return {.type = TypeOf(properties.deviceType),
          .supports_vulkan_1_3 = properties.apiVersion >= kVulkanVersion,
          .presents = GraphicsQueueFamily(device, surface).has_value()};
}

// Each device's name, type and Vulkan version, for an error saying none would
// do.
std::string Describe(std::span<const vk::PhysicalDevice> devices) {
  if (devices.empty()) {
    return "none";
  }
  std::string described;
  for (const vk::PhysicalDevice device : devices) {
    const vk::PhysicalDeviceProperties properties = device.getProperties();
    described += std::format("{}{} ({}, Vulkan {}.{})", described.empty() ? "" : ", ", properties.deviceName.data(),
                             vk::to_string(properties.deviceType), VK_API_VERSION_MAJOR(properties.apiVersion),
                             VK_API_VERSION_MINOR(properties.apiVersion));
  }
  return described;
}

// The extensions device is made with (device_choice.h), of those it has.
std::vector<const char*> ExtensionsFor(vk::PhysicalDevice device, bool gpu_crash_dumps) {
  const std::vector<vk::ExtensionProperties> properties = device.enumerateDeviceExtensionProperties();
  std::vector<std::string_view> available;
  available.reserve(properties.size());
  for (const vk::ExtensionProperties& extension : properties) {
    available.emplace_back(extension.extensionName.data());
  }
  return DeviceExtensions(gpu_crash_dumps, available);
}

// Whether extensions has the diagnostics config extension, and device the
// feature it brings, which Aftermath's settings for the device need.
bool HasDiagnosticsConfig(vk::PhysicalDevice device, std::span<const char* const> extensions) {
  if (!std::ranges::contains(extensions, std::string_view(kDiagnosticsConfigExtension),
                             [](const char* name) { return std::string_view(name); })) {
    return false;
  }
  const auto features =
      device.getFeatures2<vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceDiagnosticsConfigFeaturesNV>();
  return features.get<vk::PhysicalDeviceDiagnosticsConfigFeaturesNV>().diagnosticsConfig == vk::True;
}

std::string_view Describe(Adapter adapter) {
  return adapter == Adapter::kSoftware ? "CPU device" : "discrete or integrated GPU";
}

}  // namespace

VulkanDevice::VulkanDevice(GLFWwindow& window, Adapter adapter, Validation validation) {
  // Aftermath watches only the devices made after it starts.
  const bool gpu_crash_dumps = EnableGpuCrashDumps();
  LoadVulkan();
  CreateInstance(validation);
  VkSurfaceKHR surface = VK_NULL_HANDLE;
  if (const VkResult result = glfwCreateWindowSurface(*instance_, &window, nullptr, &surface); result != VK_SUCCESS) {
    throw std::runtime_error(
        std::format("Creating the window's Vulkan surface failed: {}", nvrhi::vulkan::resultToString(result)));
  }
  surface_ = vk::UniqueSurfaceKHR(surface, *instance_);
  ChoosePhysicalDevice(adapter);
  CreateDevice(gpu_crash_dumps);
  ThrowValidationErrors();
}

VulkanDevice::~VulkanDevice() { nvrhi_->waitForIdle(); }

void VulkanDevice::ThrowValidationErrors() {
  std::optional<std::string> error;
  {
    const std::lock_guard<std::mutex> lock(validation_mutex_);
    error.swap(validation_error_);
  }
  if (error) {
    throw std::runtime_error("Vulkan validation: " + *error);
  }
}

void VulkanDevice::NvrhiMessages::message(nvrhi::MessageSeverity severity, const char* text) {
  switch (severity) {
    case nvrhi::MessageSeverity::Info:
      LOG_DEBUG("subsystem=renderer event=nvrhi_message message=\"{}\"", text);
      return;
    case nvrhi::MessageSeverity::Warning:
      LOG_WARNING("subsystem=renderer event=nvrhi_message message=\"{}\"", text);
      return;
    case nvrhi::MessageSeverity::Error:
      LOG_ERROR("subsystem=renderer event=nvrhi_message message=\"{}\"", text);
      return;
    case nvrhi::MessageSeverity::Fatal:
      LOG_CRITICAL("subsystem=renderer event=nvrhi_message message=\"{}\"", text);
      return;
  }
}

VKAPI_ATTR vk::Bool32 VKAPI_CALL VulkanDevice::OnValidationMessage(vk::DebugUtilsMessageSeverityFlagBitsEXT severity,
                                                                   vk::DebugUtilsMessageTypeFlagsEXT /*types*/,
                                                                   const vk::DebugUtilsMessengerCallbackDataEXT* data,
                                                                   void* user_data) {
  if (severity != vk::DebugUtilsMessageSeverityFlagBitsEXT::eError) {
    LOG_WARNING("subsystem=renderer event=vulkan_validation_warning message=\"{}\"", data->pMessage);
    return vk::False;
  }
  // Warning, not error: the Renderer throws it, and the boundary that catches
  // it writes the one ERR line (ADR-0033).
  LOG_WARNING("subsystem=renderer event=vulkan_validation_error message=\"{}\"", data->pMessage);
  auto& device = *static_cast<VulkanDevice*>(user_data);
  const std::lock_guard<std::mutex> lock(device.validation_mutex_);
  if (!device.validation_error_) {
    device.validation_error_ = data->pMessage;
  }
  return vk::False;
}

void VulkanDevice::CreateInstance(Validation requested) {
  std::uint32_t window_extension_count = 0;
  const char** window_extensions = glfwGetRequiredInstanceExtensions(&window_extension_count);
  if (window_extensions == nullptr) {
    throw std::runtime_error("The system's Vulkan loader can't present to a window");
  }
  instance_extensions_.assign(window_extensions, window_extensions + window_extension_count);
  const bool validation = LoadsValidationLayer(requested, requested != Validation::kOff && HasValidationLayer());
  std::vector<const char*> layers;
  if (validation) {
    layers.push_back(kValidationLayer.data());
    instance_extensions_.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
  }

  const vk::ApplicationInfo application =
      vk::ApplicationInfo().setPApplicationName("augusta").setPEngineName("augusta").setApiVersion(kVulkanVersion);
  // Warnings and errors only, of every type.
  const vk::DebugUtilsMessengerCreateInfoEXT messenger =
      vk::DebugUtilsMessengerCreateInfoEXT()
          .setMessageSeverity(vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning |
                              vk::DebugUtilsMessageSeverityFlagBitsEXT::eError)
          .setMessageType(vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral |
                          vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation |
                          vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance)
          .setPfnUserCallback(&VulkanDevice::OnValidationMessage)
          .setPUserData(this);
  vk::InstanceCreateInfo info = vk::InstanceCreateInfo()
                                    .setPApplicationInfo(&application)
                                    .setPEnabledLayerNames(layers)
                                    .setPEnabledExtensionNames(instance_extensions_);
  // Chained too, so that creating the instance is validated as well.
  if (validation) {
    info.setPNext(&messenger);
  }
  instance_ = vk::createInstanceUnique(info);
  VULKAN_HPP_DEFAULT_DISPATCHER.init(*instance_);
  if (validation) {
    messenger_ = instance_->createDebugUtilsMessengerEXTUnique(messenger);
  }
}

void VulkanDevice::ChoosePhysicalDevice(Adapter adapter) {
  const std::vector<vk::PhysicalDevice> devices = instance_->enumeratePhysicalDevices();
  std::vector<DeviceCandidate> candidates;
  candidates.reserve(devices.size());
  for (const vk::PhysicalDevice device : devices) {
    candidates.push_back(CandidateOf(device, *surface_));
  }
  const std::optional<std::size_t> chosen = ChooseDevice(candidates, adapter);
  if (!chosen) {
    throw std::runtime_error(std::format("No {} with Vulkan 1.3 that can present to the window; Vulkan's devices: {}",
                                         Describe(adapter), Describe(devices)));
  }
  physical_device_ = devices[*chosen];
  queue_family_ = GraphicsQueueFamily(physical_device_, *surface_).value();
  LOG_INFO("subsystem=renderer event=vulkan_device name=\"{}\"", physical_device_.getProperties().deviceName.data());
}

void VulkanDevice::CreateDevice(bool gpu_crash_dumps) {
  device_extensions_ = ExtensionsFor(physical_device_, gpu_crash_dumps);
  const float priority = 1.0F;
  const vk::DeviceQueueCreateInfo queue =
      vk::DeviceQueueCreateInfo().setQueueFamilyIndex(queue_family_).setQueuePriorities(priority);
  // What NVRHI's Vulkan backend draws and synchronizes with, all core in 1.3.
  vk::PhysicalDeviceVulkan13Features features13 =
      vk::PhysicalDeviceVulkan13Features().setDynamicRendering(vk::True).setSynchronization2(vk::True);
  vk::PhysicalDeviceVulkan12Features features12 =
      vk::PhysicalDeviceVulkan12Features().setTimelineSemaphore(vk::True).setPNext(&features13);
  // What Aftermath puts in a dump: the resources a faulting address belongs to,
  // and a checkpoint after each command, with the call stack that recorded it,
  // which VK_NV_device_diagnostic_checkpoints carries.
  vk::DeviceDiagnosticsConfigCreateInfoNV diagnostics = vk::DeviceDiagnosticsConfigCreateInfoNV().setFlags(
      vk::DeviceDiagnosticsConfigFlagBitsNV::eEnableResourceTracking |
      vk::DeviceDiagnosticsConfigFlagBitsNV::eEnableAutomaticCheckpoints);
  vk::PhysicalDeviceDiagnosticsConfigFeaturesNV diagnostics_feature =
      vk::PhysicalDeviceDiagnosticsConfigFeaturesNV().setDiagnosticsConfig(vk::True).setPNext(&diagnostics);
  if (HasDiagnosticsConfig(physical_device_, device_extensions_)) {
    features13.setPNext(&diagnostics_feature);
    LOG_INFO("subsystem=renderer event=gpu_crash_dumps_attached");
  }
  device_ = physical_device_.createDeviceUnique(vk::DeviceCreateInfo()
                                                    .setQueueCreateInfos(queue)
                                                    .setPEnabledExtensionNames(device_extensions_)
                                                    .setPNext(&features12));
  VULKAN_HPP_DEFAULT_DISPATCHER.init(*device_);
  queue_ = device_->getQueue(queue_family_, 0);

  nvrhi::vulkan::DeviceDesc desc;
  desc.errorCB = &messages_;
  desc.instance = *instance_;
  desc.physicalDevice = physical_device_;
  desc.device = *device_;
  desc.graphicsQueue = queue_;
  desc.graphicsQueueIndex = static_cast<int>(queue_family_);
  desc.instanceExtensions = instance_extensions_.data();
  desc.numInstanceExtensions = instance_extensions_.size();
  desc.deviceExtensions = device_extensions_.data();
  desc.numDeviceExtensions = device_extensions_.size();
  nvrhi_ = nvrhi::vulkan::createDevice(desc);
  if (nvrhi_ == nullptr) {
    throw std::runtime_error("Creating NVRHI's Vulkan device failed");
  }
}

}  // namespace augusta::renderer
