#ifndef HEADER_ACLRTLAUNCH_AES_CIPHER_KERNEL_NPU_H
#define HEADER_ACLRTLAUNCH_AES_CIPHER_KERNEL_NPU_H
#include "acl/acl_base.h"

#ifndef ACLRT_LAUNCH_KERNEL
#define ACLRT_LAUNCH_KERNEL(kernel_func) aclrtlaunch_##kernel_func
#endif

extern "C" uint32_t aclrtlaunch_aes_cipher_kernel_npu(uint32_t blockDim, aclrtStream stream, void* input_x, void* key, void* iv, void* output, uint32_t totalLength, uint32_t mode);
#endif
