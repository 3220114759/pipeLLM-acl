
#ifndef HEADER_ACLRTLAUNCH_AES_CIPHER_KERNEL_NPU_HKERNEL_H_
#define HEADER_ACLRTLAUNCH_AES_CIPHER_KERNEL_NPU_HKERNEL_H_



extern "C" uint32_t aclrtlaunch_aes_cipher_kernel_npu(uint32_t blockDim, void* stream, void* input_x, void* key, void* iv, void* output, uint32_t totalLength, uint32_t mode);

inline uint32_t aes_cipher_kernel_npu(uint32_t blockDim, void* hold, void* stream, void* input_x, void* key, void* iv, void* output, uint32_t totalLength, uint32_t mode)
{
    (void)hold;
    return aclrtlaunch_aes_cipher_kernel_npu(blockDim, stream, input_x, key, iv, output, totalLength, mode);
}

#endif
