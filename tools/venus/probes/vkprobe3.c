// What a gamescope flippable image needs, asked per format and usage: is a dma_buf-exportable image
// with each listed DRM modifier supported? Builds for Android (NDK) and for the rootfs (glibc), so
// the same question can be put to the vendor driver directly and through Venus.
#include <vulkan/vulkan.h>
#include <stdio.h>

static void ask(VkPhysicalDevice pd, VkFormat fmt, VkFormat srgb, const char *fname, VkImageUsageFlags usage, const char *uname, int mutable_list) {
    VkDrmFormatModifierPropertiesListEXT ml = { .sType = VK_STRUCTURE_TYPE_DRM_FORMAT_MODIFIER_PROPERTIES_LIST_EXT };
    VkFormatProperties2 fp = { .sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2, .pNext = &ml };
    vkGetPhysicalDeviceFormatProperties2(pd, fmt, &fp);
    VkDrmFormatModifierPropertiesEXT mods[16];
    if (ml.drmFormatModifierCount > 16) ml.drmFormatModifierCount = 16;
    ml.pDrmFormatModifierProperties = mods;
    vkGetPhysicalDeviceFormatProperties2(pd, fmt, &fp);
    if (!ml.drmFormatModifierCount) printf("%-6s %-22s: no modifiers listed\n", fname, uname);
    for (uint32_t i = 0; i < ml.drmFormatModifierCount; i++) {
        VkPhysicalDeviceImageDrmFormatModifierInfoEXT mi = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_DRM_FORMAT_MODIFIER_INFO_EXT, .drmFormatModifier = mods[i].drmFormatModifier };
        VkPhysicalDeviceExternalImageFormatInfo ei = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO, .pNext = &mi, .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT };
        // gamescope's flippable images are mutable with an [UNORM, SRGB] view list
        VkFormat views[2] = { fmt, srgb };
        VkImageFormatListCreateInfo fl = { .sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO, .pNext = &ei, .viewFormatCount = 2, .pViewFormats = views };
        VkPhysicalDeviceImageFormatInfo2 ii = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2, .pNext = mutable_list == 1 ? (void *)&fl : (void *)&ei, .format = fmt, .type = VK_IMAGE_TYPE_2D,
            .tiling = VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT, .usage = usage, .flags = mutable_list ? VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT : 0 };
        VkExternalImageFormatProperties ep = { .sType = VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES };
        VkImageFormatProperties2 ip = { .sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2, .pNext = &ep };
        VkResult r = vkGetPhysicalDeviceImageFormatProperties2(pd, &ii, &ip);
        printf("%-6s %-22s %s mod 0x%016llx tilingfeat 0x%05x: ret %3d extfeat 0x%x\n", fname, uname, mutable_list == 1 ? "mut+srgb" : mutable_list == 2 ? "mut-only" : "plain   ", (unsigned long long)mods[i].drmFormatModifier,
               mods[i].drmFormatModifierTilingFeatures, r, ep.externalMemoryProperties.externalMemoryFeatures);
    }
}

int main(void) {
    VkApplicationInfo app = { .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, .apiVersion = VK_API_VERSION_1_2 };
    VkInstanceCreateInfo ici = { .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &app };
    VkInstance inst; if (vkCreateInstance(&ici, NULL, &inst)) { puts("no instance"); return 1; }
    uint32_t n = 1; VkPhysicalDevice pd; vkEnumeratePhysicalDevices(inst, &n, &pd);
    VkPhysicalDeviceProperties p; vkGetPhysicalDeviceProperties(pd, &p); printf("device: %s\n", p.deviceName);
    struct { VkFormat f, s; const char *n; } fmts[] = { { VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_B8G8R8A8_SRGB, "BGRA8" }, { VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_R8G8B8A8_SRGB, "RGBA8" } };
    struct { VkImageUsageFlags u; const char *n; } uses[] = {
        { VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, "sampled+xferdst" },
        { VK_IMAGE_USAGE_SAMPLED_BIT, "sampled" },
        { VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, "color+sampled" },
        { VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, "storage(output)" },
    };
    for (int m = 0; m < 3; m++) for (int f = 0; f < 2; f++) for (int u = 0; u < 4; u++) ask(pd, fmts[f].f, fmts[f].s, fmts[f].n, uses[u].u, uses[u].n, m);
    return 0;
}
