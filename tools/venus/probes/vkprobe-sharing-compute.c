// Full-size shared-image diagnostic. Uses two native Vulkan instances (one process).
// --compute writes directly into the shared LINEAR image; --copy composes in an
// OPTIMAL image and copies into a transfer-only shared output. Both use GPU fences.
// Direct LINEAR storage is diagnostic only: PowerVR omits STORAGE_IMAGE in its
// modifier features even though creation succeeds. A pass is not conformance proof.
// --legacy models the compositor's UNDEFINED acquire and missing source release; its reads
// are deliberately undefined by Vulkan. The default preserves GENERAL and releases ownership.
#include <vulkan/vulkan.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "fill_spv.h"

#define W 1280
#define H 720
#define CHECK(call) do { VkResult r_ = (call); if (r_ != VK_SUCCESS) { \
    fprintf(stderr, "%s: %d (line %d)\n", #call, r_, __LINE__); exit(2); } } while (0)

typedef struct {
    VkInstance instance;
    VkPhysicalDevice physical;
    VkDevice device;
    VkQueue queue;
    uint32_t family;
    VkCommandPool pool;
    VkCommandBuffer cmd;
    VkFence fence;
} Context;

static uint32_t memory_type(Context *c, uint32_t bits, VkMemoryPropertyFlags flags) {
    VkPhysicalDeviceMemoryProperties p;
    vkGetPhysicalDeviceMemoryProperties(c->physical, &p);
    for (uint32_t i = 0; i < p.memoryTypeCount; i++)
        if ((bits & (1u << i)) && (p.memoryTypes[i].propertyFlags & flags) == flags) return i;
    fprintf(stderr, "no memory type for bits=%x flags=%x\n", bits, flags);
    exit(2);
}

static Context context(int foreign) {
    Context c = {0};
    VkApplicationInfo app = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, .apiVersion = VK_API_VERSION_1_2};
    VkInstanceCreateInfo ici = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &app};
    CHECK(vkCreateInstance(&ici, NULL, &c.instance));
    uint32_t n = 1;
    VkResult r = vkEnumeratePhysicalDevices(c.instance, &n, &c.physical);
    if ((r != VK_SUCCESS && r != VK_INCOMPLETE) || !n) { fputs("no GPU\n", stderr); exit(2); }
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(c.physical, &props);
    printf("GPU: %s\n", props.deviceName);
    uint32_t nq = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(c.physical, &nq, NULL);
    VkQueueFamilyProperties *queues = calloc(nq, sizeof(*queues));
    if (!queues) exit(2);
    vkGetPhysicalDeviceQueueFamilyProperties(c.physical, &nq, queues);
    for (c.family = 0; c.family < nq; c.family++)
        if (queues[c.family].queueFlags & VK_QUEUE_GRAPHICS_BIT) break;
    free(queues);
    if (c.family == nq) { fputs("no graphics queue\n", stderr); exit(2); }
    float priority = 1.0f;
    VkDeviceQueueCreateInfo qci = {.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = c.family, .queueCount = 1, .pQueuePriorities = &priority};
    const char *extensions[] = {VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME,
        VK_EXT_EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME, VK_EXT_IMAGE_DRM_FORMAT_MODIFIER_EXTENSION_NAME,
        VK_EXT_QUEUE_FAMILY_FOREIGN_EXTENSION_NAME};
    VkDeviceCreateInfo dci = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = 1, .pQueueCreateInfos = &qci,
        .enabledExtensionCount = foreign ? 4u : 3u, .ppEnabledExtensionNames = extensions};
    CHECK(vkCreateDevice(c.physical, &dci, NULL, &c.device));
    vkGetDeviceQueue(c.device, c.family, 0, &c.queue);
    VkCommandPoolCreateInfo pci = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT, .queueFamilyIndex = c.family};
    CHECK(vkCreateCommandPool(c.device, &pci, NULL, &c.pool));
    VkCommandBufferAllocateInfo cai = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = c.pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1};
    CHECK(vkAllocateCommandBuffers(c.device, &cai, &c.cmd));
    VkFenceCreateInfo fci = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    CHECK(vkCreateFence(c.device, &fci, NULL, &c.fence));
    return c;
}

static void begin(Context *c) {
    CHECK(vkResetCommandBuffer(c->cmd, 0));
    VkCommandBufferBeginInfo bi = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
    CHECK(vkBeginCommandBuffer(c->cmd, &bi));
}

static void submit(Context *c) {
    CHECK(vkEndCommandBuffer(c->cmd));
    VkSubmitInfo si = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &c->cmd};
    CHECK(vkQueueSubmit(c->queue, 1, &si, c->fence));
    // Host waits serialize producer and consumer deliberately: this tests memory visibility,
    // independently of the compositor's separate Wayland fence/presentation path.
    CHECK(vkWaitForFences(c->device, 1, &c->fence, VK_TRUE, 5000000000ULL));
    CHECK(vkResetFences(c->device, 1, &c->fence));
}

static void barrier(Context *c, VkImage image, VkImageLayout old, VkImageLayout next,
                    uint32_t source, uint32_t dest, VkAccessFlags reads, VkAccessFlags writes) {
    VkImageMemoryBarrier b = {.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = reads, .dstAccessMask = writes, .oldLayout = old, .newLayout = next,
        .srcQueueFamilyIndex = source, .dstQueueFamilyIndex = dest, .image = image,
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
    vkCmdPipelineBarrier(c->cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                        0, 0, NULL, 0, NULL, 1, &b);
}

static void finish(Context *c) {
    vkDestroyFence(c->device, c->fence, NULL);
    vkDestroyCommandPool(c->device, c->pool, NULL);
    vkDestroyDevice(c->device, NULL);
    vkDestroyInstance(c->instance, NULL);
}

int main(int argc, char **argv) {
    int legacy = 0, compute = 0, staging = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--legacy")) legacy = 1;
        else if (!strcmp(argv[i], "--compute")) compute = 1;
        else if (!strcmp(argv[i], "--copy")) compute = staging = 1;
        else { fprintf(stderr, "usage: %s [--legacy] [--compute|--copy]\n", argv[0]); return 2; }
    }
    Context producer = context(1), consumer = context(!legacy);
    printf("mode: %s\n", legacy ? "legacy (undefined contents, diagnostic only)" : "preserve and release");
    uint64_t linear = 0;
    VkImageDrmFormatModifierListCreateInfoEXT mods = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_LIST_CREATE_INFO_EXT,
        .drmFormatModifierCount = 1, .pDrmFormatModifiers = &linear};
    VkExternalMemoryImageCreateInfo external = {.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO,
        .pNext = &mods, .handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT};
    VkImageCreateInfo ici = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, .pNext = &external,
        .imageType = VK_IMAGE_TYPE_2D, .format = VK_FORMAT_R8G8B8A8_UNORM, .extent = {W, H, 1},
        .mipLevels = 1, .arrayLayers = 1, .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT,
        .usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE, .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED};
    if (compute && !staging) ici.usage |= VK_IMAGE_USAGE_STORAGE_BIT;
    VkImage source, imported;
    CHECK(vkCreateImage(producer.device, &ici, NULL, &source));
    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(producer.device, source, &req);
    VkMemoryDedicatedAllocateInfo dedicated = {.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO, .image = source};
    VkExportMemoryAllocateInfo export = {.sType = VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO,
        .pNext = &dedicated, .handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT};
    VkMemoryAllocateInfo mai = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .pNext = &export,
        .allocationSize = req.size, .memoryTypeIndex = memory_type(&producer, req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)};
    VkDeviceMemory source_mem, imported_mem;
    CHECK(vkAllocateMemory(producer.device, &mai, NULL, &source_mem));
    CHECK(vkBindImageMemory(producer.device, source, source_mem, 0));
    PFN_vkGetMemoryFdKHR get_fd = (PFN_vkGetMemoryFdKHR)vkGetDeviceProcAddr(producer.device, "vkGetMemoryFdKHR");
    PFN_vkGetMemoryFdPropertiesKHR fd_props = (PFN_vkGetMemoryFdPropertiesKHR)vkGetDeviceProcAddr(consumer.device, "vkGetMemoryFdPropertiesKHR");
    if (!get_fd || !fd_props) { fputs("missing DMA-BUF entrypoints\n", stderr); return 2; }
    VkMemoryGetFdInfoKHR fi = {.sType = VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR,
        .memory = source_mem, .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT};
    int fd;
    CHECK(get_fd(producer.device, &fi, &fd));
    VkImageSubresource sub = {.aspectMask = VK_IMAGE_ASPECT_MEMORY_PLANE_0_BIT_EXT};
    VkSubresourceLayout layout;
    vkGetImageSubresourceLayout(producer.device, source, &sub, &layout);
    VkSubresourceLayout plane = {.offset = layout.offset, .rowPitch = layout.rowPitch};
    VkImageDrmFormatModifierExplicitCreateInfoEXT explicit = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_EXPLICIT_CREATE_INFO_EXT,
        .drmFormatModifier = linear, .drmFormatModifierPlaneCount = 1, .pPlaneLayouts = &plane};
    external.pNext = &explicit;
    ici.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    CHECK(vkCreateImage(consumer.device, &ici, NULL, &imported));
    VkMemoryFdPropertiesKHR properties = {.sType = VK_STRUCTURE_TYPE_MEMORY_FD_PROPERTIES_KHR};
    CHECK(fd_props(consumer.device, VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT, fd, &properties));
    vkGetImageMemoryRequirements(consumer.device, imported, &req);
    dedicated.image = imported;
    VkImportMemoryFdInfoKHR import = {.sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR,
        .pNext = &dedicated, .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT, .fd = fd};
    mai.pNext = &import;
    mai.allocationSize = req.size;
    mai.memoryTypeIndex = memory_type(&consumer, req.memoryTypeBits & properties.memoryTypeBits, 0);
    CHECK(vkAllocateMemory(consumer.device, &mai, NULL, &imported_mem)); // Vulkan owns fd now.
    CHECK(vkBindImageMemory(consumer.device, imported, imported_mem, 0));

    VkImage target = source;
    VkDeviceMemory target_mem = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkDescriptorSetLayout dsl = VK_NULL_HANDLE;
    VkPipelineLayout pl = VK_NULL_HANDLE;
    VkShaderModule sm = VK_NULL_HANDLE;
    VkPipeline pipe = VK_NULL_HANDLE;
    VkDescriptorPool dp = VK_NULL_HANDLE;
    VkDescriptorSet ds = VK_NULL_HANDLE;
    if (staging) {
        ici.pNext = NULL;
        ici.tiling = VK_IMAGE_TILING_OPTIMAL;
        ici.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        CHECK(vkCreateImage(producer.device, &ici, NULL, &target));
        vkGetImageMemoryRequirements(producer.device, target, &req);
        mai.pNext = NULL;
        mai.allocationSize = req.size;
        mai.memoryTypeIndex = memory_type(&producer, req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        CHECK(vkAllocateMemory(producer.device, &mai, NULL, &target_mem));
        CHECK(vkBindImageMemory(producer.device, target, target_mem, 0));
    }
    if (compute) {
        VkImageViewCreateInfo vci = {.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = target, .viewType = VK_IMAGE_VIEW_TYPE_2D, .format = VK_FORMAT_R8G8B8A8_UNORM,
            .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
        CHECK(vkCreateImageView(producer.device, &vci, NULL, &view));
        VkDescriptorSetLayoutBinding binding = {.binding = 0, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT};
        VkDescriptorSetLayoutCreateInfo dlci = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .bindingCount = 1, .pBindings = &binding};
        CHECK(vkCreateDescriptorSetLayout(producer.device, &dlci, NULL, &dsl));
        VkPushConstantRange pcr = {VK_SHADER_STAGE_COMPUTE_BIT, 0, 16};
        VkPipelineLayoutCreateInfo plci = {.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .setLayoutCount = 1, .pSetLayouts = &dsl, .pushConstantRangeCount = 1, .pPushConstantRanges = &pcr};
        CHECK(vkCreatePipelineLayout(producer.device, &plci, NULL, &pl));
        VkShaderModuleCreateInfo smci = {.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
            .codeSize = sizeof fill_spv, .pCode = fill_spv};
        CHECK(vkCreateShaderModule(producer.device, &smci, NULL, &sm));
        VkComputePipelineCreateInfo cpci = {.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
            .stage = {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .stage = VK_SHADER_STAGE_COMPUTE_BIT, .module = sm, .pName = "main"}, .layout = pl};
        CHECK(vkCreateComputePipelines(producer.device, VK_NULL_HANDLE, 1, &cpci, NULL, &pipe));
        VkDescriptorPoolSize ps = {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1};
        VkDescriptorPoolCreateInfo dpci = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .maxSets = 1, .poolSizeCount = 1, .pPoolSizes = &ps};
        CHECK(vkCreateDescriptorPool(producer.device, &dpci, NULL, &dp));
        VkDescriptorSetAllocateInfo dsai = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .descriptorPool = dp, .descriptorSetCount = 1, .pSetLayouts = &dsl};
        CHECK(vkAllocateDescriptorSets(producer.device, &dsai, &ds));
        VkDescriptorImageInfo dii = {.imageView = view, .imageLayout = VK_IMAGE_LAYOUT_GENERAL};
        VkWriteDescriptorSet w = {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = ds,
            .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, .pImageInfo = &dii};
        vkUpdateDescriptorSets(producer.device, 1, &w, 0, NULL);
    }
    printf("producer: %s\n", staging ? "OPTIMAL compute then copy to LINEAR" : compute ? "LINEAR compute" : "LINEAR transfer clear");

    VkBufferCreateInfo bci = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = W * H * 4, .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT};
    VkBuffer readback;
    CHECK(vkCreateBuffer(consumer.device, &bci, NULL, &readback));
    vkGetBufferMemoryRequirements(consumer.device, readback, &req);
    mai.pNext = NULL;
    mai.allocationSize = req.size;
    mai.memoryTypeIndex = memory_type(&consumer, req.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VkDeviceMemory read_mem;
    CHECK(vkAllocateMemory(consumer.device, &mai, NULL, &read_mem));
    CHECK(vkBindBufferMemory(consumer.device, readback, read_mem, 0));
    unsigned char *pixels;
    CHECK(vkMapMemory(consumer.device, read_mem, 0, VK_WHOLE_SIZE, 0, (void **)&pixels));
    int failures = 0;
    for (int frame = 0; frame < 6; frame++) {
        begin(&producer);
        VkClearColorValue color = {.float32 = {0, 0, 0, 1}};
        color.float32[frame % 3] = 1;
        if (compute) {
            barrier(&producer, target, frame ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_UNDEFINED,
                VK_IMAGE_LAYOUT_GENERAL, frame && !staging ? VK_QUEUE_FAMILY_FOREIGN_EXT : VK_QUEUE_FAMILY_IGNORED,
                frame && !staging ? producer.family : VK_QUEUE_FAMILY_IGNORED,
                staging && frame ? VK_ACCESS_TRANSFER_READ_BIT : 0, VK_ACCESS_SHADER_WRITE_BIT);
            vkCmdBindPipeline(producer.cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipe);
            vkCmdBindDescriptorSets(producer.cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pl, 0, 1, &ds, 0, NULL);
            vkCmdPushConstants(producer.cmd, pl, VK_SHADER_STAGE_COMPUTE_BIT, 0, 16, color.float32);
            vkCmdDispatch(producer.cmd, W / 8, H / 8, 1);
        }
        if (!compute || staging) {
            barrier(&producer, source, frame ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_UNDEFINED,
                VK_IMAGE_LAYOUT_GENERAL, frame ? VK_QUEUE_FAMILY_FOREIGN_EXT : VK_QUEUE_FAMILY_IGNORED,
                frame ? producer.family : VK_QUEUE_FAMILY_IGNORED, 0, VK_ACCESS_TRANSFER_WRITE_BIT);
            if (staging) {
                barrier(&producer, target, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL,
                    VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT);
                VkImageCopy copy = {.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                    .dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, .extent = {W, H, 1}};
                vkCmdCopyImage(producer.cmd, target, VK_IMAGE_LAYOUT_GENERAL, source, VK_IMAGE_LAYOUT_GENERAL, 1, &copy);
            } else {
                VkImageSubresourceRange range = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
                vkCmdClearColorImage(producer.cmd, source, VK_IMAGE_LAYOUT_GENERAL, &color, 1, &range);
            }
        }
        barrier(&producer, source, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL,
            producer.family, VK_QUEUE_FAMILY_FOREIGN_EXT,
            compute && !staging ? VK_ACCESS_SHADER_WRITE_BIT : VK_ACCESS_TRANSFER_WRITE_BIT, 0);
        submit(&producer);

        begin(&consumer);
        barrier(&consumer, imported, legacy ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_GENERAL,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_QUEUE_FAMILY_FOREIGN_EXT, consumer.family, 0, VK_ACCESS_TRANSFER_READ_BIT);
        VkBufferImageCopy region = {.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, .imageExtent = {W, H, 1}};
        vkCmdCopyImageToBuffer(consumer.cmd, imported, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback, 1, &region);
        if (!legacy) barrier(&consumer, imported, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL,
            consumer.family, VK_QUEUE_FAMILY_FOREIGN_EXT, VK_ACCESS_TRANSFER_READ_BIT, 0);
        VkBufferMemoryBarrier host = {.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT, .dstAccessMask = VK_ACCESS_HOST_READ_BIT,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .buffer = readback, .size = VK_WHOLE_SIZE};
        vkCmdPipelineBarrier(consumer.cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
            0, 0, NULL, 1, &host, 0, NULL);
        submit(&consumer);
        unsigned char expected[4] = {0, 0, 0, 255}; expected[frame % 3] = 255;
        unsigned correct = 0;
        for (int pixel = 0; pixel < W * H; pixel++) correct += !memcmp(pixels + pixel * 4, expected, 4);
        printf("frame %d: %u/%d correct, first pixel %u,%u,%u,%u\n", frame + 1, correct, W * H,
               pixels[0], pixels[1], pixels[2], pixels[3]);
        failures += correct != W * H;
    }
    vkUnmapMemory(consumer.device, read_mem);
    vkDestroyBuffer(consumer.device, readback, NULL);
    vkFreeMemory(consumer.device, read_mem, NULL);
    vkDestroyImage(consumer.device, imported, NULL);
    vkFreeMemory(consumer.device, imported_mem, NULL);
    if (compute) {
        vkDestroyDescriptorPool(producer.device, dp, NULL);
        vkDestroyPipeline(producer.device, pipe, NULL);
        vkDestroyShaderModule(producer.device, sm, NULL);
        vkDestroyPipelineLayout(producer.device, pl, NULL);
        vkDestroyDescriptorSetLayout(producer.device, dsl, NULL);
        vkDestroyImageView(producer.device, view, NULL);
    }
    if (staging) {
        vkDestroyImage(producer.device, target, NULL);
        vkFreeMemory(producer.device, target_mem, NULL);
    }
    vkDestroyImage(producer.device, source, NULL);
    vkFreeMemory(producer.device, source_mem, NULL);
    finish(&consumer); finish(&producer);
    printf("%s: %d mismatched frames\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
