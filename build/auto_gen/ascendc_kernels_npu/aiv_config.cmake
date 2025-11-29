set(MIX_SOURCES
)
set(AIV_SOURCES
    /data/lzy/pipellm_acl/build/auto_gen/ascendc_kernels_npu/auto_gen_CTR.cpp
)
set_source_files_properties(/data/lzy/pipellm_acl/build/auto_gen/ascendc_kernels_npu/auto_gen_CTR.cpp
    PROPERTIES COMPILE_DEFINITIONS ";auto_gen_aes_cipher_kernel_npu_kernel=aes_cipher_kernel_npu_0;ONE_CORE_DUMP_SIZE=1048576"
)
