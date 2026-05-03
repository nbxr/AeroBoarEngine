#include "TextureManager.h"
#include "core/BufferUtils.h"
#include <cstring>
#include <mutex>
#include <stb_image.h>

bool gfx::TextureManager::is_initialized() { return device != VK_NULL_HANDLE; }

bool gfx::TextureManager::initialize(VkDevice device, VmaAllocator allocator,
                                     VkQueue transfer_queue,
                                     uint32_t graphics_queue_family_index,
                                     uint32_t transfer_queue_family_index) {
    this->device = device;
    this->allocator = allocator;
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

    return true;
}

TextureID gfx::TextureManager::get_texture_handle(const std::string &name,
                                                  const std::string &filepath) {

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

        // TODO: load pixels
        std::vector<uint8_t> pixels{};
        int width, height, channels;

        gfx::TextureInfo texture_info;
        texture_info.name = name.empty() ? filepath : name;
        texture_info.filepath = filepath;

        // use a recycled value if available
        if (!recycle_cache.empty()) {
            TextureID id = recycle_cache.top();
            recycle_cache.pop();
            if (id < texture_cache.size()) {
                texture_cache[id] = texture_info;
                pending_upload.push_back(id);
                texture_lookup[name] = id;
                return id;
            }
            // If recycled ID is invalid, treat as new
        }

        texture_cache.push_back(texture_info);
        TextureID id(texture_cache.size() - 1);
        pending_upload.push_back(id);
        texture_lookup[name] = id;
        return id;
    }
}

void gfx::TextureManager::remove_texture(const TextureID texture_id) {
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

        // 1. Get pixel data and update size info
        std::vector<uint8_t> pixels{};
        load_pixel_data(pixels, tex_info);

        // 2. Create VkImage via VMA
        VkImageCreateInfo image_info{};
        image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        image_info.imageType = VK_IMAGE_TYPE_2D;
        image_info.format = VK_FORMAT_R8G8B8A8_UNORM;
        image_info.extent = {static_cast<uint32_t>(tex_info.width),
                             static_cast<uint32_t>(tex_info.height), 1};
        image_info.mipLevels = 1;
        image_info.arrayLayers = 1;
        image_info.samples = VK_SAMPLE_COUNT_1_BIT;
        image_info.tiling = VK_IMAGE_TILING_OPTIMAL; // GPU-friendly
        image_info.usage = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        VmaAllocationCreateInfo alloc_info{};
        alloc_info.usage = VMA_MEMORY_USAGE_AUTO;
        alloc_info.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;

        vmaCreateImage(
            allocator, &image_info, &alloc_info, &tex_info.gpu_image.handle,
            &tex_info.gpu_image.allocation, &tex_info.gpu_image.info);

        // 3. Create ImageView
        VkImageViewCreateInfo view_info{};
        view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view_info.image = tex_info.gpu_image.handle;
        view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view_info.format = tex_info.format;
        view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        view_info.subresourceRange.levelCount = 1;
        view_info.subresourceRange.layerCount = 1;

        vkCreateImageView(device, &view_info, nullptr,
                          &tex_info.gpu_image.view);
        
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
        core::BufferUtils::transition_image_layout(
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
}

void gfx::TextureManager::finalize_layout(VkBuffer command_buffer) {

    if (pending_queue_transition.empty())
        return;

    for (auto tex_handle : pending_queue_transition) {
        auto &tex_info = texture_cache[tex_handle.value];

        // Transition back to shader read layout
        core::BufferUtils::transition_image_layout(
            upload_command_buffer, tex_info.gpu_image.handle,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, tex_info.width,
            tex_info.height, transfer_queue_index, graphics_queue_index);
    }

    pending_queue_transition.clear();
}

void gfx::TextureManager::bind_descriptor(uint32_t index) {

    // In your descriptor set update:
    std::vector<VkDescriptorImageInfo> image_infos{};
    image_infos.reserve(uploaded_count);
    for (size_t i = 0; i < uploaded_count; i++) {
        image_infos[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        image_infos[i].imageView = texture_cache[i].gpu_image.view;
        image_infos[i].sampler = sampler_handle; // shared or per-texture
    }

    // Update descriptor set with pTexelBuffers = image_infos.data()
}

void gfx::TextureManager::shutdown() {
    if (upload_command_buffer != VK_NULL_HANDLE)
        vkFreeCommandBuffers(device, upload_command_pool, 1,
                             &upload_command_buffer);

    if (upload_command_pool != VK_NULL_HANDLE)
        vkDestroyCommandPool(device, upload_command_pool, nullptr);

    uint32_t image_views_destroyed = 0;
    // Destroy all AllocatedImage resources in texture_cache
    for (auto &tex_info : texture_cache) {

        if (tex_info.gpu_image.view != VK_NULL_HANDLE){
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
    recycle_cache = std::stack<TextureID>();
    uploaded_count = 0;
}

void gfx::TextureManager::load_pixel_data(std::vector<uint8_t> &pixels,
                                          gfx::TextureInfo &info) {

    // Load image using stb_image
    int width, height, channels;
    uint8_t *file_pixels =
        stbi_load(info.filepath.c_str(), &width, &height, &channels,
                  4 // Force load as RGBA (4 channels)
        );

    if (!file_pixels) {
        // Handle error: log or throw
        return;
    }

    // Resize output vector to match loaded image size
    pixels.resize(static_cast<size_t>(width * height * 4));

    // Copy data to output vector
    std::memcpy(pixels.data(), file_pixels, pixels.size());

    // Update texture info metadata
    info.width = width;
    info.height = height;
    info.channels = channels;

    // Free stb_image buffer
    stbi_image_free(file_pixels);
}
