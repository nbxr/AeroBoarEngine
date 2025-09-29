#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <VkBootstrap.h>
#include <vk_mem_alloc.h>
#include <vector>
#include <memory>
#include <string>
#include <unordered_map>

namespace aero_boar {

// Forward declarations
class GltfLoader;
class InputManager;
class IWindow;
struct Model;
struct Mesh;
struct Vertex;

class Renderer {
public:
    Renderer();
    ~Renderer();

    // Initialize Vulkan and create window
    bool Initialize(IWindow* window);
    void Shutdown();

    // Main render loop
    void BeginFrame();
    void EndFrame();
    void Render();

    // Window resize handling
    void OnWindowResize();

    // Asset loading
    bool LoadModel(const std::string& filepath);
    bool CreateCubeModel();
    void RenderModel(const std::string& modelName);

    // Camera controls
    void UpdateCamera(float deltaTime);
    void ResetCamera();

    // Getters
    bool IsInitialized() const { return m_initialized; }
    VkDevice GetDevice() const { return m_device; }
    VkPhysicalDevice GetPhysicalDevice() const { return m_physicalDevice; }
    InputManager* GetInputManager() const { return m_inputManager.get(); }

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
    
    // VMA allocator
    VmaAllocator m_allocator = VK_NULL_HANDLE;

    // Asset loading
    std::unique_ptr<GltfLoader> m_gltfLoader;
    
    // Input management
    std::unique_ptr<InputManager> m_inputManager;

    // Window and surface
    IWindow* m_window = nullptr;
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

    // Frame management
    struct Frame {
        VkSemaphore imageAvailableSemaphore = VK_NULL_HANDLE;
        VkSemaphore renderFinishedSemaphore = VK_NULL_HANDLE;
        VkFence inFlightFence = VK_NULL_HANDLE;
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        bool isActive = false;  // Whether this frame is currently being processed
        uint32_t imageIndex = 0;  // Which swapchain image this frame is using
    };
    
    struct ImageResources {
        VkSemaphore finishedSemaphore = VK_NULL_HANDLE;
        VkFence fenceInFlight = VK_NULL_HANDLE;  // Fence from the frame that's using this image
    };
    
    std::vector<Frame> m_frames;
    std::vector<ImageResources> m_imageResources;
    uint32_t m_currentFrame = 0;
    uint32_t m_currentImageIndex = 0;
    static const int MAX_FRAMES_IN_FLIGHT = 2;


    // State
    bool m_initialized = false;
    bool m_framebufferResized = false;
    bool m_frameSkipped = false;

    // Helper methods
    bool CreateInstance();
    bool CreateSurface();
    bool SelectPhysicalDevice();
    bool CreateLogicalDevice();
    bool CreateVMAAllocator();
    bool CreateSwapchain();
    bool CreateImageViews();
    bool CreateRenderPass();
    bool CreateGraphicsPipeline();
    bool CreateFramebuffers();
    bool CreateCommandPool();
    bool CreateCommandBuffers();
    bool CreateSyncObjects();
    bool CreateFrameResources();

    void CleanupSwapchain();
    void RecreateSwapchain();
    
    // Frame management methods
    void WaitForActiveFrames();
    void ResetFrameResources();
    void CleanupFrameResources();

    // Triangle data for Phase 1 (using same Vertex structure as glTF loader)
    std::vector<Vertex> m_triangleVertices;

    // Camera system
    struct Camera {
        glm::vec3 position = glm::vec3(0.0f, 0.0f, 3.0f);
        glm::vec3 front = glm::vec3(0.0f, 0.0f, -1.0f);
        glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f);
        glm::vec3 right = glm::vec3(1.0f, 0.0f, 0.0f);
        glm::vec3 worldUp = glm::vec3(0.0f, 1.0f, 0.0f);
        
        float yaw = -90.0f;   // Start looking down the negative Z axis
        float pitch = 0.0f;
        float fov = 45.0f;
        float nearPlane = 0.1f;
        float farPlane = 100.0f;
        
        float movementSpeed = 5.0f;
        float mouseSensitivity = 0.1f;
        
        // Initial values for reset
        glm::vec3 initialPosition = glm::vec3(0.0f, 0.0f, 3.0f);
        float initialYaw = -90.0f;
        float initialPitch = 0.0f;
    } m_camera;

    
    // Uniform buffer for MVP matrices
    struct UniformBufferObject {
        glm::mat4 model;
        glm::mat4 view;
        glm::mat4 proj;
    };
    
    VkBuffer m_uniformBuffer = VK_NULL_HANDLE;
    VmaAllocation m_uniformBufferAllocation = VK_NULL_HANDLE;
    void* m_uniformBufferMapped = nullptr;
    
    VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet m_descriptorSet = VK_NULL_HANDLE;

    VkBuffer m_vertexBuffer = VK_NULL_HANDLE;
    VmaAllocation m_vertexBufferAllocation = VK_NULL_HANDLE;

    bool CreateVertexBuffer();
    bool CreateUniformBuffer();
    bool CreateDescriptorSetLayout();
    bool CreateDescriptorPool();
    bool CreateDescriptorSet();
    uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);

    // Helper methods
    std::string GetExecutableDirectory();
    std::vector<char> ReadFile(const std::string& filename);
    VkShaderModule CreateShaderModule(const std::vector<char>& code);
};

} // namespace aero_boar
