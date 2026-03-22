#include <iostream>
#include <vulkan/vulkan.h>
#define VK_NO_PROTOTYPES
#define VMA_VULKAN_VERSION 1003000 // Vulkan 1.3
#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>
#define VKB_DISABLE_ENHANCED_MODE
#include <VkBootstrap.h>
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

int main() {
    std::cout << "Hello, World!" << std::endl;

    // Initialize GLFW
    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW" << std::endl;
        return -1;
    }

    // Create a windowed mode window and its OpenGL context
    GLFWwindow* window = glfwCreateWindow(640, 480, "Hello World", NULL, NULL);
    if (!window) {
        std::cerr << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return -1;
    }

    // Make the window's context current
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    // Initialize Vulkan and VMA
    VkInstance instance;
    VmaAllocator allocator;

    // Create a Vulkan instance (this is a simplified example)
    VkApplicationInfo appInfo = {};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.apiVersion = VK_MAKE_VERSION(1, 3, 0); // Specify the desired API version

    // Create a Vulkan instance (this is a simplified example)
    VkInstanceCreateInfo createInfo = {};    
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;
    if (vkCreateInstance(&createInfo, nullptr, &instance) != VK_SUCCESS) {
        std::cerr << "Failed to create Vulkan instance" << std::endl;
        return -1;
    }

    // Get the number of physical devices
    uint32_t physDeviceCount = 0;
    vkEnumeratePhysicalDevices(instance, &physDeviceCount, nullptr);
    if (physDeviceCount == 0) {
        std::cerr << "No Vulkan-capable physical devices found" << std::endl;
        vkDestroyInstance(instance, nullptr);
        glfwTerminate();
        return -1;
    }

    // Enumerate the physical devices
    std::vector<VkPhysicalDevice> physDevices(physDeviceCount);
    vkEnumeratePhysicalDevices(instance, &physDeviceCount, physDevices.data());

    // Select a physical device (for simplicity, we select the first one)
    VkPhysicalDevice physDevice = physDevices[0];

    // Create a logical device from the selected physical device
    float queuePriority = 1.0f;
    VkDeviceQueueCreateInfo queueCreateInfo = {};
    queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueCreateInfo.queueFamilyIndex = 0; // Assuming the first queue family is sufficient
    queueCreateInfo.queueCount = 1;
    queueCreateInfo.pQueuePriorities = &queuePriority;

    VkDeviceCreateInfo deviceCreateInfo = {};
    deviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceCreateInfo.queueCreateInfoCount = 1;
    deviceCreateInfo.pQueueCreateInfos = &queueCreateInfo;

    VkDevice device;
    if (vkCreateDevice(physDevice, &deviceCreateInfo, nullptr, &device) != VK_SUCCESS) {
        std::cerr << "Failed to create Vulkan logical device" << std::endl;
        vkDestroyInstance(instance, nullptr);
        glfwTerminate();
        return -1;
    }

    VmaVulkanFunctions vulkanFunctions = {};
    vulkanFunctions.vkGetInstanceProcAddr = &vkGetInstanceProcAddr;
    vulkanFunctions.vkGetDeviceProcAddr = &vkGetDeviceProcAddr;
 
    // Fill out the VmaAllocatorCreateInfo structure
    VmaAllocatorCreateInfo allocCreateInfo = {};
    allocCreateInfo.vulkanApiVersion = VK_API_VERSION_1_3;
    allocCreateInfo.instance = instance;          // Set the VkInstance pointer
    allocCreateInfo.physicalDevice = physDevice;  // Use the selected physical device
    allocCreateInfo.device = device;              // Use the created logical device
    allocCreateInfo.flags = VMA_ALLOCATOR_CREATE_EXT_MEMORY_BUDGET_BIT | 
                            VMA_ALLOCATOR_CREATE_EXT_MEMORY_PRIORITY_BIT;
    allocCreateInfo.pVulkanFunctions = &vulkanFunctions;

    if (vmaCreateAllocator(&allocCreateInfo, &allocator) != VK_SUCCESS) {
        std::cerr << "Failed to create VMA allocator" << std::endl;
        vkDestroyInstance(instance, nullptr); // Clean up the Vulkan instance
        vkDestroyDevice(device, nullptr);    // Clean up the logical device
        glfwTerminate();
        return -1;
    }

    // Use the allocator and instance as needed...

    // Clean up resources
    vmaDestroyAllocator(allocator);
    vkDestroyDevice(device, nullptr);
    vkDestroyInstance(instance, nullptr);

    glfwTerminate();
    return 0;
}