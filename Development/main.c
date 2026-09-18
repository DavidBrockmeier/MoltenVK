/* Native development host: load this build explicitly, never the Vulkan loader.
 * Initialization and a bounded transfer validate integration, not RIFE throughput.
 */
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <dlfcn.h>
#include <limits.h>
#include <mach-o/dyld.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int transfer_test(VkPhysicalDevice physical, PFN_vkGetInstanceProcAddr get, VkInstance instance) {
    PFN_vkGetPhysicalDeviceQueueFamilyProperties families =
        (PFN_vkGetPhysicalDeviceQueueFamilyProperties)get(instance, "vkGetPhysicalDeviceQueueFamilyProperties");
    PFN_vkGetPhysicalDeviceMemoryProperties memory_properties =
        (PFN_vkGetPhysicalDeviceMemoryProperties)get(instance, "vkGetPhysicalDeviceMemoryProperties");
    PFN_vkCreateDevice create = (PFN_vkCreateDevice)get(instance, "vkCreateDevice");
    PFN_vkGetDeviceProcAddr device_get = (PFN_vkGetDeviceProcAddr)get(instance, "vkGetDeviceProcAddr");
    PFN_vkEnumerateDeviceExtensionProperties extensions =
        (PFN_vkEnumerateDeviceExtensionProperties)get(instance, "vkEnumerateDeviceExtensionProperties");
    if (!families || !memory_properties || !create || !device_get || !extensions) return 1;
    uint32_t count = 0;
    families(physical, &count, NULL);
    VkQueueFamilyProperties *items = calloc(count, sizeof(*items));
    if (!items || !count) { free(items); return 1; }
    families(physical, &count, items);
    uint32_t family = UINT32_MAX;
    for (uint32_t i = 0; i < count; i++) {
        if (items[i].queueCount && (items[i].queueFlags & (VK_QUEUE_TRANSFER_BIT | VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT))) {
            family = i; break;
        }
    }
    free(items);
    if (family == UINT32_MAX) return 1;
    uint32_t extension_count = 0;
    if (extensions(physical, NULL, &extension_count, NULL) != VK_SUCCESS) return 1;
    VkExtensionProperties *extension_items = calloc(extension_count, sizeof(*extension_items));
    if (!extension_items && extension_count) return 1;
    if (extensions(physical, NULL, &extension_count, extension_items) != VK_SUCCESS) { free(extension_items); return 1; }
    int portability = 0;
    for (uint32_t i = 0; i < extension_count; i++) {
        portability |= strcmp(extension_items[i].extensionName, "VK_KHR_portability_subset") == 0;
    }
    free(extension_items);
    const char *extension = "VK_KHR_portability_subset";
    float priority = 1.0f;
    VkDeviceQueueCreateInfo queue_info = {.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = family, .queueCount = 1, .pQueuePriorities = &priority};
    VkDeviceCreateInfo device_info = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = 1, .pQueueCreateInfos = &queue_info,
        .enabledExtensionCount = portability ? 1 : 0, .ppEnabledExtensionNames = portability ? &extension : NULL};
    VkDevice device = VK_NULL_HANDLE;
    VkResult result = create(physical, &device_info, NULL, &device);
    if (result != VK_SUCCESS) { fprintf(stderr, "vkCreateDevice failed: %d\n", result); return 1; }
#define LOAD(name) PFN_vk##name name = (PFN_vk##name)device_get(device, "vk" #name)
    LOAD(DestroyDevice); LOAD(GetDeviceQueue); LOAD(CreateBuffer); LOAD(DestroyBuffer);
    LOAD(GetBufferMemoryRequirements); LOAD(AllocateMemory); LOAD(FreeMemory); LOAD(BindBufferMemory);
    LOAD(CreateCommandPool); LOAD(DestroyCommandPool); LOAD(AllocateCommandBuffers);
    LOAD(BeginCommandBuffer); LOAD(EndCommandBuffer); LOAD(CmdFillBuffer); LOAD(CmdPipelineBarrier);
    LOAD(CreateFence); LOAD(DestroyFence); LOAD(QueueSubmit); LOAD(WaitForFences);
    LOAD(MapMemory); LOAD(UnmapMemory); LOAD(InvalidateMappedMemoryRanges);
#undef LOAD
    if (!DestroyDevice || !GetDeviceQueue || !CreateBuffer || !DestroyBuffer || !GetBufferMemoryRequirements ||
        !AllocateMemory || !FreeMemory || !BindBufferMemory || !CreateCommandPool || !DestroyCommandPool ||
        !AllocateCommandBuffers || !BeginCommandBuffer || !EndCommandBuffer || !CmdFillBuffer ||
        !CmdPipelineBarrier || !CreateFence || !DestroyFence || !QueueSubmit || !WaitForFences ||
        !MapMemory || !UnmapMemory || !InvalidateMappedMemoryRanges) {
        fprintf(stderr, "Driver is missing a required device entry point.\n");
        if (DestroyDevice) DestroyDevice(device, NULL);
        return 1;
    }
    int failed = 1;
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkCommandPool pool = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    void *mapped = NULL;
#define CHECK(call) do { result = (call); if (result != VK_SUCCESS) { fprintf(stderr, #call " failed: %d\n", result); goto cleanup; } } while (0)
    VkBufferCreateInfo buffer_info = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = 4096, .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT, .sharingMode = VK_SHARING_MODE_EXCLUSIVE};
    CHECK(CreateBuffer(device, &buffer_info, NULL, &buffer));
    VkMemoryRequirements requirements;
    GetBufferMemoryRequirements(device, buffer, &requirements);
    VkPhysicalDeviceMemoryProperties properties;
    memory_properties(physical, &properties);
    uint32_t memory_type = UINT32_MAX;
    for (uint32_t i = 0; i < properties.memoryTypeCount; i++) {
        if ((requirements.memoryTypeBits & (1u << i)) &&
            (properties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) { memory_type = i; break; }
    }
    if (memory_type == UINT32_MAX) { fprintf(stderr, "No host-visible transfer memory.\n"); goto cleanup; }
    VkMemoryAllocateInfo allocation = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = requirements.size, .memoryTypeIndex = memory_type};
    CHECK(AllocateMemory(device, &allocation, NULL, &memory));
    CHECK(BindBufferMemory(device, buffer, memory, 0));
    VkCommandPoolCreateInfo pool_info = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .queueFamilyIndex = family, .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT};
    CHECK(CreateCommandPool(device, &pool_info, NULL, &pool));
    VkCommandBuffer command;
    VkCommandBufferAllocateInfo command_info = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1};
    CHECK(AllocateCommandBuffers(device, &command_info, &command));
    VkCommandBufferBeginInfo begin = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
    CHECK(BeginCommandBuffer(command, &begin));
    const uint32_t pattern = 0x13579bdf;
    CmdFillBuffer(command, buffer, 0, 4096, pattern);
    VkMemoryBarrier barrier = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT, .dstAccessMask = VK_ACCESS_HOST_READ_BIT};
    CmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &barrier, 0, NULL, 0, NULL);
    CHECK(EndCommandBuffer(command));
    VkFenceCreateInfo fence_info = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    CHECK(CreateFence(device, &fence_info, NULL, &fence));
    VkQueue queue;
    GetDeviceQueue(device, family, 0, &queue);
    VkSubmitInfo submit = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &command};
    CHECK(QueueSubmit(queue, 1, &submit, fence));
    result = WaitForFences(device, 1, &fence, VK_TRUE, UINT64_C(5000000000));
    if (result == VK_TIMEOUT) {
        /* Do not destroy pending resources or block indefinitely in driver cleanup. */
        fprintf(stderr, "Transfer fence timed out after five seconds.\n");
        fflush(NULL);
        _Exit(1);
    }
    if (result != VK_SUCCESS) { fprintf(stderr, "Transfer fence failed: %d\n", result); goto cleanup; }
    CHECK(MapMemory(device, memory, 0, VK_WHOLE_SIZE, 0, &mapped));
    VkMappedMemoryRange range = {.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
        .memory = memory, .offset = 0, .size = VK_WHOLE_SIZE};
    CHECK(InvalidateMappedMemoryRanges(device, 1, &range));
    for (uint32_t i = 0; i < 4096 / sizeof(uint32_t); i++) {
        if (((uint32_t *)mapped)[i] != pattern) { fprintf(stderr, "Transfer mismatch at word %u.\n", i); goto cleanup; }
    }
    puts("Transfer: PASS (4096 bytes filled on the GPU and verified on the CPU)");
    failed = 0;
cleanup:
    if (mapped) UnmapMemory(device, memory);
    if (fence) DestroyFence(device, fence, NULL);
    if (pool) DestroyCommandPool(device, pool, NULL);
    if (buffer) DestroyBuffer(device, buffer, NULL);
    if (memory) FreeMemory(device, memory, NULL);
    DestroyDevice(device, NULL);
    return failed;
#undef CHECK
}

int main(int argc, char **argv) {
    if (argc > 2 || (argc == 2 && strcmp(argv[1], "--help") == 0)) {
        printf("Usage: %s [path/to/libMoltenVK.dylib]\nOtherwise uses MOLTENVK_PROBE_LIBRARY, then the library beside this executable.\n", argv[0]);
        return argc > 2 ? 2 : 0;
    }
    char adjacent[PATH_MAX];
    const char *path = argc == 2 ? argv[1] : getenv("MOLTENVK_PROBE_LIBRARY");
    if (!path || !path[0]) {
        uint32_t size = sizeof(adjacent);
        if (_NSGetExecutablePath(adjacent, &size) != 0) { fprintf(stderr, "Executable path too long.\n"); return 1; }
        char *slash = strrchr(adjacent, '/');
        if (!slash || (size_t)(slash - adjacent) + sizeof("/libMoltenVK.dylib") > sizeof(adjacent)) return 1;
        strcpy(slash, "/libMoltenVK.dylib");
        path = adjacent;
    }
    char resolved[PATH_MAX];
    if (!realpath(path, resolved)) { perror(path); return 1; }
    printf("Explicit driver: %s\n", resolved);
    void *library = dlopen(resolved, RTLD_NOW | RTLD_LOCAL);
    if (!library) { fprintf(stderr, "dlopen: %s\n", dlerror()); return 1; }
    int failed = 1;
    VkInstance instance = VK_NULL_HANDLE;
    PFN_vkDestroyInstance destroy = NULL;
    VkPhysicalDevice *devices = NULL;
    PFN_vkGetInstanceProcAddr get = (PFN_vkGetInstanceProcAddr)dlsym(library, "vkGetInstanceProcAddr");
    if (!get) { fprintf(stderr, "Missing vkGetInstanceProcAddr.\n"); goto cleanup; }
    PFN_vkCreateInstance create = (PFN_vkCreateInstance)get(VK_NULL_HANDLE, "vkCreateInstance");
    PFN_vkEnumerateInstanceExtensionProperties extensions =
        (PFN_vkEnumerateInstanceExtensionProperties)get(VK_NULL_HANDLE, "vkEnumerateInstanceExtensionProperties");
    if (!create || !extensions) goto cleanup;
    uint32_t extension_count = 0;
    if (extensions(NULL, &extension_count, NULL) != VK_SUCCESS) goto cleanup;
    VkExtensionProperties *extension_items = calloc(extension_count, sizeof(*extension_items));
    if (!extension_items && extension_count) goto cleanup;
    if (extensions(NULL, &extension_count, extension_items) != VK_SUCCESS) { free(extension_items); goto cleanup; }
    int portability = 0;
    for (uint32_t i = 0; i < extension_count; i++) {
        portability |= strcmp(extension_items[i].extensionName, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME) == 0;
    }
    free(extension_items);
    const char *extension = VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME;
    VkApplicationInfo app = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "MoltenVK Probe", .apiVersion = VK_API_VERSION_1_2};
    VkInstanceCreateInfo info = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &app,
        .flags = portability ? VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR : 0,
        .enabledExtensionCount = portability ? 1 : 0, .ppEnabledExtensionNames = portability ? &extension : NULL};
    VkResult result = create(&info, NULL, &instance);
    if (result != VK_SUCCESS) { fprintf(stderr, "vkCreateInstance failed: %d\n", result); goto cleanup; }
    destroy = (PFN_vkDestroyInstance)get(instance, "vkDestroyInstance");
    PFN_vkEnumeratePhysicalDevices enumerate = (PFN_vkEnumeratePhysicalDevices)get(instance, "vkEnumeratePhysicalDevices");
    PFN_vkGetPhysicalDeviceProperties2 properties = (PFN_vkGetPhysicalDeviceProperties2)get(instance, "vkGetPhysicalDeviceProperties2");
    if (!destroy || !enumerate || !properties) goto cleanup;
    uint32_t count = 0;
    if (enumerate(instance, &count, NULL) != VK_SUCCESS || !count) { fprintf(stderr, "No physical devices.\n"); goto cleanup; }
    devices = calloc(count, sizeof(*devices));
    if (!devices || enumerate(instance, &count, devices) != VK_SUCCESS) goto cleanup;
    printf("Physical devices: %u\n", count);
    for (uint32_t i = 0; i < count; i++) {
        VkPhysicalDeviceDriverProperties driver = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES};
        VkPhysicalDeviceProperties2 props = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, .pNext = &driver};
        properties(devices[i], &props);
        printf("Device: %s\nDriver: %s\nDriver info: %s\n", props.properties.deviceName, driver.driverName, driver.driverInfo);
        if (transfer_test(devices[i], get, instance)) goto cleanup;
    }
    puts("Probe passed. This is a driver integration check, not an interpolation benchmark.");
    failed = 0;
cleanup:
    free(devices);
    if (instance && destroy) destroy(instance, NULL);
    dlclose(library);
    return failed;
}
