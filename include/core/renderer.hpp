#pragma once

#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <VkBootstrap.h>
#include <vector>
#include <memory>

namespace aero_boar {

class Renderer {
public:
    Renderer();
    ~Renderer();

    // Initialize Vulkan and create window
    bool Initialize(GLFWwindow* window);
    void Shutdown();

    // Main render loop
    void BeginFrame();
    void EndFrame();
    void Render();

    // Window resize handling
    void OnWindowResize();

    // Frame control
    bool ShouldSkipFrame() const { return m_skipFrame; }

    // Getters
    bool IsInitialized() const { return m_initialized; }
    VkDevice GetDevice() const { return m_device; }
    VkPhysicalDevice GetPhysicalDevice() const { return m_physicalDevice; }

private:
    // Vulkan core objects
    vkb::Instance m_vkbInstance;
    vkb::PhysicalDevice m_vkbPhysicalDevice;
    vkb::Device m_vkbDevice;
    VkInstance m_instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT m_debugMessenger = VK_NULL_HANDLE;
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkDevice m_device = VK_NULL_HANDLE;
    VkQueue m_graphicsQueue = VK_NULL_HANDLE;
    VkQueue m_presentQueue = VK_NULL_HANDLE;

    // Window and surface
    GLFWwindow* m_window = nullptr;
    VkSurfaceKHR m_surface = VK_NULL_HANDLE;

    // Swapchain
    VkSwapchainKHR m_swapchain = VK_NULL_HANDLE;
    std::vector<VkImage> m_swapchainImages;
    std::vector<VkImageView> m_swapchainImageViews;
    VkFormat m_swapchainImageFormat = VK_FORMAT_UNDEFINED;
    VkExtent2D m_swapchainExtent = {0, 0};

    // Render pass and pipeline
    VkRenderPass m_renderPass = VK_NULL_HANDLE;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_graphicsPipeline = VK_NULL_HANDLE;

    // Framebuffers
    std::vector<VkFramebuffer> m_swapchainFramebuffers;

    // Command buffers
    VkCommandPool m_commandPool = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> m_commandBuffers;

    // Synchronization
    std::vector<VkSemaphore> m_imageAvailableSemaphores;
    std::vector<VkSemaphore> m_renderFinishedSemaphores;
    std::vector<VkSemaphore> m_imageFinishedSemaphores; // Per-image semaphores
    std::vector<VkFence> m_inFlightFences;
    std::vector<VkFence> m_imagesInFlight;
    uint32_t m_currentFrame = 0;
    uint32_t m_currentImageIndex = 0;
    static const int MAX_FRAMES_IN_FLIGHT = 2;


    // State
    bool m_initialized = false;
    bool m_framebufferResized = false;
    bool m_skipFrame = false;

    // Helper methods
    bool CreateInstance();
    bool CreateSurface();
    bool SelectPhysicalDevice();
    bool CreateLogicalDevice();
    bool CreateSwapchain();
    bool CreateImageViews();
    bool CreateRenderPass();
    bool CreateGraphicsPipeline();
    bool CreateFramebuffers();
    bool CreateCommandPool();
    bool CreateCommandBuffers();
    bool CreateSyncObjects();

    void CleanupSwapchain();
    void RecreateSwapchain();

    // Triangle data for Phase 1
    struct Vertex {
        glm::vec2 pos;
        glm::vec3 color;
    };
    
    std::vector<Vertex> m_triangleVertices = {
        {{0.0f, -0.5f}, {1.0f, 0.0f, 0.0f}},
        {{0.5f, 0.5f}, {0.0f, 1.0f, 0.0f}},
        {{-0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}}
    };

    VkBuffer m_vertexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_vertexBufferMemory = VK_NULL_HANDLE;

    bool CreateVertexBuffer();
    uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);

    // Helper methods
    std::string GetExecutableDirectory();
    std::vector<char> ReadFile(const std::string& filename);
    VkShaderModule CreateShaderModule(const std::vector<char>& code);
};

} // namespace aero_boar
