/* pipellm.h */
#pragma once
#include <acl/acl.h>
#include <acl/acl_rt.h>
#include <openssl/evp.h>
#include <stdio.h>
#include <stdlib.h>
#include <cstring>


extern "C" void add_custom_do(
    uint32_t blockDim, 
    void *stream, 
    uint8_t *x, 
    uint8_t *y, 
    uint8_t *iv, 
    uint8_t *z, 
    uint32_t totallength, 
    uint32_t mode
);

// extern void* g_dev_key;
// extern void* g_dev_iv;
// extern bool g_is_initialized;

const size_t ENCRYPT_THRESHOLD = 0;

const size_t DECRYPT_THRESHOLD = 0;

const uint32_t MODE_STANDARD_CTR = 0; // 原有模式：实时计算
const uint32_t MODE_GEN_KEYSTREAM = 1; // 仅生成密钥流并保存
const uint32_t MODE_USE_PRECOMP  = 2; // 读取预计算密钥流进行 XOR