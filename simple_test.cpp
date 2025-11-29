#include <iostream>
#include <vector>
#include <cstring>
#include <iomanip>
#include <dlfcn.h> // 动态加载必须
#include "acl/acl.h"

// ACL 错误检查宏
#define CHECK_ACL(x) do { \
    aclError __ret = x; \
    if (__ret != ACL_SUCCESS) { \
        std::cerr << "[ERROR] " << #x << " Failed. Error Code: " << __ret << std::endl; \
        return -1; \
    } \
} while (0)

// 定义算子封装函数的指针类型 (必须是6个参数)
// void LaunchAESKernel(stream, input, key, iv, output, length)
typedef void (*LaunchAESKernel_t)(void*, void*, void*, void*, void*, uint32_t);

int main() {
    std::cout << "[INFO] Starting DLOPEN Test..." << std::endl;

    // =================================================================
    // 1. 动态加载库 (关键修复步骤)
    // =================================================================
    
    // [步骤 A] 先加载底层的算子实现库 (解决 undefined reference)
    // 请确保此文件在当前目录或指定路径下
    const char* kernel_lib_path = "/data/lzy/pipellm_acl/libascendc_kernels_npu.so";
    std::cout << "[INFO] Loading Kernel Lib: " << kernel_lib_path << std::endl;
    
    // 使用 RTLD_GLOBAL 让后续加载的库能看到这里的符号
    void* kernel_handle = dlopen(kernel_lib_path, RTLD_LAZY | RTLD_GLOBAL);
    if (!kernel_handle) {
        std::cerr << "[FATAL] Failed to load kernel lib: " << dlerror() << std::endl;
        std::cerr << "提示: 请检查 build 目录下是否有该文件并拷贝过来。" << std::endl;
        return -1;
    }

    // [步骤 B] 再加载 Host 端的封装库
    const char* wrapper_lib_path = "/data/lzy/pipellm_acl/libaes_npu_lib.so";
    std::cout << "[INFO] Loading Wrapper Lib: " << wrapper_lib_path << std::endl;
    
    void* wrapper_handle = dlopen(wrapper_lib_path, RTLD_LAZY | RTLD_GLOBAL);
    if (!wrapper_handle) {
        std::cerr << "[FATAL] Failed to load wrapper lib: " << dlerror() << std::endl;
        dlclose(kernel_handle);
        return -1;
    }

    // [步骤 C] 获取函数符号
    LaunchAESKernel_t launch_kernel = (LaunchAESKernel_t)dlsym(wrapper_handle, "LaunchAESKernel");
    if (!launch_kernel) {
        std::cerr << "[FATAL] dlsym failed: " << dlerror() << std::endl;
        dlclose(wrapper_handle);
        dlclose(kernel_handle);
        return -1;
    }
    std::cout << "[INFO] ✅ Symbol 'LaunchAESKernel' found at address: " << (void*)launch_kernel << std::endl;

    // =================================================================
    // 2. ACL 环境初始化
    // =================================================================
    const int32_t deviceId = 0;
    CHECK_ACL(aclInit(nullptr));
    CHECK_ACL(aclrtSetDevice(deviceId));
    
    aclrtContext context;
    CHECK_ACL(aclrtCreateContext(&context, deviceId));
    CHECK_ACL(aclrtSetCurrentContext(context));
    
    aclrtStream stream;
    CHECK_ACL(aclrtCreateStream(&stream));

    std::cout << "[INFO] ACL Initialized. Stream created." << std::endl;

    // =================================================================
    // 3. 内存准备 (模拟 PipeLLM 的行为)
    // =================================================================
    uint32_t length = 4096; // 测试长度
    
    // Host 端数据
    std::vector<uint8_t> host_in(length, 0xCC); // 输入填充 0xCC
    std::vector<uint8_t> host_key(32, 0x11);    // 假 Key
    std::vector<uint8_t> host_iv(16, 0x22);     // 假 IV
    std::vector<uint8_t> host_out(length, 0x00); // 输出初始化为 0

    // Device 端内存 (模拟 temp_npu_in/out)
    void *dev_in, *dev_key, *dev_iv, *dev_out;
    CHECK_ACL(aclrtMalloc(&dev_in, length, ACL_MEM_MALLOC_HUGE_FIRST));
    CHECK_ACL(aclrtMalloc(&dev_key, 32, ACL_MEM_MALLOC_HUGE_FIRST));
    CHECK_ACL(aclrtMalloc(&dev_iv, 16, ACL_MEM_MALLOC_HUGE_FIRST));
    CHECK_ACL(aclrtMalloc(&dev_out, length, ACL_MEM_MALLOC_HUGE_FIRST));

    // Host -> Device 拷贝
    CHECK_ACL(aclrtMemcpy(dev_in, length, host_in.data(), length, ACL_MEMCPY_HOST_TO_DEVICE));
    CHECK_ACL(aclrtMemcpy(dev_key, 32, host_key.data(), 32, ACL_MEMCPY_HOST_TO_DEVICE));
    CHECK_ACL(aclrtMemcpy(dev_iv, 16, host_iv.data(), 16, ACL_MEMCPY_HOST_TO_DEVICE));
    // 清空输出区，防止误判
    CHECK_ACL(aclrtMemset(dev_out, length, 0, length));

    // =================================================================
    // 4. 执行算子
    // =================================================================
    std::cout << "[INFO] Launching Kernel via Function Pointer..." << std::endl;
    
    // 调用我们动态获取的函数指针
    launch_kernel(stream, dev_in, dev_key, dev_iv, dev_out, length);

    // 同步等待
    std::cout << "[INFO] Synchronizing Stream..." << std::endl;
    aclError sync_ret = aclrtSynchronizeStream(stream);
    if (sync_ret != ACL_SUCCESS) {
        std::cerr << "[FATAL] Stream Sync Failed! Error Code: " << sync_ret << std::endl;
        return -1;
    }
    std::cout << "[INFO] ✅ Kernel Execution Success!" << std::endl;

    // =================================================================
    // 5. 结果回读与验证
    // =================================================================
    CHECK_ACL(aclrtMemcpy(host_out.data(), length, dev_out, length, ACL_MEMCPY_DEVICE_TO_HOST));

    std::cout << "[RESULT] First 16 bytes of output: ";
    bool all_zero = true;
    bool same_as_input = true;
    for (int i = 0; i < 16; ++i) {
        std::cout << std::hex << std::setw(2) << std::setfill('0') << (int)host_out[i] << " ";
        if (host_out[i] != 0) all_zero = false;
        if (host_out[i] != 0xCC) same_as_input = false;
    }
    std::cout << std::dec << std::endl;

    if (all_zero) {
        std::cerr << "[WARN] Output is all ZEROs! Kernel might not have written anything." << std::endl;
    } else if (same_as_input) {
        std::cerr << "[WARN] Output equals Input! Did encryption/decryption happen?" << std::endl;
    } else {
        std::cout << "[SUCCESS] Data has changed. Pipeline is working." << std::endl;
    }

    // =================================================================
    // 6. 资源清理
    // =================================================================
    CHECK_ACL(aclrtFree(dev_in));
    CHECK_ACL(aclrtFree(dev_key));
    CHECK_ACL(aclrtFree(dev_iv));
    CHECK_ACL(aclrtFree(dev_out));
    CHECK_ACL(aclrtDestroyStream(stream));
    CHECK_ACL(aclrtDestroyContext(context));
    CHECK_ACL(aclrtResetDevice(deviceId));
    CHECK_ACL(aclFinalize());
    
    dlclose(wrapper_handle);
    dlclose(kernel_handle);

    std::cout << "[INFO] Test Finished." << std::endl;
    return 0;
}