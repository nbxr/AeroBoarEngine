#include "gfx/TextureManager.h"
#include "gfx/BufferUtils.h"
#include <cstring>
#include <iostream>
#include <mutex>
#include <stb_image.h>

bool gfx::TextureManager::is_initialized() { return device != VK_NULL_HANDLE; }

bool gfx::TextureManager::initialize(VkDevice device, VmaAllocator allocator,
                                     VkQueue transfer_queue,
                                     VkQueue graphics_queue,
                                     uint32_t graphics_queue_family_index,
                                     uint32_t transfer_queue_family_index) {
    this->device = device;
    this->allocator = allocator;
    this->graphics_queue = graphics_queue;
    this->transfer_queue = transfer_queue;
    this->graphics_queue_index = graphics_queue_family_index;
    this->transfer_queue_index = transfer_queue_family_index;

    // ADD: Validate allocator
    if (allocator == VK_NULL_HANDLE) {
        // log: "TextureManager::initialize - VmaAllocator is NULL"
        return false;
    }

    VkCommandPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT |
                      VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool_info.queueFamilyIndex = transfer_queue_family_index;

    vkCreateCommandPool(device, &pool_info, nullptr, &upload_command_pool);

    // Allocate one reusable primary command buffer
    VkCommandBufferAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc_info.commandPool = upload_command_pool;
    alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_info.commandBufferCount = 1;

    vkAllocateCommandBuffers(device, &alloc_info, &upload_command_buffer);

    // update and reuse info to allocate command buffer for transitioning
    // ownership
    pool_info.queueFamilyIndex = graphics_queue_family_index;
    vkCreateCommandPool(device, &pool_info, nullptr, &transition_command_pool);

    alloc_info.commandPool = transition_command_pool;
    vkAllocateCommandBuffers(device, &alloc_info, &transition_command_buffer);

    // Create a default sampler for the bindless combined image sampler array.
    // (glTF assets typically use linear filtering; address mode repeat is a
    // reasonable default until material-specific samplers are added.)
    VkSamplerCreateInfo sampler_info{};
    sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampler_info.magFilter = VK_FILTER_LINEAR;
    sampler_info.minFilter = VK_FILTER_LINEAR;
    sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler_info.minLod = 0.0f;
    sampler_info.maxLod = 1.0f;  // All our textures are created with mipLevels=1
    sampler_info.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    if (vkCreateSampler(device, &sampler_info, nullptr, &sampler_handle) !=
        VK_SUCCESS) {
        std::cerr << "[TextureManager] Failed to create main sampler, creating fallback.\n";
        // Create a minimal valid sampler so we never write VK_NULL_HANDLE into descriptors.
        VkSamplerCreateInfo fb{};
        fb.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        fb.magFilter = VK_FILTER_NEAREST;
        fb.minFilter = VK_FILTER_NEAREST;
        fb.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        fb.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        fb.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        fb.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        fb.minLod = 0.0f;
        fb.maxLod = 0.0f;
        if (vkCreateSampler(device, &fb, nullptr, &sampler_handle) != VK_SUCCESS) {
            std::cerr << "[TextureManager] ERROR: even fallback sampler creation failed. Descriptors will be invalid.\n";
        }
    }

    return true;
}

gfx::TextureID gfx::TextureManager::get_texture_handle(const std::string &name,
                                                  const std::string &filepath,
                                                  bool is_srgb) {

    std::scoped_lock lock(texture_mutex);

    // check if key is name or filepath
    const std::string *key = nullptr;
    if (name.empty())
        key = &filepath;
    else
        key = &name;

    // check if texture was already added
    auto it = texture_lookup.find(*key);
    if (it != texture_lookup.end()) {
        return it->second;
    } else {
        // not added previously, so load texture and
        // add to upload list

        gfx::TextureInfo texture_info;
        texture_info.name = name.empty() ? filepath : name;
        texture_info.filepath = filepath;
        texture_info.format = is_srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;

        // use a recycled value if available
        if (!recycle_cache.empty()) {
            gfx::TextureID id = recycle_cache.top();
            recycle_cache.pop();
            if (id < texture_cache.size()) {
                texture_cache[id] = texture_info;
                pending_upload.push_back(id);
                texture_lookup[*key] = id;
                return id;
            }
            // If recycled ID is invalid, treat as new
        }

        texture_cache.push_back(texture_info);
        gfx::TextureID id(texture_cache.size() - 1);
        pending_upload.push_back(id);
        texture_lookup[*key] = id;
        return id;
    }
}

void gfx::TextureManager::remove_texture(const gfx::TextureID texture_id) {
    std::scoped_lock lock(texture_mutex);

    recycle_cache.push(texture_id);
    // remove entry from lookup
    auto &texture_info = texture_cache[texture_id];
    texture_lookup.erase(texture_info.name);
    texture_cleanup.push_back(texture_info);
    // avoid uploading last texture if it's not used
    if (texture_id == texture_cache.size() - 1)
        uploaded_count--;
}

void gfx::TextureManager::upload_textures() {
    std::scoped_lock lock(texture_mutex);

    if (pending_upload.empty())
        return;

    // reuse the same buffer
    vkResetCommandBuffer(upload_command_buffer,
                         VK_COMMAND_BUFFER_RESET_RELEASE_RESOURCES_BIT);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(upload_command_buffer, &beginInfo);

    std::vector<std::pair<VkBuffer, VmaAllocation>> staging_resources{};
    staging_resources.reserve(pending_upload.size());
    pending_queue_transition.reserve(pending_upload.size());

    uint32_t image_views_created = 0;

    for (auto &tex_handle : pending_upload) {
        auto &tex_info = texture_cache[tex_handle.value];

        // 1. Get pixel data and update size info.
        // load_pixel_data always produces at least a 1x1 magenta fallback, so every
        // texture ID we handed out will get a real VkImage + VkImageView. This is
        // critical for GPU-Assisted Validation and partially-bound bindless arrays:
        // we must never put VK_NULL_HANDLE imageView or sampler into a descriptor.
        std::vector<uint8_t> pixels{};
        load_pixel_data(pixels, tex_info);

        if (tex_info.width == 0 || tex_info.height == 0 || pixels.empty()) {
            // Should be extremely rare now (load_pixel_data has fallback).
            std::cerr << "[TextureManager] Texture had invalid size after load; forcing 1x1 placeholder.\n";
            pixels = {255, 0, 255, 255};
            tex_info.width = 1;
            tex_info.height = 1;
            tex_info.channels = 4;
        }

        // 2. Create VkImage via VMA
        VkImageCreateInfo image_info{};
        image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        image_info.imageType = VK_IMAGE_TYPE_2D;
        image_info.format = tex_info.format;
        image_info.extent = {static_cast<uint32_t>(tex_info.width),
                             static_cast<uint32_t>(tex_info.height), 1};
        image_info.mipLevels = 1;
        image_info.arrayLayers = 1;
        image_info.samples = VK_SAMPLE_COUNT_1_BIT;
        image_info.tiling = VK_IMAGE_TILING_OPTIMAL; // GPU-friendly
        image_info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        // Prefer VMA suballocation for typical texture sizes. Dedicated allocs
        // under ~1 MiB trigger BestPractices-small-dedicated-allocation.
        VmaAllocationCreateInfo alloc_info{};
        alloc_info.usage = VMA_MEMORY_USAGE_AUTO;
        const VkDeviceSize approx_bytes =
            VkDeviceSize(tex_info.width) * tex_info.height * 4u;
        if (approx_bytes >= (1u << 20)) {
            alloc_info.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
        }

        VkResult img_res = vmaCreateImage(
            allocator, &image_info, &alloc_info, &tex_info.gpu_image.handle,
            &tex_info.gpu_image.allocation, &tex_info.gpu_image.info);
        if (img_res != VK_SUCCESS) {
            std::cerr << "[TextureManager] vmaCreateImage failed for a texture (will use broken view).\n";
        }

        // 3. Create ImageView
        VkImageViewCreateInfo view_info{};
        view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view_info.image = tex_info.gpu_image.handle;
        view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view_info.format = tex_info.format;
        view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        view_info.subresourceRange.levelCount = 1;
        view_info.subresourceRange.layerCount = 1;

        VkResult view_res = vkCreateImageView(device, &view_info, nullptr,
                                              &tex_info.gpu_image.view);
        if (view_res != VK_SUCCESS || tex_info.gpu_image.view == VK_NULL_HANDLE) {
            std::cerr << "[TextureManager] vkCreateImageView failed for a texture.\n";
        }

        image_views_created++;

        // 4. Upload pixel data via Staging Buffer
        // Create staging buffer
        VkBufferCreateInfo buffer_info{};
        buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        buffer_info.size = pixels.size();
        buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo buffer_alloc_info{};
        buffer_alloc_info.usage =
            VMA_MEMORY_USAGE_CPU_ONLY; // Host-local for fast CPU write

        VkBuffer staging_buffer;
        VmaAllocation staging_allocation;
        vmaCreateBuffer(allocator, &buffer_info, &buffer_alloc_info,
                        &staging_buffer, &staging_allocation, nullptr);

        // 5. Map and copy
        void *data;
        vmaMapMemory(allocator, staging_allocation, &data);
        memcpy(data, pixels.data(), pixels.size());
        vmaUnmapMemory(allocator, staging_allocation);

        // 6. Transition image to transfer destination
        gfx::BufferUtils::transition_image_layout(
            upload_command_buffer, tex_info.gpu_image.handle,
            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            tex_info.width, tex_info.height);

        // Copy buffer to image
        VkBufferImageCopy region{};
        region.bufferOffset = 0;
        region.bufferRowLength = 0;
        region.bufferImageHeight = 0;
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.layerCount = 1;
        region.imageOffset = {0, 0, 0};
        region.imageExtent = {static_cast<uint32_t>(tex_info.width),
                              static_cast<uint32_t>(tex_info.height), 1};

        vkCmdCopyBufferToImage(
            upload_command_buffer, staging_buffer, tex_info.gpu_image.handle,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        // still need to transition back to shader read layout
        // and change queue ownership to graphics queue
        pending_queue_transition.push_back(tex_handle);

        // start ownership release
        gfx::BufferUtils::transition_image_layout( // release barrier
            upload_command_buffer, tex_info.gpu_image.handle,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, tex_info.width,
            tex_info.height, transfer_queue_index,
            graphics_queue_index); // src=transfer,

        // Cleanup staging resources
        staging_resources.push_back(
            std::make_pair(staging_buffer, staging_allocation));
    }

    vkEndCommandBuffer(upload_command_buffer);

    // Submit once
    VkSubmitInfo submit_info{};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &upload_command_buffer;
    vkQueueSubmit(transfer_queue, 1, &submit_info,
                  VK_NULL_HANDLE); // or graphics queue

    vkQueueWaitIdle(transfer_queue); // simple blocking for scene load

    for (auto &[buffer, alloc] : staging_resources) {
        vmaDestroyBuffer(allocator, buffer, alloc);
    }

    pending_upload.clear();
    uploaded_count = texture_cache.size();

    transfer_queue_ownership();
}

void gfx::TextureManager::transfer_queue_ownership() {

    if (pending_queue_transition.empty())
        return;

    // reuse the transition_command_buffer
    vkResetCommandBuffer(transition_command_buffer,
                         VK_COMMAND_BUFFER_RESET_RELEASE_RESOURCES_BIT);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(transition_command_buffer, &beginInfo);

    for (auto tex_handle : pending_queue_transition) {
        auto &tex_info = texture_cache[tex_handle.value];

        // Transition back to shader read layout
        gfx::BufferUtils::transition_image_layout(
            transition_command_buffer, tex_info.gpu_image.handle,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, tex_info.width,
            tex_info.height, transfer_queue_index, graphics_queue_index);
    }

    vkEndCommandBuffer(transition_command_buffer);

    // Submit once
    VkSubmitInfo submit_info{};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &transition_command_buffer;
    vkQueueSubmit(graphics_queue, 1, &submit_info, VK_NULL_HANDLE);

    vkQueueWaitIdle(graphics_queue);

    pending_queue_transition.clear();
}

void gfx::TextureManager::bind_descriptor(uint32_t index, VkDescriptorSet target_set) {
    if (target_set == VK_NULL_HANDLE)
        return;

    // We allocated the variable-count binding for 10000 entries (see init_bindless_descriptor_set).
    // With GPU-Assisted Validation + PARTIALLY_BOUND + runtime non-uniform indexing,
    // it is much safer to explicitly write a valid descriptor for *every* slot in the
    // declared range. Uninitialized / null slots in the tail of the array are a very
    // common cause of internal crashes or false "corruption" inside the validation layer.
    constexpr uint32_t kMaxBindlessTextures = 10000;

    const uint32_t real_count = uploaded_count;
    const uint32_t write_count = kMaxBindlessTextures;

    std::vector<VkDescriptorImageInfo> image_infos(write_count);

    // Choose a safe fallback for unused / tail slots (first real texture if available).
    VkImageView safe_view = VK_NULL_HANDLE;
    if (real_count > 0 && texture_cache[0].gpu_image.view != VK_NULL_HANDLE) {
        safe_view = texture_cache[0].gpu_image.view;
    }

    for (uint32_t i = 0; i < write_count; ++i) {
        image_infos[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        if (i < real_count && texture_cache[i].gpu_image.view != VK_NULL_HANDLE) {
            image_infos[i].imageView = texture_cache[i].gpu_image.view;
        } else {
            image_infos[i].imageView = safe_view;   // tail or failed textures get a real (if any) view
        }
        image_infos[i].sampler = sampler_handle;
    }

    // Write the entire range in one go.
    gfx::BufferUtils::update_descriptor(device, image_infos, target_set, index);

    if (real_count > 0) {
        std::cerr << "[TextureManager] Bound " << real_count << " real textures + tail filled to "
                  << write_count << " total slots for bindless array (binding " << index << ").\n";
    }
}

void gfx::TextureManager::shutdown() {
    if (upload_command_buffer != VK_NULL_HANDLE)
        vkFreeCommandBuffers(device, upload_command_pool, 1,
                             &upload_command_buffer);

    if (upload_command_pool != VK_NULL_HANDLE)
        vkDestroyCommandPool(device, upload_command_pool, nullptr);

    if (transition_command_buffer != VK_NULL_HANDLE)
        vkFreeCommandBuffers(device, transition_command_pool, 1,
                             &transition_command_buffer);

    if (transition_command_pool != VK_NULL_HANDLE)
        vkDestroyCommandPool(device, transition_command_pool, nullptr);

    if (sampler_handle != VK_NULL_HANDLE) {
        vkDestroySampler(device, sampler_handle, nullptr);
        sampler_handle = VK_NULL_HANDLE;
    }

    uint32_t image_views_destroyed = 0;
    // Destroy all AllocatedImage resources in texture_cache
    for (auto &tex_info : texture_cache) {

        if (tex_info.gpu_image.view != VK_NULL_HANDLE) {
            vkDestroyImageView(device, tex_info.gpu_image.view, nullptr);
            image_views_destroyed++;
        }
        if (tex_info.gpu_image.handle != VK_NULL_HANDLE)
            vmaDestroyImage(allocator, tex_info.gpu_image.handle,
                            tex_info.gpu_image.allocation);
    }

    // Clear all caches and lists
    texture_cache.clear();
    texture_lookup.clear();
    pending_upload.clear();
    pending_queue_transition.clear();
    texture_cleanup.clear();
    recycle_cache = std::stack<gfx::TextureID>();
    uploaded_count = 0;
}

void gfx::TextureManager::load_pixel_data(std::vector<uint8_t> &pixels,
                                          gfx::TextureInfo &info) {

    // Load image using stb_image
    int width = 0, height = 0, channels = 0;
    uint8_t *file_pixels =
        stbi_load(info.filepath.c_str(), &width, &height, &channels,
                  4 // Force load as RGBA (4 channels)
        );

    if (file_pixels && width > 0 && height > 0) {
        pixels.resize(static_cast<size_t>(width * height * 4));
        std::memcpy(pixels.data(), file_pixels, pixels.size());
        info.width = static_cast<uint32_t>(width);
        info.height = static_cast<uint32_t>(height);
        info.channels = static_cast<uint32_t>(channels);
        stbi_image_free(file_pixels);
        return;
    }

    // Fallback: 1x1 magenta placeholder (very visible during development)
    if (file_pixels) {
        stbi_image_free(file_pixels);
    }
    std::cerr << "[TextureManager] Failed to load texture '" << info.filepath
              << "' (or invalid size). Using 1x1 placeholder.\n";

    pixels = {255, 0, 255, 255}; // RGBA magenta
    info.width = 1;
    info.height = 1;
    info.channels = 4;
}
