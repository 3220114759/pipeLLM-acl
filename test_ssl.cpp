#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <openssl/evp.h>
#include <openssl/err.h>

// 必须与您 C++ 代码中的 decrypt_magic_sz 匹配
#define DECRYPT_MAGIC_SZ 0xabcde // 703710

int main() {
    printf("[Test C] 🚀 启动最简 SSL 钩子测试...\n");

    // --- 1. 准备 OpenSSL 上下文和数据 ---
    EVP_CIPHER_CTX *ctx;
    const EVP_CIPHER *cipher;

    // 准备虚拟数据 (与 Python 脚本中的一致)
    unsigned char dummy_key[32];
    unsigned char dummy_iv[12];
    unsigned char dummy_tag[16];
    memset(dummy_key, 0, sizeof(dummy_key));
    memset(dummy_iv, 0, sizeof(dummy_iv));
    memset(dummy_tag, 0, sizeof(dummy_tag));

    // 为 magic_sz 调用准备缓冲区
    unsigned char *in_buf = (unsigned char *)malloc(DECRYPT_MAGIC_SZ);
    unsigned char *out_buf = (unsigned char *)malloc(DECRYPT_MAGIC_SZ);
    if (!in_buf || !out_buf) {
        fprintf(stderr, "[Test C] ❌ 内存分配失败\n");
        return 1;
    }
    memset(in_buf, 0, DECRYPT_MAGIC_SZ);
    int outl; // 输出长度

    // --- 2. 获取 Cipher 和创建 CTX ---
    printf("[Test C] 正在调用 EVP_aes_256_gcm...\n");
    cipher = EVP_aes_256_gcm();

    printf("[Test C] 正在调用 EVP_CIPHER_CTX_new...\n");
    ctx = EVP_CIPHER_CTX_new();
    if (!ctx) {
        fprintf(stderr, "[Test C] ❌ EVP_CIPHER_CTX_new 失败\n");
        return 1;
    }
    printf("[Test C]   -> CTX 指针: %p\n", (void*)ctx);


    // --- 3. 严格按照 Python 脚本的顺序模拟握手 ---

    // 步骤 1: [关键] EVP_DecryptUpdate (magic_sz)
    // 这应该触发 openssl.cpp 中 "探测到解密握手" 的逻辑
    printf("[Test C] 1. [关键] 正在调用 EVP_DecryptUpdate (inl=%d)...\n", DECRYPT_MAGIC_SZ);
    int ret = EVP_DecryptUpdate(ctx, out_buf, &outl, in_buf, DECRYPT_MAGIC_SZ);
    printf("[Test C]    -> EVP_DecryptUpdate 返回: %d (预期为 1)\n", ret);


    // 步骤 2: EVP_DecryptInit_ex (首次)
    // 这应该触发 metadata->remain++ (变为 1)
    printf("[Test C] 2. 正在调用 EVP_DecryptInit_ex (首次)...\n");
    ret = EVP_DecryptInit_ex(ctx, cipher, NULL, NULL, NULL);
    printf("[Test C]    -> EVP_DecryptInit_ex (1) 返回: %d\n", ret);


    // 步骤 3: EVP_DecryptInit_ex (第二次, 传入 Key/IV)
    // 这应该触发 metadata->remain++ (变为 2)
    printf("[Test C] 3. 正在调用 EVP_DecryptInit_ex (第二次, 带 Key/IV)...\n");
    ret = EVP_DecryptInit_ex(ctx, NULL, NULL, dummy_key, dummy_iv);
    printf("[Test C]    -> EVP_DecryptInit_ex (2) 返回: %d\n", ret);
    

    // 步骤 4: EVP_CIPHER_CTX_ctrl (SET_TAG)
    // 这应该触发 "memcpy(metadata.cur_tag, ptr, 16)"
    printf("[Test C] 4. 正在调用 EVP_CIPHER_CTX_ctrl (SET_TAG)...\n");
    ret = EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, 16, dummy_tag);
    printf("[Test C]    -> EVP_CIPHER_CTX_ctrl 返回: %d\n", ret);


    // --- 4. 清理 ---
    printf("[Test C] 5. 正在调用 EVP_CIPHER_CTX_free...\n");
    EVP_CIPHER_CTX_free(ctx);

    free(in_buf);
    free(out_buf);

    printf("[Test C] ✅ 测试脚本执行完毕。\n");
    return 0;
}