#include "assets/gltf_loader.hpp"
#include "core/transfer_manager.hpp"
#include <iostream>
#include <fstream>
#include <stdexcept>
#include <algorithm>
#include <cstring>
#include <vector>

namespace aero_boar {

// AssetThreadPool implementation
AssetThreadPool::AssetThreadPool(size_t numThreads) : stop(false) {
    for (size_t i = 0; i < numThreads; ++i) {
        workers.emplace_back([this] {
            while (true) {
                std::function<void()> task;
                {
                    std::unique_lock<std::mutex> lock(queueMutex);
                    condition.wait(lock, [this] { return stop || !tasks.empty(); });
                    if (stop && tasks.empty()) return;
                    task = std::move(tasks.front());
                    tasks.pop();
                }
                task();
            }
        });
    }
}

AssetThreadPool::~AssetThreadPool() {
    Shutdown();
}

template<typename F, typename... Args>
auto AssetThreadPool::Enqueue(F&& f, Args&&... args) -> std::future<typename std::invoke_result_t<F, Args...>> {
    using return_type = typename std::invoke_result_t<F, Args...>;

    auto task = std::make_shared<std::packaged_task<return_type()>>(
        std::bind(std::forward<F>(f), std::forward<Args>(args)...)
    );

    std::future<return_type> res = task->get_future();
    {
        std::unique_lock<std::mutex> lock(queueMutex);
        if (stop) {
            throw std::runtime_error("enqueue on stopped ThreadPool");
        }
        tasks.emplace([task]() { (*task)(); });
    }
    condition.notify_one();
    return res;
}

void AssetThreadPool::Shutdown() {
    {
        std::unique_lock<std::mutex> lock(queueMutex);
        if (stop) {
            return; // Already shutting down
        }
        stop = true;
    }
    condition.notify_all();
    for (std::thread &worker : workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
}

// GltfLoader implementation
GltfLoader::GltfLoader(VkDevice device, VkPhysicalDevice physicalDevice, VmaAllocator allocator)
    : m_device(device), m_physicalDevice(physicalDevice), m_allocator(allocator) {
}

GltfLoader::~GltfLoader() {
    Shutdown();
}

bool GltfLoader::Initialize() {
    try {
        // Create thread pool
        m_threadPool = std::make_unique<AssetThreadPool>();
        
        // Create transfer manager
        m_transferManager = std::make_unique<TransferManager>(m_device, m_physicalDevice, m_allocator);
        if (!m_transferManager->Initialize()) {
            std::cerr << "Failed to initialize transfer manager" << std::endl;
            return false;
        }

        std::cout << "GltfLoader initialized successfully" << std::endl;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "GltfLoader initialization failed: " << e.what() << std::endl;
        return false;
    }
}

void GltfLoader::Shutdown() {
    std::cout << "GltfLoader::Shutdown() called" << std::endl;
    if (m_shutdown) {
        std::cout << "GltfLoader already shut down, skipping" << std::endl;
        return;
    }
    m_shutdown = true;
    
    try {
        if (m_threadPool) {
            std::cout << "Shutting down thread pool..." << std::endl;
            m_threadPool->Shutdown();
            m_threadPool.reset();
            std::cout << "Thread pool shutdown completed" << std::endl;
        }

        // Cleanup all loaded models BEFORE shutting down transfer manager
        {
            std::cout << "Cleaning up loaded models..." << std::endl;
            std::lock_guard<std::mutex> lock(m_modelsMutex);
            for (auto& [name, model] : m_loadedModels) {
                if (model) {
                    // Cleanup model resources
                    for (auto& mesh : model->meshes) {
                        if (mesh.vertexBuffer != VK_NULL_HANDLE && m_device != VK_NULL_HANDLE) {
                            vkDestroyBuffer(m_device, mesh.vertexBuffer, nullptr);
                            mesh.vertexBuffer = VK_NULL_HANDLE;
                        }
                        if (mesh.indexBuffer != VK_NULL_HANDLE && m_device != VK_NULL_HANDLE) {
                            vkDestroyBuffer(m_device, mesh.indexBuffer, nullptr);
                            mesh.indexBuffer = VK_NULL_HANDLE;
                        }
                        if (mesh.vertexBufferAllocation != VK_NULL_HANDLE && m_allocator != VK_NULL_HANDLE) {
                            vmaFreeMemory(m_allocator, mesh.vertexBufferAllocation);
                            mesh.vertexBufferAllocation = VK_NULL_HANDLE;
                        }
                        if (mesh.indexBufferAllocation != VK_NULL_HANDLE && m_allocator != VK_NULL_HANDLE) {
                            vmaFreeMemory(m_allocator, mesh.indexBufferAllocation);
                            mesh.indexBufferAllocation = VK_NULL_HANDLE;
                        }
                    }
                    
                    for (auto& material : model->materials) {
                        if (material.baseColorTexture != VK_NULL_HANDLE && m_device != VK_NULL_HANDLE) {
                            vkDestroyImage(m_device, material.baseColorTexture, nullptr);
                            material.baseColorTexture = VK_NULL_HANDLE;
                        }
                        if (material.baseColorTextureView != VK_NULL_HANDLE && m_device != VK_NULL_HANDLE) {
                            vkDestroyImageView(m_device, material.baseColorTextureView, nullptr);
                            material.baseColorTextureView = VK_NULL_HANDLE;
                        }
                        if (material.baseColorSampler != VK_NULL_HANDLE && m_device != VK_NULL_HANDLE) {
                            vkDestroySampler(m_device, material.baseColorSampler, nullptr);
                            material.baseColorSampler = VK_NULL_HANDLE;
                        }
                        if (material.baseColorTextureAllocation != VK_NULL_HANDLE && m_allocator != VK_NULL_HANDLE) {
                            vmaFreeMemory(m_allocator, material.baseColorTextureAllocation);
                            material.baseColorTextureAllocation = VK_NULL_HANDLE;
                        }
                    }
                }
            }
            m_loadedModels.clear();
        }

        // Shutdown transfer manager after cleaning up resources
        if (m_transferManager) {
            std::cout << "Shutting down transfer manager..." << std::endl;
            m_transferManager->Shutdown();
            m_transferManager.reset();
            std::cout << "Transfer manager shutdown completed" << std::endl;
        }
    } catch (const std::exception& e) {
        std::cerr << "Error during glTF loader shutdown: " << e.what() << std::endl;
    }
    std::cout << "GltfLoader::Shutdown() completed" << std::endl;
}

std::future<AssetLoadResult> GltfLoader::LoadModelAsync(const std::string& filepath) {
    return m_threadPool->Enqueue([this, filepath]() -> AssetLoadResult {
        return LoadModel(filepath);
    });
}

AssetLoadResult GltfLoader::LoadModel(const std::string& filepath) {
    AssetLoadResult result;
    
    try {
        // Check if model is already loaded
        {
            std::lock_guard<std::mutex> lock(m_modelsMutex);
            auto it = m_loadedModels.find(filepath);
            if (it != m_loadedModels.end()) {
                result.model = it->second;
                result.success = true;
                return result;
            }
        }

        // Parse glTF file
        result = ParseGltfFile(filepath);
        if (!result.success) {
            return result;
        }

        // Store loaded model
        {
            std::lock_guard<std::mutex> lock(m_modelsMutex);
            m_loadedModels[filepath] = result.model;
        }

        std::cout << "Successfully loaded model: " << filepath << std::endl;
        return result;
        
    } catch (const std::exception& e) {
        result.success = false;
        result.errorMessage = "Exception during model loading: " + std::string(e.what());
        std::cerr << result.errorMessage << std::endl;
        return result;
    }
}

AssetLoadResult GltfLoader::CreateCubeModel() {
    AssetLoadResult result;
    result.model = std::make_shared<Model>();
    result.model->name = "cube";
    
    try {
        // Create a simple cube mesh
        Mesh cubeMesh;
        
        // Define cube vertices (8 vertices)
        std::vector<Vertex> cubeVertices = {
            // Front face
            {{-0.5f, -0.5f,  0.5f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 1.0f}}, // 0
            {{ 0.5f, -0.5f,  0.5f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}, {0.0f, 1.0f, 0.0f, 1.0f}}, // 1
            {{ 0.5f,  0.5f,  0.5f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}, {0.0f, 0.0f, 1.0f, 1.0f}}, // 2
            {{-0.5f,  0.5f,  0.5f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}, {1.0f, 1.0f, 0.0f, 1.0f}}, // 3
            // Back face
            {{-0.5f, -0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, {1.0f, 0.0f}, {1.0f, 0.0f, 1.0f, 1.0f}}, // 4
            {{ 0.5f, -0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, {0.0f, 0.0f}, {0.0f, 1.0f, 1.0f, 1.0f}}, // 5
            {{ 0.5f,  0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, {0.0f, 1.0f}, {1.0f, 1.0f, 1.0f, 1.0f}}, // 6
            {{-0.5f,  0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, {1.0f, 1.0f}, {0.5f, 0.5f, 0.5f, 1.0f}}  // 7
        };
        
        // Define cube indices (12 triangles = 36 indices)
        std::vector<uint32_t> cubeIndices = {
            // Front face
            0, 1, 2,  2, 3, 0,
            // Back face
            4, 6, 5,  6, 4, 7,
            // Left face
            4, 0, 3,  3, 7, 4,
            // Right face
            1, 5, 6,  6, 2, 1,
            // Top face
            3, 2, 6,  6, 7, 3,
            // Bottom face
            4, 5, 1,  1, 0, 4
        };
        
        cubeMesh.vertices = cubeVertices;
        cubeMesh.indices = cubeIndices;
        cubeMesh.materialIndex = 0;
        cubeMesh.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        
        // Create vertex buffer
        VkBufferCreateInfo vertexBufferInfo{};
        vertexBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        vertexBufferInfo.size = sizeof(Vertex) * cubeMesh.vertices.size();
        vertexBufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        vertexBufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo vertexAllocInfo = {};
        vertexAllocInfo.usage = VMA_MEMORY_USAGE_AUTO;
        vertexAllocInfo.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;

        if (!m_transferManager->CreateBuffer(vertexBufferInfo, vertexAllocInfo, 
                                           cubeMesh.vertexBuffer, cubeMesh.vertexBufferAllocation)) {
            result.success = false;
            result.errorMessage = "Failed to create vertex buffer for cube";
            return result;
        }

        if (!m_transferManager->UploadBufferData(cubeMesh.vertexBuffer, cubeMesh.vertexBufferAllocation,
                                               cubeMesh.vertices.data(), sizeof(Vertex) * cubeMesh.vertices.size())) {
            result.success = false;
            result.errorMessage = "Failed to upload vertex data for cube";
            return result;
        }

        // Create index buffer
        VkBufferCreateInfo indexBufferInfo{};
        indexBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        indexBufferInfo.size = sizeof(uint32_t) * cubeMesh.indices.size();
        indexBufferInfo.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        indexBufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo indexAllocInfo = {};
        indexAllocInfo.usage = VMA_MEMORY_USAGE_AUTO;
        indexAllocInfo.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;

        if (!m_transferManager->CreateBuffer(indexBufferInfo, indexAllocInfo,
                                           cubeMesh.indexBuffer, cubeMesh.indexBufferAllocation)) {
            result.success = false;
            result.errorMessage = "Failed to create index buffer for cube";
            return result;
        }

        if (!m_transferManager->UploadBufferData(cubeMesh.indexBuffer, cubeMesh.indexBufferAllocation,
                                               cubeMesh.indices.data(), sizeof(uint32_t) * cubeMesh.indices.size())) {
            result.success = false;
            result.errorMessage = "Failed to upload index data for cube";
            return result;
        }
        
        // Create a simple material
        Material cubeMaterial;
        cubeMaterial.baseColorFactor = glm::vec4(0.8f, 0.2f, 0.2f, 1.0f); // Red color
        cubeMaterial.metallicFactor = 0.0f;
        cubeMaterial.roughnessFactor = 0.5f;
        
        // Add mesh and material to model
        result.model->meshes.push_back(cubeMesh);
        result.model->materials.push_back(cubeMaterial);
        
        // Create root node
        result.model->rootNode = std::make_unique<Node>();
        result.model->rootNode->name = "Cube";
        result.model->rootNode->meshIndices.push_back(0);
        
        result.model->isLoaded = true;
        result.success = true;
        
        // Store the model
        {
            std::lock_guard<std::mutex> lock(m_modelsMutex);
            m_loadedModels["cube"] = result.model;
        }
        
        std::cout << "Successfully created cube model programmatically" << std::endl;
        return result;
        
    } catch (const std::exception& e) {
        result.success = false;
        result.errorMessage = "Exception during cube creation: " + std::string(e.what());
        std::cerr << result.errorMessage << std::endl;
        return result;
    }
}

std::shared_ptr<Model> GltfLoader::GetModel(const std::string& name) {
    std::lock_guard<std::mutex> lock(m_modelsMutex);
    auto it = m_loadedModels.find(name);
    if (it != m_loadedModels.end()) {
        return it->second;
    }
    return nullptr;
}

bool GltfLoader::IsModelLoaded(const std::string& name) {
    std::lock_guard<std::mutex> lock(m_modelsMutex);
    return m_loadedModels.find(name) != m_loadedModels.end();
}

void GltfLoader::UnloadModel(const std::string& name) {
    std::lock_guard<std::mutex> lock(m_modelsMutex);
    auto it = m_loadedModels.find(name);
    if (it != m_loadedModels.end()) {
        // Cleanup model resources
        auto& model = it->second;
        if (model) {
            for (auto& mesh : model->meshes) {
                if (mesh.vertexBuffer != VK_NULL_HANDLE) {
                    vkDestroyBuffer(m_device, mesh.vertexBuffer, nullptr);
                }
                if (mesh.indexBuffer != VK_NULL_HANDLE) {
                    vkDestroyBuffer(m_device, mesh.indexBuffer, nullptr);
                }
                if (mesh.vertexBufferAllocation != VK_NULL_HANDLE) {
                    vmaFreeMemory(m_allocator, mesh.vertexBufferAllocation);
                }
                if (mesh.indexBufferAllocation != VK_NULL_HANDLE) {
                    vmaFreeMemory(m_allocator, mesh.indexBufferAllocation);
                }
            }
            
            for (auto& material : model->materials) {
                if (material.baseColorTexture != VK_NULL_HANDLE) {
                    vkDestroyImage(m_device, material.baseColorTexture, nullptr);
                }
                if (material.baseColorTextureView != VK_NULL_HANDLE) {
                    vkDestroyImageView(m_device, material.baseColorTextureView, nullptr);
                }
                if (material.baseColorSampler != VK_NULL_HANDLE) {
                    vkDestroySampler(m_device, material.baseColorSampler, nullptr);
                }
                if (material.baseColorTextureAllocation != VK_NULL_HANDLE) {
                    vmaFreeMemory(m_allocator, material.baseColorTextureAllocation);
                }
            }
        }
        m_loadedModels.erase(it);
    }
}

AssetLoadResult GltfLoader::ParseGltfFile(const std::string& filepath) {
    AssetLoadResult result;
    result.model = std::make_shared<Model>();
    result.model->name = filepath;

    try {
        tinygltf::Model gltfModel;
        tinygltf::TinyGLTF loader;
        std::string err;
        std::string warn;

        bool ret = false;
        if (filepath.find(".glb") != std::string::npos) {
            ret = loader.LoadBinaryFromFile(&gltfModel, &err, &warn, filepath);
        } else {
            ret = loader.LoadASCIIFromFile(&gltfModel, &err, &warn, filepath);
        }

        if (!warn.empty()) {
            std::cout << "glTF warning: " << warn << std::endl;
        }

        if (!err.empty()) {
            result.success = false;
            result.errorMessage = "glTF error: " + err;
            return result;
        }

        if (!ret) {
            result.success = false;
            result.errorMessage = "Failed to load glTF file: " + filepath;
            return result;
        }

        // Load materials first (needed for meshes)
        if (!LoadMaterials(gltfModel, *result.model)) {
            result.success = false;
            result.errorMessage = "Failed to load materials";
            return result;
        }

        // Load meshes
        if (!LoadMeshes(gltfModel, *result.model)) {
            result.success = false;
            result.errorMessage = "Failed to load meshes";
            return result;
        }

        // Load nodes
        if (!LoadNodes(gltfModel, *result.model)) {
            result.success = false;
            result.errorMessage = "Failed to load nodes";
            return result;
        }

        result.model->isLoaded = true;
        result.success = true;
        return result;

    } catch (const std::exception& e) {
        result.success = false;
        result.errorMessage = "Exception during glTF parsing: " + std::string(e.what());
        return result;
    }
}

bool GltfLoader::LoadMaterials(const tinygltf::Model& gltfModel, Model& model) {
    model.materials.resize(gltfModel.materials.size());

    for (size_t i = 0; i < gltfModel.materials.size(); i++) {
        const auto& gltfMaterial = gltfModel.materials[i];
        auto& material = model.materials[i];

        // Load base color factor
        if (gltfMaterial.pbrMetallicRoughness.baseColorFactor.size() >= 4) {
            material.baseColorFactor = glm::vec4(
                static_cast<float>(gltfMaterial.pbrMetallicRoughness.baseColorFactor[0]),
                static_cast<float>(gltfMaterial.pbrMetallicRoughness.baseColorFactor[1]),
                static_cast<float>(gltfMaterial.pbrMetallicRoughness.baseColorFactor[2]),
                static_cast<float>(gltfMaterial.pbrMetallicRoughness.baseColorFactor[3])
            );
        }

        // Load metallic and roughness factors
        material.metallicFactor = static_cast<float>(gltfMaterial.pbrMetallicRoughness.metallicFactor);
        material.roughnessFactor = static_cast<float>(gltfMaterial.pbrMetallicRoughness.roughnessFactor);

        // Load base color texture
        if (gltfMaterial.pbrMetallicRoughness.baseColorTexture.index >= 0) {
            const auto& texture = gltfModel.textures[gltfMaterial.pbrMetallicRoughness.baseColorTexture.index];
            if (texture.source >= 0 && texture.source < gltfModel.images.size()) {
                const auto& image = gltfModel.images[texture.source];
                if (!CreateTextureFromImage(image, material)) {
                    std::cerr << "Failed to create texture for material " << i << std::endl;
                    return false;
                }
            }
        }
    }

    return true;
}

bool GltfLoader::LoadMeshes(const tinygltf::Model& gltfModel, Model& model) {
    model.meshes.resize(gltfModel.meshes.size());

    for (size_t i = 0; i < gltfModel.meshes.size(); i++) {
        const auto& gltfMesh = gltfModel.meshes[i];
        auto& mesh = model.meshes[i];

        // For simplicity, we'll only load the first primitive of each mesh
        if (gltfMesh.primitives.empty()) {
            continue;
        }

        const auto& primitive = gltfMesh.primitives[0];
        mesh.materialIndex = primitive.material;

        // Process vertices
        ProcessVertices(gltfModel, primitive, mesh.vertices);
        if (mesh.vertices.empty()) {
            std::cerr << "No vertices found in mesh " << i << std::endl;
            continue;
        }

        // Process indices
        ProcessIndices(gltfModel, primitive, mesh.indices);
        if (mesh.indices.empty()) {
            std::cerr << "No indices found in mesh " << i << std::endl;
            continue;
        }

        // Create vertex buffer
        VkBufferCreateInfo vertexBufferInfo{};
        vertexBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        vertexBufferInfo.size = sizeof(Vertex) * mesh.vertices.size();
        vertexBufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        vertexBufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo vertexAllocInfo = {};
        vertexAllocInfo.usage = VMA_MEMORY_USAGE_AUTO;
        vertexAllocInfo.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;

        if (!m_transferManager->CreateBuffer(vertexBufferInfo, vertexAllocInfo, 
                                           mesh.vertexBuffer, mesh.vertexBufferAllocation)) {
            std::cerr << "Failed to create vertex buffer for mesh " << i << std::endl;
            return false;
        }

        if (!m_transferManager->UploadBufferData(mesh.vertexBuffer, mesh.vertexBufferAllocation,
                                               mesh.vertices.data(), sizeof(Vertex) * mesh.vertices.size())) {
            std::cerr << "Failed to upload vertex data for mesh " << i << std::endl;
            return false;
        }

        // Create index buffer
        VkBufferCreateInfo indexBufferInfo{};
        indexBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        indexBufferInfo.size = sizeof(uint32_t) * mesh.indices.size();
        indexBufferInfo.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        indexBufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo indexAllocInfo = {};
        indexAllocInfo.usage = VMA_MEMORY_USAGE_AUTO;
        indexAllocInfo.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;

        if (!m_transferManager->CreateBuffer(indexBufferInfo, indexAllocInfo,
                                           mesh.indexBuffer, mesh.indexBufferAllocation)) {
            std::cerr << "Failed to create index buffer for mesh " << i << std::endl;
            return false;
        }

        if (!m_transferManager->UploadBufferData(mesh.indexBuffer, mesh.indexBufferAllocation,
                                               mesh.indices.data(), sizeof(uint32_t) * mesh.indices.size())) {
            std::cerr << "Failed to upload index data for mesh " << i << std::endl;
            return false;
        }

        // Set primitive topology
        mesh.topology = GetVkPrimitiveTopology(primitive.mode);
    }

    return true;
}

bool GltfLoader::LoadNodes(const tinygltf::Model& gltfModel, Model& model) {
    if (gltfModel.scenes.empty()) {
        return true; // No scenes to load
    }

    const auto& scene = gltfModel.scenes[0]; // Load first scene
    model.rootNode = std::make_unique<Node>();
    model.rootNode->name = "Root";

    // Load scene nodes
    for (size_t i = 0; i < scene.nodes.size(); i++) {
        int nodeIndex = scene.nodes[i];
        if (nodeIndex >= 0 && nodeIndex < static_cast<int>(gltfModel.nodes.size())) {
            auto childNode = LoadNode(gltfModel, gltfModel.nodes[nodeIndex]);
            if (childNode) {
                model.rootNode->children.push_back(std::move(childNode));
            }
        }
    }

    return true;
}

std::unique_ptr<Node> GltfLoader::LoadNode(const tinygltf::Model& gltfModel, const tinygltf::Node& gltfNode) {
    auto node = std::make_unique<Node>();
    node->name = gltfNode.name;
    node->transform = GetNodeTransform(gltfNode);

    // Load mesh indices
    if (gltfNode.mesh >= 0) {
        node->meshIndices.push_back(static_cast<uint32_t>(gltfNode.mesh));
    }

    // Load children
    for (size_t i = 0; i < gltfNode.children.size(); i++) {
        int childIndex = gltfNode.children[i];
        if (childIndex >= 0 && childIndex < static_cast<int>(gltfModel.nodes.size())) {
            auto childNode = LoadNode(gltfModel, gltfModel.nodes[childIndex]);
            if (childNode) {
                node->children.push_back(std::move(childNode));
            }
        }
    }

    return node;
}

bool GltfLoader::CreateTextureFromImage(const tinygltf::Image& image, Material& material) {
    // For now, we'll create a simple 1x1 white texture as a placeholder
    // In a full implementation, you would decode the image data and create proper textures
    
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = 1;
    imageInfo.extent.height = 1;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocInfo.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;

    if (!m_transferManager->CreateImage(imageInfo, allocInfo, 
                                      material.baseColorTexture, material.baseColorTextureAllocation)) {
        return false;
    }

    // Create white pixel data
    uint32_t whitePixel = 0xFFFFFFFF;
    if (!m_transferManager->UploadImageData(material.baseColorTexture, imageInfo,
                                          &whitePixel, sizeof(whitePixel))) {
        return false;
    }

    // Create image view
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = material.baseColorTexture;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(m_device, &viewInfo, nullptr, &material.baseColorTextureView) != VK_SUCCESS) {
        return false;
    }

    // Create sampler
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.maxAnisotropy = 1.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 0.0f;

    if (vkCreateSampler(m_device, &samplerInfo, nullptr, &material.baseColorSampler) != VK_SUCCESS) {
        return false;
    }

    return true;
}

void GltfLoader::ProcessVertices(const tinygltf::Model& gltfModel, 
                                const tinygltf::Primitive& primitive,
                                std::vector<Vertex>& vertices) {
    vertices.clear();

    // Get position data
    const auto& positionAccessor = gltfModel.accessors[primitive.attributes.at("POSITION")];
    const auto& positionBufferView = gltfModel.bufferViews[positionAccessor.bufferView];
    const auto& positionBuffer = gltfModel.buffers[positionBufferView.buffer];
    
    const float* positions = reinterpret_cast<const float*>(&positionBuffer.data[positionBufferView.byteOffset + positionAccessor.byteOffset]);
    
    // Get normal data (if available)
    const float* normals = nullptr;
    if (primitive.attributes.find("NORMAL") != primitive.attributes.end()) {
        const auto& normalAccessor = gltfModel.accessors[primitive.attributes.at("NORMAL")];
        const auto& normalBufferView = gltfModel.bufferViews[normalAccessor.bufferView];
        const auto& normalBuffer = gltfModel.buffers[normalBufferView.buffer];
        normals = reinterpret_cast<const float*>(&normalBuffer.data[normalBufferView.byteOffset + normalAccessor.byteOffset]);
    }

    // Get texture coordinate data (if available)
    const float* texCoords = nullptr;
    if (primitive.attributes.find("TEXCOORD_0") != primitive.attributes.end()) {
        const auto& texCoordAccessor = gltfModel.accessors[primitive.attributes.at("TEXCOORD_0")];
        const auto& texCoordBufferView = gltfModel.bufferViews[texCoordAccessor.bufferView];
        const auto& texCoordBuffer = gltfModel.buffers[texCoordBufferView.buffer];
        texCoords = reinterpret_cast<const float*>(&texCoordBuffer.data[texCoordBufferView.byteOffset + texCoordAccessor.byteOffset]);
    }

    // Get color data (if available)
    const float* colors = nullptr;
    const tinygltf::Accessor* colorAccessor = nullptr;
    if (primitive.attributes.find("COLOR_0") != primitive.attributes.end()) {
        colorAccessor = &gltfModel.accessors[primitive.attributes.at("COLOR_0")];
        const auto& colorBufferView = gltfModel.bufferViews[colorAccessor->bufferView];
        const auto& colorBuffer = gltfModel.buffers[colorBufferView.buffer];
        colors = reinterpret_cast<const float*>(&colorBuffer.data[colorBufferView.byteOffset + colorAccessor->byteOffset]);
    }

    // Create vertices
    vertices.resize(positionAccessor.count);
    for (size_t i = 0; i < positionAccessor.count; i++) {
        Vertex& vertex = vertices[i];
        
        // Position (required)
        vertex.position = glm::vec3(positions[i * 3], positions[i * 3 + 1], positions[i * 3 + 2]);
        
        // Normal (optional)
        if (normals) {
            vertex.normal = glm::vec3(normals[i * 3], normals[i * 3 + 1], normals[i * 3 + 2]);
        } else {
            vertex.normal = glm::vec3(0.0f, 0.0f, 1.0f); // Default up normal
        }
        
        // Texture coordinates (optional)
        if (texCoords) {
            vertex.texCoord = glm::vec2(texCoords[i * 2], texCoords[i * 2 + 1]);
        } else {
            vertex.texCoord = glm::vec2(0.0f, 0.0f); // Default UV
        }
        
        // Color (optional)
        if (colors && colorAccessor) {
            if (colorAccessor->type == TINYGLTF_TYPE_VEC3) {
                vertex.color = glm::vec4(colors[i * 3], colors[i * 3 + 1], colors[i * 3 + 2], 1.0f);
            } else if (colorAccessor->type == TINYGLTF_TYPE_VEC4) {
                vertex.color = glm::vec4(colors[i * 4], colors[i * 4 + 1], colors[i * 4 + 2], colors[i * 4 + 3]);
            }
        } else {
            vertex.color = glm::vec4(1.0f); // Default white
        }
    }
}

void GltfLoader::ProcessIndices(const tinygltf::Model& gltfModel,
                               const tinygltf::Primitive& primitive,
                               std::vector<uint32_t>& indices) {
    indices.clear();

    if (primitive.indices < 0) {
        return; // No indices
    }

    const auto& indexAccessor = gltfModel.accessors[primitive.indices];
    const auto& indexBufferView = gltfModel.bufferViews[indexAccessor.bufferView];
    const auto& indexBuffer = gltfModel.buffers[indexBufferView.buffer];

    indices.resize(indexAccessor.count);

    if (indexAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
        const uint16_t* src = reinterpret_cast<const uint16_t*>(&indexBuffer.data[indexBufferView.byteOffset + indexAccessor.byteOffset]);
        for (size_t i = 0; i < indexAccessor.count; i++) {
            indices[i] = static_cast<uint32_t>(src[i]);
        }
    } else if (indexAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT) {
        const uint32_t* src = reinterpret_cast<const uint32_t*>(&indexBuffer.data[indexBufferView.byteOffset + indexAccessor.byteOffset]);
        memcpy(indices.data(), src, indexAccessor.count * sizeof(uint32_t));
    } else if (indexAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE) {
        const uint8_t* src = reinterpret_cast<const uint8_t*>(&indexBuffer.data[indexBufferView.byteOffset + indexAccessor.byteOffset]);
        for (size_t i = 0; i < indexAccessor.count; i++) {
            indices[i] = static_cast<uint32_t>(src[i]);
        }
    }
}

glm::mat4 GltfLoader::GetNodeTransform(const tinygltf::Node& node) {
    glm::mat4 transform = glm::mat4(1.0f);

    if (!node.matrix.empty()) {
        // Use matrix if available
        for (int i = 0; i < 4; i++) {
            for (int j = 0; j < 4; j++) {
                transform[i][j] = static_cast<float>(node.matrix[i * 4 + j]);
            }
        }
    } else {
        // Build from TRS
        if (!node.translation.empty()) {
            glm::vec3 translation(
                static_cast<float>(node.translation[0]),
                static_cast<float>(node.translation[1]),
                static_cast<float>(node.translation[2])
            );
            transform = glm::translate(transform, translation);
        }

        if (!node.rotation.empty()) {
            glm::quat rotation(
                static_cast<float>(node.rotation[3]), // w
                static_cast<float>(node.rotation[0]), // x
                static_cast<float>(node.rotation[1]), // y
                static_cast<float>(node.rotation[2])  // z
            );
            transform = transform * glm::mat4_cast(rotation);
        }

        if (!node.scale.empty()) {
            glm::vec3 scale(
                static_cast<float>(node.scale[0]),
                static_cast<float>(node.scale[1]),
                static_cast<float>(node.scale[2])
            );
            transform = glm::scale(transform, scale);
        }
    }

    return transform;
}

VkFormat GltfLoader::GetVkFormat(int componentType, int type, bool normalized) {
    if (type == TINYGLTF_TYPE_SCALAR) {
        switch (componentType) {
            case TINYGLTF_COMPONENT_TYPE_BYTE:
                return normalized ? VK_FORMAT_R8_SNORM : VK_FORMAT_R8_SINT;
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
                return normalized ? VK_FORMAT_R8_UNORM : VK_FORMAT_R8_UINT;
            case TINYGLTF_COMPONENT_TYPE_SHORT:
                return normalized ? VK_FORMAT_R16_SNORM : VK_FORMAT_R16_SINT;
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
                return normalized ? VK_FORMAT_R16_UNORM : VK_FORMAT_R16_UINT;
            case TINYGLTF_COMPONENT_TYPE_INT:
                return VK_FORMAT_R32_SINT;
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
                return VK_FORMAT_R32_UINT;
            case TINYGLTF_COMPONENT_TYPE_FLOAT:
                return VK_FORMAT_R32_SFLOAT;
            case TINYGLTF_COMPONENT_TYPE_DOUBLE:
                return VK_FORMAT_R64_SFLOAT;
        }
    } else if (type == TINYGLTF_TYPE_VEC2) {
        switch (componentType) {
            case TINYGLTF_COMPONENT_TYPE_BYTE:
                return normalized ? VK_FORMAT_R8G8_SNORM : VK_FORMAT_R8G8_SINT;
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
                return normalized ? VK_FORMAT_R8G8_UNORM : VK_FORMAT_R8G8_UINT;
            case TINYGLTF_COMPONENT_TYPE_SHORT:
                return normalized ? VK_FORMAT_R16G16_SNORM : VK_FORMAT_R16G16_SINT;
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
                return normalized ? VK_FORMAT_R16G16_UNORM : VK_FORMAT_R16G16_UINT;
            case TINYGLTF_COMPONENT_TYPE_INT:
                return VK_FORMAT_R32G32_SINT;
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
                return VK_FORMAT_R32G32_UINT;
            case TINYGLTF_COMPONENT_TYPE_FLOAT:
                return VK_FORMAT_R32G32_SFLOAT;
            case TINYGLTF_COMPONENT_TYPE_DOUBLE:
                return VK_FORMAT_R64G64_SFLOAT;
        }
    } else if (type == TINYGLTF_TYPE_VEC3) {
        switch (componentType) {
            case TINYGLTF_COMPONENT_TYPE_BYTE:
                return normalized ? VK_FORMAT_R8G8B8_SNORM : VK_FORMAT_R8G8B8_SINT;
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
                return normalized ? VK_FORMAT_R8G8B8_UNORM : VK_FORMAT_R8G8B8_UINT;
            case TINYGLTF_COMPONENT_TYPE_SHORT:
                return normalized ? VK_FORMAT_R16G16B16_SNORM : VK_FORMAT_R16G16B16_SINT;
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
                return normalized ? VK_FORMAT_R16G16B16_UNORM : VK_FORMAT_R16G16B16_UINT;
            case TINYGLTF_COMPONENT_TYPE_INT:
                return VK_FORMAT_R32G32B32_SINT;
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
                return VK_FORMAT_R32G32B32_UINT;
            case TINYGLTF_COMPONENT_TYPE_FLOAT:
                return VK_FORMAT_R32G32B32_SFLOAT;
            case TINYGLTF_COMPONENT_TYPE_DOUBLE:
                return VK_FORMAT_R64G64B64_SFLOAT;
        }
    } else if (type == TINYGLTF_TYPE_VEC4) {
        switch (componentType) {
            case TINYGLTF_COMPONENT_TYPE_BYTE:
                return normalized ? VK_FORMAT_R8G8B8A8_SNORM : VK_FORMAT_R8G8B8A8_SINT;
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
                return normalized ? VK_FORMAT_R8G8B8A8_UNORM : VK_FORMAT_R8G8B8A8_UINT;
            case TINYGLTF_COMPONENT_TYPE_SHORT:
                return normalized ? VK_FORMAT_R16G16B16A16_SNORM : VK_FORMAT_R16G16B16A16_SINT;
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
                return normalized ? VK_FORMAT_R16G16B16A16_UNORM : VK_FORMAT_R16G16B16A16_UINT;
            case TINYGLTF_COMPONENT_TYPE_INT:
                return VK_FORMAT_R32G32B32A32_SINT;
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
                return VK_FORMAT_R32G32B32A32_UINT;
            case TINYGLTF_COMPONENT_TYPE_FLOAT:
                return VK_FORMAT_R32G32B32A32_SFLOAT;
            case TINYGLTF_COMPONENT_TYPE_DOUBLE:
                return VK_FORMAT_R64G64B64A64_SFLOAT;
        }
    }

    return VK_FORMAT_UNDEFINED;
}

VkPrimitiveTopology GltfLoader::GetVkPrimitiveTopology(int mode) {
    switch (mode) {
        case TINYGLTF_MODE_POINTS:
            return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
        case TINYGLTF_MODE_LINE:
            return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
        case TINYGLTF_MODE_LINE_LOOP:
            return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
        case TINYGLTF_MODE_TRIANGLES:
            return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        case TINYGLTF_MODE_TRIANGLE_STRIP:
            return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
        case TINYGLTF_MODE_TRIANGLE_FAN:
            return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN;
        default:
            return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    }
}

} // namespace aero_boar
