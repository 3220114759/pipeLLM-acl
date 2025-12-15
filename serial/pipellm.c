/* --- START OF FILE pipellm.c (Final: AllReduce + AllGather) --- */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dlfcn.h>
#include <stdint.h>
#include <fcntl.h>
#include <pthread.h>
#include <time.h>

/* =================================================================
   基础定义
   ================================================================= */
typedef int aclError;
typedef int aclrtMemcpyKind;
typedef void* aclrtStream;
typedef void* aclrtEvent;

// HCCL 定义
typedef int (*hccl_allreduce_t)(void *, void *, uint64_t, int, int, void *, void *);
typedef int (*hccl_allgather_t)(void *, void *, uint64_t, int, void *, void *); // [New]

typedef struct evp_cipher_ctx_st EVP_CIPHER_CTX;
typedef struct evp_cipher_st EVP_CIPHER;
typedef struct engine_st ENGINE;

/* =================================================================
   动态符号加载
   ================================================================= */
static void* g_real_memcpy_async = NULL;
static void* g_real_memcpy = NULL;
static void* g_real_malloc = NULL;
static void* g_real_memset_async = NULL;
static void* g_real_create_stream = NULL;
static void* g_real_destroy_stream = NULL;
static void* g_real_sync_stream = NULL;
static void* g_real_free = NULL;
static void* g_real_create_event = NULL;
static void* g_real_record_event = NULL;
static void* g_real_sync_event = NULL;

static hccl_allreduce_t g_real_hccl_all_reduce = NULL;
static hccl_allgather_t g_real_hccl_all_gather = NULL; // [New]

static EVP_CIPHER_CTX* (*ptr_EVP_CIPHER_CTX_new)(void) = NULL;
static int (*ptr_EVP_EncryptInit_ex)(EVP_CIPHER_CTX *, const EVP_CIPHER *, ENGINE *, const unsigned char *, const unsigned char *) = NULL;
static int (*ptr_EVP_EncryptUpdate)(EVP_CIPHER_CTX *, unsigned char *, int *, const unsigned char *, int) = NULL;
static int (*ptr_EVP_EncryptFinal_ex)(EVP_CIPHER_CTX *, unsigned char *, int *) = NULL;
static int (*ptr_EVP_DecryptInit_ex)(EVP_CIPHER_CTX *, const EVP_CIPHER *, ENGINE *, const unsigned char *, const unsigned char *) = NULL;
static int (*ptr_EVP_DecryptUpdate)(EVP_CIPHER_CTX *, unsigned char *, int *, const unsigned char *, int) = NULL;
static int (*ptr_EVP_DecryptFinal_ex)(EVP_CIPHER_CTX *, unsigned char *, int *) = NULL;
static const EVP_CIPHER* (*ptr_EVP_aes_128_ctr)(void) = NULL;

static void (*ptr_add_custom_do)(uint32_t, void*, uint8_t*, uint8_t*, uint8_t*, uint8_t*, uint32_t, uint32_t) = NULL;

static void* load_sym(const char* name, int type) {
    void* sym = NULL;
    if (type == 0) {
        sym = dlsym(RTLD_NEXT, name);
        if (!sym) sym = dlsym(RTLD_DEFAULT, name);
    } 
    else if (type == 1) {
        sym = dlsym(RTLD_NEXT, name);
        if (!sym) sym = dlsym(RTLD_DEFAULT, name);
        if (!sym) {
            static void* ssl_h = NULL;
            if (!ssl_h) ssl_h = dlopen("libcrypto.so.1.1", RTLD_NOW|RTLD_LOCAL);
            if (!ssl_h) ssl_h = dlopen("libcrypto.so", RTLD_NOW|RTLD_LOCAL);
            if (ssl_h) sym = dlsym(ssl_h, name);
        }
    }
    else if (type == 2) {
        static void* kern_h = NULL;
        if (!kern_h) {
            const char* dep_lib = "/data/lzy/ops/aes-ctr/build/lib/libascendc_kernels_npu.so";
            const char* kern_lib = "/data/lzy/ops/aes-ctr/build/libaes_npu_lib.so";
            dlopen(dep_lib, RTLD_NOW | RTLD_GLOBAL);
            kern_h = dlopen(kern_lib, RTLD_NOW | RTLD_LOCAL);
            if (!kern_h) fprintf(stderr, "[PipeLLM] Error loading kernel lib: %s\n", dlerror());
        }
        if (kern_h) sym = dlsym(kern_h, name);
    }
    return sym;
}

static void init_symbols() {
    if (g_real_memcpy_async) return;
    g_real_memcpy_async = load_sym("aclrtMemcpyAsync", 0);
    g_real_memcpy = load_sym("aclrtMemcpy", 0);
    g_real_malloc = load_sym("aclrtMalloc", 0);
    g_real_memset_async = load_sym("aclrtMemsetAsync", 0);
    g_real_create_stream = load_sym("aclrtCreateStream", 0);
    g_real_destroy_stream = load_sym("aclrtDestroyStream", 0);
    g_real_sync_stream = load_sym("aclrtSynchronizeStream", 0);
    g_real_free = load_sym("aclrtFree", 0);
    g_real_create_event = load_sym("aclrtCreateEvent", 0);
    g_real_record_event = load_sym("aclrtRecordEvent", 0);
    g_real_sync_event = load_sym("aclrtSynchronizeEvent", 0);

    ptr_EVP_CIPHER_CTX_new = load_sym("EVP_CIPHER_CTX_new", 1);
    ptr_EVP_EncryptInit_ex = load_sym("EVP_EncryptInit_ex", 1);
    ptr_EVP_EncryptUpdate = load_sym("EVP_EncryptUpdate", 1);
    ptr_EVP_EncryptFinal_ex = load_sym("EVP_EncryptFinal_ex", 1);
    ptr_EVP_DecryptInit_ex = load_sym("EVP_DecryptInit_ex", 1);
    ptr_EVP_DecryptUpdate = load_sym("EVP_DecryptUpdate", 1);
    ptr_EVP_DecryptFinal_ex = load_sym("EVP_DecryptFinal_ex", 1);
    ptr_EVP_aes_128_ctr = load_sym("EVP_aes_128_ctr", 1);
    ptr_add_custom_do = load_sym("add_custom_do", 2);

    // Load HCCL Symbols
    if (!g_real_hccl_all_reduce) {
        // Try RTLD_NEXT first
        g_real_hccl_all_reduce = (hccl_allreduce_t)dlsym(RTLD_NEXT, "HcclAllReduce");
        g_real_hccl_all_gather = (hccl_allgather_t)dlsym(RTLD_NEXT, "HcclAllGather");
        
        if (!g_real_hccl_all_reduce) {
            const char* hccl_paths[] = {"/usr/local/Ascend/ascend-toolkit/latest/lib64/libhccl.so", "libhccl.so"};
            for (int i = 0; i < 2; i++) {
                void* handle = dlopen(hccl_paths[i], RTLD_NOW | RTLD_GLOBAL);
                if (handle) {
                    g_real_hccl_all_reduce = (hccl_allreduce_t)dlsym(handle, "HcclAllReduce");
                    g_real_hccl_all_gather = (hccl_allgather_t)dlsym(handle, "HcclAllGather");
                    if (g_real_hccl_all_reduce) break;
                }
            }
        }
    }
}

/* =================================================================
   上下文与内存管理
   ================================================================= */
#define NPU_ALIGN_SIZE 512
#define ENCRYPT_THRESHOLD_LIMIT (1024 * 1024) 
#define SAFETY_PADDING 4096
#define CHECKSUM_SIZE 1024 
#define SLOT_NUM 2  

static const uint8_t g_fixed_key[32] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x00};

static int g_kernel_mode = 2; 

typedef struct {
    pid_t pid;
    EVP_CIPHER_CTX* enc_ctx;
    EVP_CIPHER_CTX* dec_ctx;
    uint8_t* host_bufs[SLOT_NUM]; 
    void* dev_bufs_in[SLOT_NUM];
    void* dev_bufs_out[SLOT_NUM];
    uint8_t* host_ivs[SLOT_NUM];
    void* dev_ivs[SLOT_NUM];
    aclrtEvent events[SLOT_NUM]; 
    int current_slot;
    size_t host_len;
    void* dev_key; 
    void* dev_keystream; 
    uint8_t* host_checksum; 
    int keystream_ready;
    int ready;
} proc_ctx_t;

static pthread_key_t g_key;
static pthread_once_t g_once = PTHREAD_ONCE_INIT;
static void make_key() { pthread_key_create(&g_key, NULL); }

static proc_ctx_t* get_ctx() {
    pthread_once(&g_once, make_key);
    proc_ctx_t* ctx = (proc_ctx_t*)pthread_getspecific(g_key);

    if (!ctx || ctx->pid != getpid()) {
        init_symbols();
        if (!ptr_EVP_CIPHER_CTX_new) return NULL; 
        
        char* mode_env = getenv("PIPE_KERNEL_MODE");
        if (mode_env) g_kernel_mode = atoi(mode_env);
        srand(time(NULL));

        ctx = (proc_ctx_t*)calloc(1, sizeof(proc_ctx_t));
        ctx->pid = getpid();
        ctx->enc_ctx = ptr_EVP_CIPHER_CTX_new();
        ctx->dec_ctx = ptr_EVP_CIPHER_CTX_new();
        ctx->host_len = ENCRYPT_THRESHOLD_LIMIT + NPU_ALIGN_SIZE + SAFETY_PADDING;
        ctx->current_slot = 0;

        typedef int (*f_malloc)(void**, size_t);
        typedef int (*f_memcpy)(void*, size_t, const void*, size_t, int);
        typedef int (*f_create_event)(aclrtEvent*);
        typedef int (*f_record_event)(aclrtEvent, aclrtStream);
        
        f_malloc real_malloc = (f_malloc)g_real_malloc;
        f_memcpy real_memcpy = (f_memcpy)g_real_memcpy;
        f_create_event real_create_event = (f_create_event)g_real_create_event;
        f_record_event real_record_event = (f_record_event)g_real_record_event;

        if (real_malloc && real_memcpy) {
            real_malloc(&ctx->dev_key, 32);
            real_memcpy(ctx->dev_key, 32, g_fixed_key, 32, 1);
            
            size_t dev_alloc_size = ctx->host_len + CHECKSUM_SIZE + 512;
            for (int i = 0; i < SLOT_NUM; ++i) {
                posix_memalign((void**)&ctx->host_bufs[i], sysconf(_SC_PAGESIZE), ctx->host_len);
                real_malloc(&ctx->dev_bufs_in[i], dev_alloc_size);
                real_malloc(&ctx->dev_bufs_out[i], dev_alloc_size);
                ctx->host_ivs[i] = (uint8_t*)malloc(16);
                real_malloc(&ctx->dev_ivs[i], 32);
                if (real_create_event) {
                    real_create_event(&ctx->events[i]);
                    real_record_event(ctx->events[i], NULL); 
                }
            }
            if (g_kernel_mode == 2) real_malloc(&ctx->dev_keystream, dev_alloc_size);
            else ctx->dev_keystream = NULL;
            ctx->host_checksum = (uint8_t*)malloc(CHECKSUM_SIZE);
            ctx->keystream_ready = 0;
        }
        ctx->ready = 1;
        pthread_setspecific(g_key, ctx);
    }
    return ctx;
}

/* =================================================================
   辅助函数
   ================================================================= */
static void generate_random_iv(uint8_t* iv_buf) {
    for (int i = 0; i < 16; i++) iv_buf[i] = rand() % 256;
}

static void generate_random_mask_fp16(uint8_t* buf, size_t size) {
    uint16_t* ptr = (uint16_t*)buf;
    size_t count = size / 2;
    for (size_t i = 0; i < count; ++i) {
        ptr[i] = 0; // 随机掩码 (当前为0)
    }
}

static uint32_t calc_block_dim(size_t count) {
    const size_t MIN_CHUNK_SIZE = 4096;
    const uint32_t MAX_CORE_NUM = 24;
    if (count <= MIN_CHUNK_SIZE) return 1;
    size_t needed = (count + MIN_CHUNK_SIZE - 1) / MIN_CHUNK_SIZE;
    if (needed > MAX_CORE_NUM) return MAX_CORE_NUM;
    return (uint32_t)needed;
}

static void verify_integrity(const uint8_t* data, size_t len, const uint8_t* npu_checksum, uint32_t blockDim) {
    // 略
}

/* =================================================================
   HCCL Hook (Common TLS)
   ================================================================= */
typedef int (*f_memcpy_async)(void*, size_t, const void*, size_t, int, void*);
typedef int (*f_memcpy_sync)(void*, size_t, const void*, size_t, int);
typedef int (*f_malloc)(void**, size_t);
typedef int (*f_free)(void*);

static __thread void* g_hccl_temp_buf = NULL;
static __thread void* g_hccl_mask_buf = NULL; 
static __thread void* g_hccl_rank_buf = NULL; 
static __thread void* g_hccl_rank_one_buf = NULL; // [New] For AllGather (Rank=1)
static __thread uint64_t g_hccl_temp_cap = 0;

// 通用初始化函数
static int init_hccl_buffers(uint64_t bytes_needed, void* stream) {
    f_malloc real_malloc = (f_malloc)g_real_malloc;
    f_free real_free = (f_free)g_real_free;
    f_memcpy_sync real_memcpy_sync = (f_memcpy_sync)g_real_memcpy;

    if (g_hccl_temp_buf == NULL || bytes_needed > g_hccl_temp_cap) {
        if (g_hccl_temp_buf) {
            real_free(g_hccl_temp_buf);
            real_free(g_hccl_mask_buf);
            real_free(g_hccl_rank_buf);
            real_free(g_hccl_rank_one_buf);
            g_hccl_temp_buf = NULL;
        }
        
        uint64_t alloc_size = (bytes_needed + 2097152 - 1) / 2097152 * 2097152;
        
        if (real_malloc(&g_hccl_temp_buf, alloc_size) != 0 || 
            real_malloc(&g_hccl_mask_buf, alloc_size) != 0 ||
            real_malloc(&g_hccl_rank_buf, 32) != 0 ||
            real_malloc(&g_hccl_rank_one_buf, 32) != 0) {
            return -1; // Fail
        }
        g_hccl_temp_cap = alloc_size;

        uint8_t* host_mask = (uint8_t*)malloc(alloc_size);
        if (host_mask) {
            generate_random_mask_fp16(host_mask, alloc_size);
            real_memcpy_sync(g_hccl_mask_buf, alloc_size, host_mask, alloc_size, 1); 
            free(host_mask);
        }

        static int32_t rank_val = 8;
        real_memcpy_sync(g_hccl_rank_buf, 32, &rank_val, sizeof(int32_t), 1); 
        
        static int32_t rank_one = 1;
        real_memcpy_sync(g_hccl_rank_one_buf, 32, &rank_one, sizeof(int32_t), 1);
    }
    return 0;
}

__attribute__((visibility("default")))
int HcclAllReduce(void *inputPtr, void *outputPtr, uint64_t count, int dataType, int op, void *comm, void *stream) {
    init_symbols();
    if (!g_real_hccl_all_reduce) return -1;

    uint64_t bytes_needed = count * 2; 
    if (init_hccl_buffers(bytes_needed, stream) != 0) {
        return g_real_hccl_all_reduce(inputPtr, outputPtr, count, dataType, op, comm, stream);
    }

    f_memcpy_async real_memcpy_async = (f_memcpy_async)g_real_memcpy_async;
    real_memcpy_async(g_hccl_temp_buf, g_hccl_temp_cap, inputPtr, bytes_needed, 3, stream);

    uint32_t kernel_len = (bytes_needed + 31) / 32 * 32;
    uint32_t blockDim = calc_block_dim(kernel_len);
    
    if (ptr_add_custom_do) {
        ptr_add_custom_do(blockDim, stream, g_hccl_temp_buf, g_hccl_mask_buf, NULL, g_hccl_temp_buf, kernel_len, 3);
    }

    int ret = g_real_hccl_all_reduce(g_hccl_temp_buf, outputPtr, count, dataType, op, comm, stream);

    if (ptr_add_custom_do) {
        ptr_add_custom_do(blockDim, stream, outputPtr, g_hccl_mask_buf, g_hccl_rank_buf, outputPtr, kernel_len, 4);
    }

    return ret;
}

// [New] AllGather Hook
__attribute__((visibility("default")))
int HcclAllGather(void *inputPtr, void *outputPtr, uint64_t count, int dataType, void *comm, void *stream) {
    init_symbols();
    if (!g_real_hccl_all_gather) return -1;

    // AllGather: Input (count) -> Output (count * 8)
    // 加密输入：大小为 count
    uint64_t in_bytes = count * 2;
    
    if (init_hccl_buffers(in_bytes, stream) != 0) {
        return g_real_hccl_all_gather(inputPtr, outputPtr, count, dataType, comm, stream);
    }

    f_memcpy_async real_memcpy_async = (f_memcpy_async)g_real_memcpy_async;
    real_memcpy_async(g_hccl_temp_buf, g_hccl_temp_cap, inputPtr, in_bytes, 3, stream);

    uint32_t kernel_len = (in_bytes + 31) / 32 * 32;
    uint32_t blockDim = calc_block_dim(kernel_len);

    // 1. [ENCRYPT] 加掩码 (Mode 3)
    if (ptr_add_custom_do) {
        ptr_add_custom_do(blockDim, stream, g_hccl_temp_buf, g_hccl_mask_buf, NULL, g_hccl_temp_buf, kernel_len, 3);
    }

    // 2. [HCCL] 执行 Gather
    int ret = g_real_hccl_all_gather(g_hccl_temp_buf, outputPtr, count, dataType, comm, stream);

    if (ptr_add_custom_do) {
        int rank_size = 8;
        for (int i = 0; i < rank_size; i++) {
            // 计算每一段的偏移地址
            uint8_t* ptr_offset = (uint8_t*)outputPtr + (i * in_bytes);
            // 调用 Mode 4, 传入 Rank=1
            ptr_add_custom_do(blockDim, stream, ptr_offset, g_hccl_mask_buf, g_hccl_rank_one_buf, ptr_offset, kernel_len, 4);
        }
    }

    return ret;
}


typedef int (*f_sync)(void*);
typedef int (*f_memset_async)(void*, size_t, int, size_t, void*);
typedef int (*f_sync_event)(aclrtEvent);
typedef int (*f_record_event)(aclrtEvent, aclrtStream);

__attribute__((visibility("default")))
int aclrtMemcpyAsync(void *dst, size_t destMax, const void *src, size_t count, int kind, void *stream) {
    init_symbols();
    f_memcpy_async real_fn = (f_memcpy_async)g_real_memcpy_async;
    if (!real_fn) return 50000;

    static int enable_h2d = -1;
    static int enable_d2h = -1;
    static int enable_check = -1;

    if (enable_h2d == -1) {
        char* eh = getenv("PIPE_ENABLE_H2D");
        char* ed = getenv("PIPE_ENABLE_D2H");
        char* ec = getenv("PIPE_ENABLE_CHECK"); 
        enable_h2d = eh ? atoi(eh) : 1;
        enable_d2h = ed ? atoi(ed) : 1;
        enable_check = ec ? atoi(ec) : 1; 
    }

    if (count > ENCRYPT_THRESHOLD_LIMIT) return real_fn(dst, destMax, src, count, kind, stream);
    if (kind == 1 && !enable_h2d) return real_fn(dst, destMax, src, count, kind, stream);
    if (kind == 2 && !enable_d2h) return real_fn(dst, destMax, src, count, kind, stream);

    proc_ctx_t* ctx = get_ctx();
    if (!ctx || !ctx->ready) return real_fn(dst, destMax, src, count, kind, stream);

    f_sync real_sync_fn = (f_sync)g_real_sync_stream;
    f_memset_async real_memset = (f_memset_async)g_real_memset_async;
    f_sync_event real_sync_event = (f_sync_event)g_real_sync_event;
    f_record_event real_record_event = (f_record_event)g_real_record_event;

    // H2D
    if (kind == 1) { 
        size_t align = ((count + 511) / 512) * 512;
        if (align == 0) align = 512;
        if (align > ctx->host_len) return real_fn(dst, destMax, src, count, kind, stream);

        int slot = ctx->current_slot;
        real_sync_event(ctx->events[slot]);
        generate_random_iv(ctx->host_ivs[slot]);

        ptr_EVP_EncryptInit_ex(ctx->enc_ctx, ptr_EVP_aes_128_ctr(), NULL, g_fixed_key, ctx->host_ivs[slot]);
        int len1, len2;
        ptr_EVP_EncryptUpdate(ctx->enc_ctx, ctx->host_bufs[slot], &len1, (const uint8_t*)src, count);
        ptr_EVP_EncryptFinal_ex(ctx->enc_ctx, ctx->host_bufs[slot] + len1, &len2);
        if (align > count) memset(ctx->host_bufs[slot] + count, 0, align - count);

        real_fn(ctx->dev_ivs[slot], 32, ctx->host_ivs[slot], 16, 1, stream);
        real_fn(ctx->dev_bufs_in[slot], align, ctx->host_bufs[slot], align, 1, stream);

        uint32_t blockDim = calc_block_dim(align);
        if (ptr_add_custom_do) {
             ptr_add_custom_do(blockDim, stream, ctx->dev_bufs_in[slot], ctx->dev_key, ctx->dev_ivs[slot], ctx->dev_bufs_out[slot], align, 0);
        } else {
             real_fn(ctx->dev_bufs_out[slot], align, ctx->dev_bufs_in[slot], align, 3, stream);
        }

        real_fn(dst, destMax, ctx->dev_bufs_out[slot], count, 3, stream);
        real_record_event(ctx->events[slot], stream);
        ctx->current_slot = (ctx->current_slot + 1) % SLOT_NUM;
        return 0;
    }

    // D2H
    if (kind == 2) { 
        size_t align = ((count + 511) / 512) * 512;
        if (align == 0) align = 512;
        if (align > ctx->host_len) return real_fn(dst, destMax, src, count, kind, stream);

        int slot = ctx->current_slot; 
        generate_random_iv(ctx->host_ivs[slot]);
        
        real_fn(ctx->dev_ivs[slot], 32, ctx->host_ivs[slot], 16, 1, stream);
        real_fn(ctx->dev_bufs_in[slot], align, src, count, 3, stream);
        if (align > count) real_memset((uint8_t*)ctx->dev_bufs_in[slot] + count, align - count, 0, align - count, stream);

        uint32_t blockDim = calc_block_dim(align);
        if (ptr_add_custom_do) {
            ptr_add_custom_do(blockDim, stream, ctx->dev_bufs_in[slot], ctx->dev_key, ctx->dev_ivs[slot], ctx->dev_bufs_out[slot], align, 0);
        } else {
            real_fn(ctx->dev_bufs_out[slot], align, ctx->dev_bufs_in[slot], align, 3, stream);
        }

        real_fn(ctx->host_bufs[slot], align, ctx->dev_bufs_out[slot], align, 2, stream);
        real_sync_fn(stream);

        ptr_EVP_DecryptInit_ex(ctx->dec_ctx, ptr_EVP_aes_128_ctr(), NULL, g_fixed_key, ctx->host_ivs[slot]);
        int len1, len2;
        ptr_EVP_DecryptUpdate(ctx->dec_ctx, (uint8_t*)dst, &len1, ctx->host_bufs[slot], count);
        ptr_EVP_DecryptFinal_ex(ctx->dec_ctx, (uint8_t*)dst + len1, &len2);

        if (enable_check) {
            verify_integrity(ctx->host_bufs[slot], count, NULL, blockDim);
        }

        return 0;
    }

    return real_fn(dst, destMax, src, count, kind, stream);
}

// 同步 Memcpy 包装
__attribute__((visibility("default")))
int aclrtMemcpy(void *dst, size_t destMax, const void *src, size_t count, int kind) {
    init_symbols();
    typedef int (*f_memcpy)(void*, size_t, const void*, size_t, int);
    f_memcpy real_fn = (f_memcpy)g_real_memcpy;
    if (!real_fn) return 50000;

    if (count <= ENCRYPT_THRESHOLD_LIMIT && (kind == 1 || kind == 2)) {
        typedef int (*f_create)(void**);
        typedef int (*f_destroy)(void*);
        typedef int (*f_sync)(void*);
        f_create r_create = (f_create)g_real_create_stream;
        f_destroy r_destroy = (f_destroy)g_real_destroy_stream;
        f_sync r_sync = (f_sync)g_real_sync_stream;
        if (r_create && r_destroy && r_sync) {
            void* tmp;
            r_create(&tmp);
            aclrtMemcpyAsync(dst, destMax, src, count, kind, tmp);
            r_sync(tmp);
            r_destroy(tmp);
            return 0;
        }
    }
    return real_fn(dst, destMax, src, count, kind);
}

long long pipellm_get_total_bytes_processed() { return 0; }
