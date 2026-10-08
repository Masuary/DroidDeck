// Asks Android's Vulkan driver what Venus needs to know about exporting memory, and checks whether
// an exported "opaque" fd is really a dma_buf (mmap-able, sized). Build: NDK clang, link -lvulkan.
#include <vulkan/vulkan.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <limits.h>

static const char *feat(VkExternalMemoryFeatureFlags f) {
    static char b[64]; b[0] = 0;
    if (f & VK_EXTERNAL_MEMORY_FEATURE_DEDICATED_ONLY_BIT) strcat(b, "dedicated-only ");
    if (f & VK_EXTERNAL_MEMORY_FEATURE_EXPORTABLE_BIT) strcat(b, "exportable ");
    if (f & VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT) strcat(b, "importable ");
    return b;
}

int main(void) {
    VkApplicationInfo app = { .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, .apiVersion = VK_API_VERSION_1_1 };
    VkInstanceCreateInfo ici = { .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &app };
    VkInstance inst; if (vkCreateInstance(&ici, NULL, &inst)) { puts("no instance"); return 1; }
    uint32_t n = 1; VkPhysicalDevice pd; vkEnumeratePhysicalDevices(inst, &n, &pd);
    VkPhysicalDeviceProperties p; vkGetPhysicalDeviceProperties(pd, &p); printf("device: %s\n", p.deviceName);

    VkExternalMemoryHandleTypeFlagBits types[] = { VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT, VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT };
    const char *names[] = { "dma_buf", "opaque_fd" };
    for (int i = 0; i < 2; i++) {
        VkPhysicalDeviceExternalBufferInfo bi = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_BUFFER_INFO, .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT, .handleType = types[i] };
        VkExternalBufferProperties bp = { .sType = VK_STRUCTURE_TYPE_EXTERNAL_BUFFER_PROPERTIES };
        vkGetPhysicalDeviceExternalBufferProperties(pd, &bi, &bp);
        printf("buffer %-9s: features [%s] exportFrom 0x%x compatible 0x%x\n", names[i], feat(bp.externalMemoryProperties.externalMemoryFeatures),
               bp.externalMemoryProperties.exportFromImportedHandleTypes, bp.externalMemoryProperties.compatibleHandleTypes);
    }

    VkPhysicalDeviceMemoryProperties mp; vkGetPhysicalDeviceMemoryProperties(pd, &mp);
    float prio = 1; VkDeviceQueueCreateInfo q = { .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, .queueCount = 1, .pQueuePriorities = &prio };
    const char *exts[] = { "VK_KHR_external_memory_fd", "VK_EXT_external_memory_dma_buf" };
    VkDeviceCreateInfo dci = { .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, .queueCreateInfoCount = 1, .pQueueCreateInfos = &q, .enabledExtensionCount = 2, .ppEnabledExtensionNames = exts };
    VkDevice dev; if (vkCreateDevice(pd, &dci, NULL, &dev)) { puts("no device"); return 1; }
    PFN_vkGetMemoryFdKHR getFd = (PFN_vkGetMemoryFdKHR)vkGetDeviceProcAddr(dev, "vkGetMemoryFdKHR");

    for (uint32_t t = 0; t < mp.memoryTypeCount; t++) {
        VkMemoryPropertyFlags f = mp.memoryTypes[t].propertyFlags;
        if (!(f & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) continue;
        for (int i = 0; i < 2; i++) {
            VkExportMemoryAllocateInfo ex = { .sType = VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO, .handleTypes = types[i] };
            VkMemoryAllocateInfo ai = { .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .pNext = &ex, .allocationSize = 65536, .memoryTypeIndex = t };
            VkDeviceMemory m; VkResult r = vkAllocateMemory(dev, &ai, NULL, &m);
            if (r) { printf("type %u (flags 0x%x) alloc %s: %d\n", t, f, names[i], r); continue; }
            VkMemoryGetFdInfoKHR gi = { .sType = VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR, .memory = m, .handleType = types[i] };
            int fd = -1; r = getFd(dev, &gi, &fd);
            char link[PATH_MAX] = "", path[64]; ssize_t len = -1; off_t size = -1; int mapok = 0;
            if (!r) {
                snprintf(path, sizeof path, "/proc/self/fd/%d", fd);
                len = readlink(path, link, sizeof link - 1); if (len > 0) link[len] = 0;
                size = lseek(fd, 0, SEEK_END);
                void *ptr = mmap(NULL, 65536, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
                if (ptr != MAP_FAILED) { mapok = 1; munmap(ptr, 65536); }
                close(fd);
            }
            printf("type %u (flags 0x%x) export %-9s: ret %d fd -> '%s' size %lld mmap %s\n", t, f, names[i], r, link, (long long)size, mapok ? "ok" : "FAILED");
            vkFreeMemory(dev, m, NULL);
        }
    }
    return 0;
}
