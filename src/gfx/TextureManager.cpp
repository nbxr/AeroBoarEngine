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
                                     uint32_t transfer_queue_family_index,
                                     VkDescriptorSet descriptor_set) {
    this->device = device;
    this->allocator = allocator;
    this->graphics_queue = graphics_queue;
    this->transfer_queue = transfer_queue;
    this->graphics_queue_index = graphics_queue_family_index;
    this->transfer_queue_index = transfer_queue_family_index;
    this->descriptor_set = descriptor_set;

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
    sampler_info.maxLod = VK_LOD_CLAMP_NONE;
    sampler_info.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    if (vkCreateSampler(device, &sampler_info, nullptr, &sampler_handle) !=
        VK_SUCCESS) {
        // Non-fatal for desktop dev; textures will still upload but binding may
        // be incomplete until sampler is valid.
    }

    return true;
}

gfx::TextureID gfx::TextureManager::get_texture_handle(const std::string &name,
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

        gfx::TextureInfo texture_info;
        texture_info.name = name.empty() ? filepath : name;
        texture_info.filepath = filepath;

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

        // 1. Get pixel data and update size info
        std::vector<uint8_t> pixels{};
        load_pixel_data(pixels, tex_info);

        if (tex_info.width == 0 || tex_info.height == 0 || pixels.empty()) {
            std::cerr << "[TextureManager] Skipping texture with invalid dimensions after load.\n";
            continue;
        }

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

void gfx::TextureManager::bind_descriptor(uint32_t index) {
    if (uploaded_count == 0 || descriptor_set == VK_NULL_HANDLE)
        return;

    std::vector<VkDescriptorImageInfo> image_infos;
    image_infos.resize(uploaded_count);
    for (size_t i = 0; i < uploaded_count; i++) {
        image_infos[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        image_infos[i].imageView = texture_cache[i].gpu_image.view;
        image_infos[i].sampler = sampler_handle;
    }

    gfx::BufferUtils::update_descriptor(device, image_infos, descriptor_set,
                                         index);
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
