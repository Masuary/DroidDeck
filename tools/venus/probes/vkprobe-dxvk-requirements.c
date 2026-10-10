/* Which of DXVK 3.1.1's required Vulkan features does this device lack?
 * (src/dxvk/dxvk_device_info.cpp getFeatureList / checkDeviceCompatibility) */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <vulkan/vulkan.h>

static int has_ext(const VkExtensionProperties *e, uint32_t n, const char *name) {
    for (uint32_t i = 0; i < n; i++)
        if (!strcmp(e[i].extensionName, name)) return 1;
    return 0;
}

static int missing = 0;
static void req(const char *name, int ok) {
    printf("  %-44s %s\n", name, ok ? "ok" : "MISSING");
    if (!ok) missing++;
}

int main(void) {
    VkApplicationInfo app = { .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, .apiVersion = VK_API_VERSION_1_3 };
    VkInstanceCreateInfo ici = { .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &app };
    VkInstance inst;
    if (vkCreateInstance(&ici, NULL, &inst) != VK_SUCCESS) { puts("no instance"); return 2; }
    uint32_t n = 1; VkPhysicalDevice pd;
    vkEnumeratePhysicalDevices(inst, &n, &pd);
    if (!n) { puts("no device"); return 2; }

    VkPhysicalDeviceProperties props; vkGetPhysicalDeviceProperties(pd, &props);
    uint32_t ne = 0; vkEnumerateDeviceExtensionProperties(pd, NULL, &ne, NULL);
    VkExtensionProperties *ext = calloc(ne, sizeof(*ext));
    vkEnumerateDeviceExtensionProperties(pd, NULL, &ne, ext);

    VkPhysicalDeviceMaintenance6FeaturesKHR m6 = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_6_FEATURES_KHR };
    VkPhysicalDeviceMaintenance5FeaturesKHR m5 = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_5_FEATURES_KHR, .pNext = &m6 };
    VkPhysicalDeviceRobustness2FeaturesEXT rb2 = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_EXT, .pNext = &m5 };
    VkPhysicalDeviceDepthClipEnableFeaturesEXT dce = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_CLIP_ENABLE_FEATURES_EXT, .pNext = &rb2 };
    VkPhysicalDeviceVulkan13Features v13 = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES, .pNext = &dce };
    VkPhysicalDeviceVulkan12Features v12 = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES, .pNext = &v13 };
    VkPhysicalDeviceVulkan11Features v11 = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES, .pNext = &v12 };
    VkPhysicalDeviceFeatures2 f2 = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, .pNext = &v11 };
    vkGetPhysicalDeviceFeatures2(pd, &f2);

    printf("%s, Vulkan %u.%u.%u, %u extensions\n", props.deviceName, VK_API_VERSION_MAJOR(props.apiVersion),
           VK_API_VERSION_MINOR(props.apiVersion), VK_API_VERSION_PATCH(props.apiVersion), ne);
    puts("DXVK requirements after textureCompressionBC (everything before it already passed on this phone):");
    req("textureCompressionBC", f2.features.textureCompressionBC);
    req("vk11.shaderDrawParameters", v11.shaderDrawParameters);
    req("vk11.storageBuffer16BitAccess", v11.storageBuffer16BitAccess);
    req("vk12.bufferDeviceAddress", v12.bufferDeviceAddress);
    req("vk12.descriptorIndexing", v12.descriptorIndexing);
    req("vk12.storageBuffer8BitAccess", v12.storageBuffer8BitAccess);
    req("vk12.descriptorBindingSampledImageUpdateAfterBind", v12.descriptorBindingSampledImageUpdateAfterBind);
    req("vk12.descriptorBindingUpdateUnusedWhilePending", v12.descriptorBindingUpdateUnusedWhilePending);
    req("vk12.descriptorBindingPartiallyBound", v12.descriptorBindingPartiallyBound);
    req("vk12.hostQueryReset", v12.hostQueryReset);
    req("vk12.runtimeDescriptorArray", v12.runtimeDescriptorArray);
    req("vk12.samplerMirrorClampToEdge", v12.samplerMirrorClampToEdge);
    req("vk12.scalarBlockLayout", v12.scalarBlockLayout);
    req("vk12.shaderInt8", v12.shaderInt8);
    req("vk12.timelineSemaphore", v12.timelineSemaphore);
    req("vk12.uniformBufferStandardLayout", v12.uniformBufferStandardLayout);
    req("vk12.vulkanMemoryModel", v12.vulkanMemoryModel);
    req("vk13.inlineUniformBlock", v13.inlineUniformBlock);
    req("vk13.computeFullSubgroups", v13.computeFullSubgroups);
    req("vk13.dynamicRendering", v13.dynamicRendering);
    req("vk13.maintenance4", v13.maintenance4);
    req("vk13.shaderDemoteToHelperInvocation", v13.shaderDemoteToHelperInvocation);
    req("vk13.shaderZeroInitializeWorkgroupMemory", v13.shaderZeroInitializeWorkgroupMemory);
    req("vk13.subgroupSizeControl", v13.subgroupSizeControl);
    req("vk13.synchronization2", v13.synchronization2);
    req("VK_EXT_depth_clip_enable depthClipEnable", has_ext(ext, ne, "VK_EXT_depth_clip_enable") && dce.depthClipEnable);
    int rb2ext = has_ext(ext, ne, "VK_EXT_robustness2") || has_ext(ext, ne, "VK_KHR_robustness2");
    req("robustness2 robustBufferAccess2", rb2ext && rb2.robustBufferAccess2);
    req("robustness2 nullDescriptor", rb2ext && rb2.nullDescriptor);
    req("VK_KHR/EXT_load_store_op_none", has_ext(ext, ne, "VK_KHR_load_store_op_none") || has_ext(ext, ne, "VK_EXT_load_store_op_none"));
    req("VK_KHR_maintenance5 maintenance5", has_ext(ext, ne, "VK_KHR_maintenance5") && m5.maintenance5);
    req("VK_KHR_maintenance6 maintenance6", has_ext(ext, ne, "VK_KHR_maintenance6") && m6.maintenance6);
    req("VK_KHR_swapchain", has_ext(ext, ne, "VK_KHR_swapchain"));
    char buf[64]; snprintf(buf, sizeof buf, "maxPushConstantsSize >= 256 (is %u)", props.limits.maxPushConstantsSize);
    req(buf, props.limits.maxPushConstantsSize >= 256 || has_ext(ext, ne, "VK_EXT_descriptor_heap"));
    printf("%d missing\n", missing);
    puts("For BC emulation: ASTC LDR / ETC2 compression in hardware:");
    printf("  textureCompressionASTC_LDR %s, textureCompressionETC2 %s\n",
           f2.features.textureCompressionASTC_LDR ? "yes" : "no", f2.features.textureCompressionETC2 ? "yes" : "no");
    vkDestroyInstance(inst, NULL);
    return missing ? 1 : 0;
}
