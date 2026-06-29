#include "pipellm.h"
#include "hack.h"
#include <dlfcn.h>
#include <iostream>
#include <cstdio>
#include <mutex>
#include <algorithm>
#include <thread>
#include <cstring>
#include <chrono>
#include <cassert>
#include <dlfcn.h>
#include <atomic>
std::mutex g_ctx_map_mutex;
static bool enable_encrypt = false;
static bool enable_h2d_encrypt = false;
static bool enable_d2h_encrypt = false;
static bool enable_decrypt = false;
extern size_t encrypt_magic_sz = 0xabcde;
extern size_t decrypt_magic_sz = 0xabcde;
std::map<EVP_CIPHER_CTX *, std::shared_ptr<encrypt_metadata>> m_ctx_metadata;
std::map<size_t, EVP_CIPHER_CTX *> m_magic_encctx;
std::set<size_t> s_magic;
std::map<EVP_CIPHER_CTX *, std::shared_ptr<decrypt_metadata>> m_ctx_dmetadata;
std::map<size_t, EVP_CIPHER_CTX *> m_magic_decctx;
std::set<size_t> s_magic_dec;
constexpr static int encrypt_workers_num_max = 16;
static int encrypt_workers_num = 1;
static int encrypt_workers_per_commit = 1;
constexpr static int decrypt_workers_num_max = 2;
static int decrypt_workers_num = 1;
int memcpy_thread_num = 1;
int commit_workers_num = encrypt_workers_num / encrypt_workers_per_commit;
encrypt_worker_entry *encrypt_worker_entrys[encrypt_workers_num_max];
std::thread *encrypt_worker_threads[encrypt_workers_num_max];
std::thread *commit_worker_threads[encrypt_workers_num_max];
decrypt_worker_entry *decrypt_worker_entrys[decrypt_workers_num_max];
std::thread *decrypt_worker_threads[decrypt_workers_num_max];
std::thread *decrypt_manager_threads[decrypt_workers_num_max];

Predictor predictor;
void sync_predictor();
// 适配 add_custom_do: (uint32_t blockDim, void* stream, uint8_t* input, uint8_t* key, uint8_t* iv, uint8_t* output, uint32_t totalLength, uint32_t mode)
typedef void (*LaunchAESKernel_t)(void*, void*, void*, void*, void*, uint32_t);
LaunchAESKernel_t g_launch_kernel = nullptr;
static void* g_aes_lib_handle = nullptr;
void* g_dev_key = nullptr;
void* g_dev_iv = nullptr;

std::atomic<bool> g_running(true);

// void __attribute__((destructor)) pipellm_fini() {
//     fprintf(stderr, "[PipeLLM] 🛑 正在停止 Worker 线程...\n");
//     g_running = false;
//     // 稍微等一下让线程有机会退出
//     usleep(200000); 
//     fprintf(stderr, "[PipeLLM] 👋 PipeLLM 退出完成\n");
// }

// [修改加载函数，加载新NPU算子]
static void load_aes_library() {
    if (g_launch_kernel) return;

    fprintf(stderr, "[PipeLLM] 🔄 正在加载 NPU 算子库...\n");

    // 1. 先加载 ACL 库 (RTLD_GLOBAL)
    void* acl_handle = dlopen("libascendcl.so", RTLD_NOW | RTLD_GLOBAL);
    if (!acl_handle) {
        acl_handle = dlopen("/usr/local/Ascend/ascend-toolkit/latest/lib64/libascendcl.so", RTLD_NOW | RTLD_GLOBAL);
    }
    if (!acl_handle) {
        fprintf(stderr, "[PipeLLM] ⚠️ ACL库加载失败（非致命）: %s\n", dlerror());
    }

    // 2. 加载 kernel 库并获取符号
    const char* kernel_lib_path = "/data/lzy/ops/aes-ctr/build/lib/libascendc_kernels_npu.so";
    void* kernel_handle = dlopen(kernel_lib_path, RTLD_NOW | RTLD_GLOBAL);
    if (!kernel_handle) {
        fprintf(stderr, "[PipeLLM] ❌ 加载 Kernel 库失败: %s\n", dlerror());
        return;
    }

    // 尝试找 C++ 修饰的符号名
    void* sym = dlsym(kernel_handle, "_Z13add_custom_dojPvPhS0_S0_S0_jj");
    if (sym) {
        g_launch_kernel = (LaunchAESKernel_t)sym;
        fprintf(stderr, "[PipeLLM] ✅ 成功加载 NPU 加解密算子库 (C++ 符号)\n");
        fprintf(stderr, "[PipeLLM]    Addr: %p\n", (void*)g_launch_kernel);
    } else {
        // 尝试 C 符号
        sym = dlsym(kernel_handle, "add_custom_do");
        if (sym) {
            g_launch_kernel = (LaunchAESKernel_t)sym;
            fprintf(stderr, "[PipeLLM] ✅ 成功加载 NPU 加解密算子库 (C 符号)\n");
            fprintf(stderr, "[PipeLLM]    Addr: %p\n", (void*)g_launch_kernel);
        } else {
            fprintf(stderr, "[PipeLLM] ❌ 无法找到 add_custom_do 符号: %s\n", dlerror());
        }
    }
}
/*
 * [替换] cudaDeviceSynchronize
 */
HOOK_C_API HOOK_DECL_EXPORT aclError aclrtSynchronizeDevice()
{
    predictor.lock();
    sync_predictor();
    predictor.unlock();

    return real_aclrtSynchronizeDevice(); // <-- [修改]
}

/*
 * [替换] cudaStreamSynchronize
 */
HOOK_C_API HOOK_DECL_EXPORT aclError aclrtSynchronizeStream(aclrtStream stream) // <-- [修改]
{
    predictor.lock();
    sync_predictor();
    predictor.unlock();

    return real_aclrtSynchronizeStream(stream); // <-- [修改]
}

/*
 * [替换] cudaStreamWaitEvent
 * [注意] ACL API 没有 flags 参数
 */
HOOK_C_API HOOK_DECL_EXPORT aclError aclrtStreamWaitEvent(aclrtStream stream, aclrtEvent event) // <-- [修改]
{
    predictor.lock();
    sync_predictor();
    predictor.unlock();

    return real_aclrtStreamWaitEvent(stream, event); // <-- [修改]
}

static bool first = 0;

static inline void init_encrypt_ctx(aclrtMemcpyKind kind, aclrtStream stream)
{
    static bool first = true;
    if (first) {
        first = false;
        
        // 加载NPU算子库
        load_aes_library();
        
        fprintf(stderr, "[PipeLLM Debug] ===== init_encrypt_ctx 开始 =====\n");
        
        // 读取环境变量
        auto enable_encrypt_env = std::getenv("PIPELLM_ENABLE_ENCRYPT");
        auto enable_decrypt_env = std::getenv("PIPELLM_ENABLE_DECRYPT");
        
        auto enable_h2d_env = std::getenv("PIPE_ENABLE_H2D");
        auto enable_d2h_env = std::getenv("PIPE_ENABLE_D2H");
        
        enable_h2d_encrypt = enable_h2d_env ? std::atoi(enable_h2d_env) != 0 : false;
        enable_d2h_encrypt = enable_d2h_env ? std::atoi(enable_d2h_env) != 0 : false;
        
        // bool enable_encrypt = enable_encrypt_env ? std::atoi(enable_encrypt_env) != 0 : false;
        // bool enable_decrypt = enable_decrypt_env ? std::atoi(enable_decrypt_env) != 0 : false;
        
        // 内部使用全局变量控制worker初始化
        bool global_enable_encrypt = (enable_h2d_encrypt || enable_d2h_encrypt);
        
        fprintf(stderr, "[PipeLLM Debug] enable_h2d_encrypt=%d, enable_d2h_encrypt=%d, global_enable=%d\n", 
                enable_h2d_encrypt, enable_d2h_encrypt, global_enable_encrypt);
        
        // 阶段2: 只创建加密工作线程（如果启用）
        if (global_enable_encrypt) {
            fprintf(stderr, "[PipeLLM Debug] 阶段2: 创建加密工作线程\n");
            
            // 简化版本，只创建1个工作线程
            encrypt_workers_num = 1;
            encrypt_workers_per_commit = 1;
            commit_workers_num = 1;
            
            for (int i = 0; i < commit_workers_num; i++) {
                fprintf(stderr, "[PipeLLM Debug] 创建commit worker %d\n", i);
                auto worker_entry = new encrypt_worker_entry;
                encrypt_worker_entrys[i] = worker_entry;
                worker_entry->id = i;
                worker_entry->magic_sz = encrypt_magic_sz + i;
                worker_entry->commit_init_done = false;
                worker_entry->encrypt_workers_per_commit = encrypt_workers_per_commit;
                s_magic.insert(worker_entry->magic_sz);
                
                // 创建commit worker
                commit_worker_threads[i] = new std::thread(commit_worker, worker_entry);
                while (!worker_entry->commit_init_done); // 等待初始化完成


                // 在创建 encrypt worker 之前添加检查
                for (int j = 0; j < encrypt_workers_per_commit; j++) {
                    fprintf(stderr, "[PipeLLM Debug] 准备创建encrypt worker %d-%d\n", i, j);
                    
                    // 检查 worker_entry 状态
                    fprintf(stderr, "[PipeLLM Debug] worker_entry->id=%d\n", worker_entry->id);
                    fprintf(stderr, "[PipeLLM Debug] worker_entry->local_id=%d\n", worker_entry->local_id);
                    fprintf(stderr, "[PipeLLM Debug] worker_entry->encrypt_workers_per_commit=%d\n", 
                            worker_entry->encrypt_workers_per_commit);
                    fprintf(stderr, "[PipeLLM Debug] worker_entry->magic_sz=%zu\n", worker_entry->magic_sz);
                    
                    worker_entry->enc_init_done = false;
                    worker_entry->local_id = j;
                    
                    fprintf(stderr, "[PipeLLM Debug] 创建encrypt worker线程\n");
                    fflush(stderr);
                    
                    encrypt_worker_threads[i * encrypt_workers_per_commit + j] = 
                        new std::thread(encrypt_worker, worker_entry);
                    
                    fprintf(stderr, "[PipeLLM Debug] 等待encrypt worker初始化\n");
                    fflush(stderr);
                    
                    while (!worker_entry->enc_init_done); // 等待初始化完成
                    
                    fprintf(stderr, "[PipeLLM Debug] encrypt worker %d-%d 初始化完成\n", i, j);
                    fflush(stderr);
                }

                // 创建encrypt worker
                for (int j = 0; j < encrypt_workers_per_commit; j++) {
                    fprintf(stderr, "[PipeLLM Debug] 创建encrypt worker %d-%d\n", i, j);
                    worker_entry->enc_init_done = false;
                    worker_entry->local_id = j;
                    encrypt_worker_threads[i * encrypt_workers_per_commit + j] = 
                        new std::thread(encrypt_worker, worker_entry);
                    while (!worker_entry->enc_init_done); // 等待初始化完成
                }
            }
            fprintf(stderr, "[PipeLLM Debug] 加密工作线程创建完成\n");
        }
        
        fprintf(stderr, "[PipeLLM Debug] ===== init_encrypt_ctx 完成 =====\n");
        fflush(stderr);
    }
}

/*
 * (此函数 100% 保持不变)
 */

void assign_to_workers(void *dst, const void *src, size_t count, bool decrypt = false, bool predict = true, bool commit = false, bool update_predict_iv = false, bool batch_commit = false, aclrtStream user_stream = nullptr)
{
    const char *src_char = (const char *)src;
    char *dst_char = (char *)dst;

    size_t div = (count + commit_workers_num - 1) / commit_workers_num;
    div = (div + block_unit - 1) / block_unit * block_unit;

    // --- Event Pool 初始化 (保持不变) ---
    static std::vector<aclrtEvent> g_worker_events;
    static std::mutex g_init_mutex;
    if (g_worker_events.empty()) {
        std::lock_guard<std::mutex> lock(g_init_mutex);
        if (g_worker_events.empty()) {
            g_worker_events.resize(commit_workers_num, nullptr);
            for (int k = 0; k < commit_workers_num; k++) {
                real_aclrtCreateEvent(&g_worker_events[k]); 
            }
        }
    }
    // ------------------------------------

    for (int i = 0; i < commit_workers_num; i++) {
        auto worker_entry = encrypt_worker_entrys[i];
        auto sz = std::min(count, div);
        
        if (predict) {
            // 预测路径保持不变...
            encryption_task task;
            task.src = src_char;
            task.size = sz;
            task.iv_increment = 32;
            {
                std::lock_guard<std::mutex> lock(worker_entry->lock);
                worker_entry->enc_tasks.push_back(task);
            }
        } else {
            commit_task task;
            worker_entry->commit = true;
            task.enc_task.src = src_char;
            task.enc_task.size = sz;
            task.dst = dst_char;
            task.using_predict = commit;
            task.update_predict_iv = update_predict_iv;

            // [新增] 定义一个栈上的原子变量，用于握手
            std::atomic<bool> is_submitted(false);

            if (commit && user_stream != nullptr) {
                aclrtEvent evt = g_worker_events[i];
                task.sync_event = evt;
                
                // [关键] 将原子变量的地址传给 Worker
                task.submitted_flag = &is_submitted; 

            } else {
                task.sync_event = nullptr;
                task.submitted_flag = nullptr;
            }

            // 1. 推送任务
            {
                std::lock_guard<std::mutex> lock(worker_entry->lock);
                worker_entry->commit_tasks.push_back(task);
            }

            // 2. [关键修复] 自旋等待：确保 Worker 已经执行了 RecordEvent
            // 只有当 is_submitted 变为 true 后，主线程才能继续往下走。
            // 因为 Record 只是向队列发指令，这个过程极快 (10~30us)，不会造成性能瓶颈。
            if (commit && user_stream != nullptr) {
                while (!is_submitted.load()) {
                    // CPU 空转等待，比 sleep 更快响应
                    std::this_thread::yield(); 
                }
                
                // 3. 现在可以安全地 Wait 了，因为 Record 肯定已经发生了
                real_aclrtStreamWaitEvent(user_stream, task.sync_event);
            }
        }
        
        src_char += sz;
        dst_char += sz;
        count -= sz;
        if (count == 0) break;
    }
}


void sync_predictor()
{
    // Caller must hold the predictor's lock
    if (!first || !(enable_h2d_encrypt || enable_d2h_encrypt)) return;
    assert(predictor.locking());
    if (!predictor.pending_commit.empty()) {
        assert(encrypt_workers_num == 1);
        {
            std::lock_guard<std::mutex> lock(encrypt_worker_entrys[0]->lock);
            encrypt_worker_entrys[0]->commit = true;
            for (auto &task: predictor.pending_commit) {
                commit_task task0;
                task0.enc_task.src = task.first;
                task0.enc_task.size = task.second.second;
                task0.dst = task.second.first;
                task0.using_predict = true;
                task0.update_predict_iv = false;
                encrypt_worker_entrys[0]->commit_tasks.push_back(task0);
            }
        }
        predictor.pending_commit.clear();
    }

    if (!predictor.pending_decrypt.empty()) {
        assert(decrypt_workers_num == 1);
        {
            std::lock_guard<std::mutex> lock(decrypt_worker_entrys[0]->lock);
            for (auto &task: predictor.pending_decrypt) {
                decrypt_worker_entrys[0]->dec_tasks.push_back(task);
            }
        }
        predictor.pending_decrypt.clear();
    }
    for (int i = 0; i < commit_workers_num; i++) {
        while (1) {
            {
                if (encrypt_worker_entrys[i]->commit == false) {
                    break;
                }
            }
        }
    }
    predictor.pending_commit.clear();
}

aclrtContext g_main_ctx = nullptr;
HOOK_C_API HOOK_DECL_EXPORT aclError aclrtMemcpyAsync(void *dst, size_t destMax, const void *src, size_t count,
                                                       aclrtMemcpyKind kind, aclrtStream stream)
{
    if (g_main_ctx == nullptr) {
        aclrtGetCurrentContext(&g_main_ctx);
        if (g_main_ctx != nullptr) {
            fprintf(stderr, "[PipeLLM] ✅ 捕获主线程 Context: %p\n", g_main_ctx);
        }
    }
    // 1. 初始化加密上下文
    init_encrypt_ctx(kind, stream);
    // 2. 基础参数检查
    if (count == 0) return ACL_SUCCESS;
    if (dst == nullptr || src == nullptr) {
        return real_aclrtMemcpyAsync(dst, destMax, src, count, kind, stream);
    }

    // 3. 设备内部拷贝 (D2D) 直接放行
    if (kind == ACL_MEMCPY_DEVICE_TO_DEVICE) {
        return real_aclrtMemcpyAsync(dst, destMax, src, count, kind, stream);
    }

    // =================================================================
    // 4. 加密路径 (Host -> Device) —— 全量加密
    // =================================================================
    if (enable_h2d_encrypt && kind == ACL_MEMCPY_HOST_TO_DEVICE && count >= encrypt_threshold_sz) {
        fprintf(stderr, "[PipeLLM] H2D+Encrypt: src=%p, size=%zu (NPU)\n", src, count);
    
        predictor.lock();
    
        // ==================== 情况 1: 线性预测模式（vLLM 专用） ====================
        if (predictor.linear_mode && count == predictor.linear_stride && 0) {
            void* predicted_next = (uint8_t*)src + predictor.linear_stride;
            fprintf(stderr, "[PipeLLM] 线性预测命中: 当前=%p → 预测=%p\n", src, predicted_next);
    
            assign_to_workers(nullptr, predicted_next, count, false, true, false, false, false, stream);
            assign_to_workers(dst, src, count, false, false, true, false, false, stream);
            goto POST_PROCESS;
        }
    
        // ==================== 情况 2: 重复循环模式（原有） ====================
        // if (predictor.read_only_swap_profiled) {
        //     auto &record = predictor.read_only_swap_record;
        //     bool hit = src == record[predictor.read_only_swap_cur_idx].first &&
        //                count == record[predictor.read_only_swap_cur_idx].second;
    
        //     if (!hit) goto SERIAL_FALLBACK;
    
        //     predictor.read_only_swap_cur_idx = (predictor.read_only_swap_cur_idx + 1) % record.size();
        //     predictor.read_only_swap_pred_idx = (predictor.read_only_swap_pred_idx + 1) % record.size();
    
        //     auto &entry = record[predictor.read_only_swap_pred_idx];
        //     assign_to_workers(nullptr, entry.first, entry.second, false, true, false, false, false, stream);
        //     assign_to_workers(dst, src, count, false, false, true, false, false, stream);
        //     goto POST_PROCESS;
        // }
    
        // // ==================== 情况 3: 其他已知模式 ====================
        // else if (predictor.other_swap_set.find(std::make_pair(src, count)) != predictor.other_swap_set.end()) {
        //     predictor.other_swap_set.erase(std::make_pair(src, count));
        //     assign_to_workers(nullptr, src, count, false, true, false, false, false, stream);
        //     assign_to_workers(dst, src, count, false, false, true, false, false, stream);
        //     goto POST_PROCESS;
        // }
    
        // ==================== 情况 4: 模式发现阶段（新增线性检测） ====================
        else if (!predictor.read_only_swap_profiled && !predictor.linear_mode) {
            // fprintf(stderr, "[PipeLLM] 模式发现中 (size=%zu)\n", count);
    
            auto &record = predictor.read_only_swap_record;
            record.push_back(std::make_pair(src, count));
    
            assign_to_workers(nullptr, src, count, false, true, false, false, false, stream);
            assign_to_workers(dst, src, count, false, false, true, false, false, stream);
            sync_predictor();
    
            // --- 线性模式检测 ---
            if (record.size() >= 8) {
                auto& prev = record[record.size() - 2];
                if (count == prev.second &&
                    (uint8_t*)src > (uint8_t*)prev.first &&
                    ((uint8_t*)src - (uint8_t*)prev.first) == count) {
                    predictor.linear_count++;
                    if (predictor.linear_count >= predictor.LINEAR_THRESHOLD) {
                        predictor.linear_mode = true;
                        predictor.linear_stride = count;
                        fprintf(stderr, "[PipeLLM] ✅ 进入线性预测模式 (stride=%zu)\n", count);
                    }
                } else {
                    predictor.linear_count = 0;
                }
            }
    
            // --- 原有重复模式检测 ---
            // bool found = true;
            // const int repeat = 32;
            // auto size = record.size();
            // if (size < repeat * 2 || (size % 2 == 1)) found = false;
            // else {
            //     auto mid = size / 2;
            //     for (int i = 0; i < size / 2; i++) {
            //         if (record[i] != record[mid + i]) { found = false; break; }
            //     }
            // }
    
            // if (found) {
            //     fprintf(stderr, "[PipeLLM] 发现重复模式! 开启预测优化\n");
            //     for (int i = 0; i < size / 2; i++) record.pop_back();
            //     predictor.read_only_swap_profiled = true;
            //     predictor.read_only_swap_cur_idx = (size / 2) % record.size();
            //     predictor.read_only_swap_pred_idx = (predictor.read_only_swap_cur_idx + 1) % record.size();
    
            //     auto &entry1 = record[predictor.read_only_swap_pred_idx];
            //     assign_to_workers(nullptr, entry1.first, entry1.second, false, true, false, false, false, stream);
            // }
            goto POST_PROCESS;
        }
    
    SERIAL_FALLBACK:
        assign_to_workers(nullptr, src, count, false, true, false, false, false, stream);
        assign_to_workers(dst, src, count, false, false, true, false, false, stream);
    
    POST_PROCESS:
        predictor.unlock();
        return ACL_SUCCESS;
    }

    // =================================================================
    // 5. 解密路径 (Device -> Host) - 同步方式
    // =================================================================
    if (enable_d2h_encrypt && kind == ACL_MEMCPY_DEVICE_TO_HOST && count >= decrypt_threshold_sz) {
        fprintf(stderr, "[PipeLLM] D2H+Decrypt START: src=%p, dst=%p, size=%zu\n", src, dst, count);

        if (m_ctx_metadata.empty()) {
            return real_aclrtMemcpyAsync(dst, destMax, src, count, kind, stream);
        }

        auto &meta = *m_ctx_metadata.begin()->second;
        if (meta.dev_key == nullptr) {
            real_aclrtMalloc(&meta.dev_key, 32);
            real_aclrtMalloc(&meta.dev_iv, 16);
            real_aclrtMemcpyAsync(meta.dev_key, 32, meta.key, 32, ACL_MEMCPY_HOST_TO_DEVICE, stream);
            real_aclrtMemcpyAsync(meta.dev_iv, 16, meta.init_iv, 16, ACL_MEMCPY_HOST_TO_DEVICE, stream);
            fprintf(stderr, "[PipeLLM] ✅ NPU Key/IV 内存初始化完成\n");
        }
        g_dev_key = meta.dev_key;
        g_dev_iv = meta.dev_iv;

        // 同步解密
        static void* d2h_temp_in = nullptr;
        static void* d2h_temp_out = nullptr;
        static size_t d2h_temp_size = 0;
        if (d2h_temp_size < count) {
            if (d2h_temp_in) real_aclrtFree(d2h_temp_in);
            if (d2h_temp_out) real_aclrtFree(d2h_temp_out);
            real_aclrtMalloc(&d2h_temp_in, count);
            real_aclrtMalloc(&d2h_temp_out, count);
            d2h_temp_size = count;
        }

        // Step1: D2D
        fprintf(stderr, "[PipeLLM] D2H Step1 D2D: src=%p -> tmp_in=%p, size=%zu\n", src, d2h_temp_in, count);
        real_aclrtMemcpyAsync(d2h_temp_in, count, src, count, ACL_MEMCPY_DEVICE_TO_DEVICE, stream);

        // Step2: Kernel
        fprintf(stderr, "[PipeLLM] D2H Step2 Kernel: tmp_in=%p, key=%p, iv=%p, tmp_out=%p, size=%zu\n",
                d2h_temp_in, meta.dev_key, meta.dev_iv, d2h_temp_out, count);
        if (g_launch_kernel && meta.dev_key && meta.dev_iv) {
            uint32_t blockDim = (count > 131072) ? 8 : (count > 32768) ? 4 : (count > 4096) ? 2 : 1;
            // 使用正确的函数指针类型调用
            typedef void (*NPUKernel_t)(uint32_t, void*, uint8_t*, uint8_t*, uint8_t*, uint8_t*, uint32_t, uint32_t);
            ((NPUKernel_t)g_launch_kernel)(
                blockDim,
                stream,
                (uint8_t*)d2h_temp_in,
                (uint8_t*)meta.dev_key,
                (uint8_t*)meta.dev_iv,
                (uint8_t*)d2h_temp_out,
                (uint32_t)count,
                0
            );
        }

        // Step3: D2H
        fprintf(stderr, "[PipeLLM] D2H Step3 D2H: tmp_out=%p -> dst=%p, size=%zu\n", d2h_temp_out, dst, count);
        real_aclrtMemcpyAsync(dst, destMax, d2h_temp_out, count, ACL_MEMCPY_DEVICE_TO_HOST, stream);

        // Sync
        real_aclrtSynchronizeStream(stream);
        fprintf(stderr, "[PipeLLM] D2H+Decrypt END: dst=%p, size=%zu\n", dst, count);

        return ACL_SUCCESS;
    }

    // 6. 默认回传
    if (count >= 100000) {
        fprintf(stderr, "[PipeLLM] D2H: dst=%p, src=%p, size=%zu\n", dst, src, count);
    }
    return real_aclrtMemcpyAsync(dst, destMax, src, count, kind, stream);
}





HOOK_C_API HOOK_DECL_EXPORT
void EVP_CIPHER_CTX_free(EVP_CIPHER_CTX *ctx)
{
    // 真实的 OpenSSL 会处理 nullptr，我们也一样
    if (ctx == nullptr) {
        real_EVP_CIPHER_CTX_free(ctx);
        return;
    }

    // [!!! 关键修复 !!!]
    // 在调用真实的 free 之前，必须从我们的 map 中移除这个 ctx，
    // 以防止“用后释放” (Use-After-Free) 漏洞。
    // 同时，必须使用互斥锁来保证线程安全。
    {
        std::lock_guard<std::mutex> lock(g_ctx_map_mutex);
        
        m_ctx_metadata.erase(ctx);
        m_ctx_dmetadata.erase(ctx);
    } 
    // 互斥锁在这里自动释放

    // 现在 ctx 已经从我们的 map 中安全移除，
    // 我们可以安全地调用真实的 free 函数了。
    real_EVP_CIPHER_CTX_free(ctx);
}


// 修正后的桥接函数
extern "C" HOOK_DECL_EXPORT aclError aclrtUseStreamResInCurrentThread(aclrtStream stream) {
    // 定义函数指针类型，匹配 aclError(aclrtStream)
    typedef aclError (*func_ptr)(aclrtStream);
    
    // 1. 优先寻找带 Impl 后缀的真实实现（CANN 8.5.1 内部实际使用的符号）
    static func_ptr real_func = (func_ptr)dlsym(RTLD_NEXT, "aclrtUseStreamResInCurrentThreadImpl");
    
    if (!real_func) {
        // 2. 如果没找到 Impl，尝试找不带后缀的原始符号
        // 注意：这里必须用 RTLD_NEXT 避免死循环指向自己
        real_func = (func_ptr)dlsym(RTLD_NEXT, "aclrtUseStreamResInCurrentThread");
    }

    if (real_func) {
        return real_func(stream);
    }

    // 3. 兜底逻辑：如果底层库里真的两个符号都没有，
    // 我们返回成功(0)，防止 Python 因为加载 .so 失败而直接崩溃
    return ACL_SUCCESS; 
}
