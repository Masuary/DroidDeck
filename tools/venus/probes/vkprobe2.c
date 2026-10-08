// Image side of the PowerVR dma_buf question: what the driver reports for a LINEAR-modifier
// RGBA8 image exported/imported as dma_buf, and whether exporting and re-importing actually work.
#include <vulkan/vulkan.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>

#define DRM_FORMAT_MOD_LINEAR 0ULL

int main(void) {
    VkApplicationInfo app = { .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, .apiVersion = VK_API_VERSION_1_2 };
    VkInstanceCreateInfo ici = { .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &app };
    VkInstance inst; vkCreateInstance(&ici, NULL, &inst);
    uint32_t n = 1; VkPhysicalDevice pd; vkEnumeratePhysicalDevices(inst, &n, &pd);

    // Modifiers the driver lists for RGBA8
    VkDrmFormatModifierPropertiesListEXT ml = { .sType = VK_STRUCTURE_TYPE_DRM_FORMAT_MODIFIER_PROPERTIES_LIST_EXT };
    VkFormatProperties2 fp = { .sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2, .pNext = &ml };
    vkGetPhysicalDeviceFormatProperties2(pd, VK_FORMAT_R8G8B8A8_UNORM, &fp);
    VkDrmFormatModifierPropertiesEXT mods[16]; ml.drmFormatModifierCount = ml.drmFormatModifierCount > 16 ? 16 : ml.drmFormatModifierCount; ml.pDrmFormatModifierProperties = mods;
    vkGetPhysicalDeviceFormatProperties2(pd, VK_FORMAT_R8G8B8A8_UNORM, &fp);
    for (uint32_t i = 0; i < ml.drmFormatModifierCount; i++) printf("RGBA8 modifier 0x%llx planes %u features 0x%x\n", (unsigned long long)mods[i].drmFormatModifier, mods[i].drmFormatModifierPlaneCount, mods[i].drmFormatModifierTilingFeatures);

    VkExternalMemoryHandleTypeFlagBits types[] = { VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT, VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT };
    const char *names[] = { "dma_buf", "opaque_fd" };
    for (int i = 0; i < 2; i++) {
        VkPhysicalDeviceImageDrmFormatModifierInfoEXT mi = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_DRM_FORMAT_MODIFIER_INFO_EXT, .drmFormatModifier = DRM_FORMAT_MOD_LINEAR, .sharingMode = VK_SHARING_MODE_EXCLUSIVE };
        VkPhysicalDeviceExternalImageFormatInfo ei = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO, .pNext = &mi, .handleType = types[i] };
        VkPhysicalDeviceImageFormatInfo2 ii = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2, .pNext = &ei, .format = VK_FORMAT_R8G8B8A8_UNORM, .type = VK_IMAGE_TYPE_2D,
            .tiling = VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT, .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT };
        VkExternalImageFormatProperties ep = { .sType = VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES };
        VkImageFormatProperties2 ip = { .sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2, .pNext = &ep };
        VkResult r = vkGetPhysicalDeviceImageFormatProperties2(pd, &ii, &ip);
        printf("image LINEAR %-9s: ret %d features 0x%x exportFrom 0x%x compatible 0x%x\n", names[i], r, ep.externalMemoryProperties.externalMemoryFeatures,
               ep.externalMemoryProperties.exportFromImportedHandleTypes, ep.externalMemoryProperties.compatibleHandleTypes);
    }

    float prio = 1; VkDeviceQueueCreateInfo q = { .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, .queueCount = 1, .pQueuePriorities = &prio };
    const char *exts[] = { "VK_KHR_external_memory_fd", "VK_EXT_external_memory_dma_buf", "VK_EXT_image_drm_format_modifier" };
    VkDeviceCreateInfo dci = { .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, .queueCreateInfoCount = 1, .pQueueCreateInfos = &q, .enabledExtensionCount = 3, .ppEnabledExtensionNames = exts };
    VkDevice dev; if (vkCreateDevice(pd, &dci, NULL, &dev)) { puts("no device"); return 1; }
    PFN_vkGetMemoryFdKHR getFd = (PFN_vkGetMemoryFdKHR)vkGetDeviceProcAddr(dev, "vkGetMemoryFdKHR");

    // Export: a LINEAR-modifier image with dma_buf export
    uint64_t lin = DRM_FORMAT_MOD_LINEAR;
    VkImageDrmFormatModifierListCreateInfoEXT mlc = { .sType = VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_LIST_CREATE_INFO_EXT, .drmFormatModifierCount = 1, .pDrmFormatModifiers = &lin };
    VkExternalMemoryImageCreateInfo emi = { .sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO, .pNext = &mlc, .handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT };
    VkImageCreateInfo ci = { .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, .pNext = &emi, .imageType = VK_IMAGE_TYPE_2D, .format = VK_FORMAT_R8G8B8A8_UNORM, .extent = {1280, 720, 1},
        .mipLevels = 1, .arrayLayers = 1, .samples = 1, .tiling = VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT, .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT };
    VkImage img; VkResult r = vkCreateImage(dev, &ci, NULL, &img); printf("create LINEAR dma_buf image: %d\n", r); if (r) return 0;
    VkMemoryRequirements mr; vkGetImageMemoryRequirements(dev, img, &mr);
    VkMemoryDedicatedAllocateInfo ded = { .sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO, .image = img };
    VkExportMemoryAllocateInfo ex = { .sType = VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO, .pNext = &ded, .handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT };
    uint32_t t = __builtin_ctz(mr.memoryTypeBits);
    VkMemoryAllocateInfo ai = { .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .pNext = &ex, .allocationSize = mr.size, .memoryTypeIndex = t };
    VkDeviceMemory m; r = vkAllocateMemory(dev, &ai, NULL, &m); printf("alloc exportable image memory (type %u, %llu bytes): %d\n", t, (unsigned long long)mr.size, r); if (r) return 0;
    r = vkBindImageMemory(dev, img, m, 0); printf("bind: %d\n", r);
    VkMemoryGetFdInfoKHR gi = { .sType = VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR, .memory = m, .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT };
    int fd = -1; r = getFd(dev, &gi, &fd);
    char link[PATH_MAX] = "", path[64]; snprintf(path, sizeof path, "/proc/self/fd/%d", fd); ssize_t len = readlink(path, link, sizeof link - 1); if (len > 0) link[len] = 0;
    printf("export image memory as dma_buf: ret %d fd -> '%s' size %lld\n", r, link, (long long)lseek(fd, 0, SEEK_END));

    // Import that dma_buf into a second image
    VkImage img2; vkCreateImage(dev, &ci, NULL, &img2);
    VkMemoryDedicatedAllocateInfo ded2 = { .sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO, .image = img2 };
    VkImportMemoryFdInfoKHR im = { .sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR, .pNext = &ded2, .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT, .fd = dup(fd) };
    VkMemoryAllocateInfo ai2 = { .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .pNext = &im, .allocationSize = mr.size, .memoryTypeIndex = t };
    VkDeviceMemory m2; r = vkAllocateMemory(dev, &ai2, NULL, &m2); printf("import dma_buf into image memory: %d\n", r);
    if (!r) printf("bind imported: %d\n", vkBindImageMemory(dev, img2, m2, 0));
    PFN_vkGetMemoryFdPropertiesKHR gp = (PFN_vkGetMemoryFdPropertiesKHR)vkGetDeviceProcAddr(dev, "vkGetMemoryFdPropertiesKHR");
    VkMemoryFdPropertiesKHR fdp = { .sType = VK_STRUCTURE_TYPE_MEMORY_FD_PROPERTIES_KHR };
    r = gp(dev, VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT, fd, &fdp); printf("vkGetMemoryFdPropertiesKHR(dma_buf): %d memoryTypeBits 0x%x\n", r, fdp.memoryTypeBits);
    return 0;
}
