#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vk_mem_alloc.h>
#include <tiny_gltf.h>
#include <vector>
#include <memory>
#include <string>
#include <unordered_map>
#include <future>
#include <mutex>
#include <thread>
#include <atomic>
#include <queue>
#include <condition_variable>
#include <functional>
#include <type_traits>

namespace aero_boar {

// Forward declarations
class Renderer;

// Asset structures
struct Vertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 texCoord;
    glm::vec4 color = glm::vec4(1.0f); // Default white color
};

struct Material {
    glm::vec4 baseColorFactor = glm::vec4(1.0f);
    float metallicFactor = 0.0f;
    float roughnessFactor = 1.0f;
    VkImage baseColorTexture = VK_NULL_HANDLE;
    VkImageView baseColorTextureView = VK_NULL_HANDLE;
    VkSampler baseColorSampler = VK_NULL_HANDLE;
    VmaAllocation baseColorTextureAllocation = VK_NULL_HANDLE;
};

struct Mesh {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    VkBuffer vertexBuffer = VK_NULL_HANDLE;
    VkBuffer indexBuffer = VK_NULL_HANDLE;
    VmaAllocation vertexBufferAllocation = VK_NULL_HANDLE;
    VmaAllocation indexBufferAllocation = VK_NULL_HANDLE;
    uint32_t materialIndex = 0;
    VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
};

struct Node {
    glm::mat4 transform = glm::mat4(1.0f);
    std::vector<uint32_t> meshIndices;
    std::vector<std::unique_ptr<Node>> children;
    std::string name;
};

struct Model {
    std::vector<Mesh> meshes;
    std::vector<Material> materials;
    std::unique_ptr<Node> rootNode;
    std::string name;
    bool isLoaded = false;
    std::string errorMessage;
};

// Asset loading result
struct AssetLoadResult {
    std::shared_ptr<Model> model;
    bool success = false;
    std::string errorMessage;
};

// Background thread pool for asset loading
class AssetThreadPool {
public:
    AssetThreadPool(size_t numThreads = std::thread::hardware_concurrency());
    ~AssetThreadPool();

    template<typename F, typename... Args>
    auto Enqueue(F&& f, Args&&... args) -> std::future<typename std::invoke_result_t<F, Args...>>;

    void Shutdown();

private:
    std::vector<std::thread> workers;
    std::queue<std::function<void()>> tasks;
    std::mutex queueMutex;
    std::condition_variable condition;
    std::atomic<bool> stop;
};

// Forward declaration
class TransferManager;

// Main glTF loader class
class GltfLoader {
public:
    GltfLoader(VkDevice device, VkPhysicalDevice physicalDevice, VmaAllocator allocator);
    ~GltfLoader();

    bool Initialize();
    void Shutdown();

    // Async asset loading
    std::future<AssetLoadResult> LoadModelAsync(const std::string& filepath);
    
    // Synchronous asset loading (for testing)
    AssetLoadResult LoadModel(const std::string& filepath);
    
    // Create a simple cube model programmatically (for testing)
    AssetLoadResult CreateCubeModel();

    // Get loaded model by name
    std::shared_ptr<Model> GetModel(const std::string& name);
    
    // Check if model is loaded
    bool IsModelLoaded(const std::string& name);

    // Cleanup model resources
    void UnloadModel(const std::string& name);

private:
    VkDevice m_device = VK_NULL_HANDLE;
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VmaAllocator m_allocator = VK_NULL_HANDLE;
    
    std::unique_ptr<AssetThreadPool> m_threadPool;
    std::unique_ptr<TransferManager> m_transferManager;
    
    std::unordered_map<std::string, std::shared_ptr<Model>> m_loadedModels;
    std::mutex m_modelsMutex;
    std::atomic<bool> m_shutdown{false};

    // glTF parsing methods
    AssetLoadResult ParseGltfFile(const std::string& filepath);
    bool LoadTextures(const tinygltf::Model& gltfModel, Model& model);
    bool LoadMaterials(const tinygltf::Model& gltfModel, Model& model);
    bool LoadMeshes(const tinygltf::Model& gltfModel, Model& model);
    bool LoadNodes(const tinygltf::Model& gltfModel, Model& model);
    
    // Helper methods
    bool CreateTextureFromImage(const tinygltf::Image& image, Material& material);
    bool CreateBufferFromAccessor(const tinygltf::Model& gltfModel, 
                                 const tinygltf::Accessor& accessor,
                                 VkBuffer& buffer, VmaAllocation& allocation);
    
    // Vertex processing
    void ProcessVertices(const tinygltf::Model& gltfModel, 
                        const tinygltf::Primitive& primitive,
                        std::vector<Vertex>& vertices);
    
    void ProcessIndices(const tinygltf::Model& gltfModel,
                       const tinygltf::Primitive& primitive,
                       std::vector<uint32_t>& indices);

    // Utility methods
    glm::mat4 GetNodeTransform(const tinygltf::Node& node);
    VkFormat GetVkFormat(int componentType, int type, bool normalized = false);
    VkPrimitiveTopology GetVkPrimitiveTopology(int mode);
    std::unique_ptr<Node> LoadNode(const tinygltf::Model& gltfModel, const tinygltf::Node& gltfNode);
};

} // namespace aero_boar
