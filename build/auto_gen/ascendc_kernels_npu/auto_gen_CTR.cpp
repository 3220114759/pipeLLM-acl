#ifndef __CTR__KERNEL_FUN_H__
#define __CTR__KERNEL_FUN_H__

#undef __global__
#define __global__ inline
#define aes_cipher_kernel_npu aes_cipher_kernel_npu_origin
#include "/data/lzy/pipellm_acl/CTR.cpp"

#undef aes_cipher_kernel_npu
#undef __global__
#if ASCENDC_CPU_DEBUG
#define __global__
#else
#define __global__ __attribute__((cce_kernel))
#endif

#ifndef ONE_CORE_DUMP_SIZE
#define ONE_CORE_DUMP_SIZE 1048576 * 1
#endif

extern "C" __global__ [aicore] void auto_gen_aes_cipher_kernel_npu_kernel(
__attribute__((cce_global)) uint8_t* input_x, __attribute__((cce_global)) uint8_t* key, __attribute__((cce_global)) uint8_t* iv, __attribute__((cce_global)) uint8_t* output, uint32_t totalLength, uint32_t mode, GM_ADDR overflow_status) {
#if defined(HAVE_WORKSPACE)
    GM_ADDR workspace_param;
    GM_ADDR workspace_usr;
#if defined(HAVE_TILING)
    workspace_param = totalLength;
#else
    workspace_param = mode;
#endif
    AscendC::SetSysWorkspaceForce(workspace_param);
    workspace_usr = AscendC::GetUserWorkspace(workspace_param);
#if defined(HAVE_TILING)
    totalLength = workspace_usr;
#else
    mode = workspace_usr;
#endif
#endif
    aes_cipher_kernel_npu_origin(input_x, key, iv, output, totalLength, mode);
#if defined(ASCENDC_DUMP) && defined(ASCENDC_DEBUG)
    AscendC::WriteBackOverflow(overflow_status);
#endif
}

#endif
#include "inner_interface/inner_kernel_operator_intf.h"
