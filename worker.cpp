/*
 * 文件名: worker.cpp (已修改为 ACL 版本)
 * 职责: 实现加密/解密/提交的工作线程
 */

 #include "pipellm.h"
 #include "hack.h"
 #include <unistd.h>
 #include <cstring>
 #include <cassert>
 #include <chrono> // 添加 chrono 头文件 (您的代码中使用了)
 #include <sys/syscall.h>
#include <unistd.h>
#include <atomic>

extern LaunchAESKernel_t g_launch_kernel;
extern void* g_dev_key;
extern void* g_dev_iv;
extern aclrtContext g_main_ctx;
extern std::atomic<bool> g_running;

// 获取线程ID，区分是谁在打印
static long get_tid() { return syscall(SYS_gettid); }
static long long current_micros() {
    auto now = std::chrono::system_clock::now();
    auto duration = now.time_since_epoch();
    return std::chrono::duration_cast<std::chrono::microseconds>(duration).count();
}

static void busy_wait_us(long microseconds) {
    auto start = std::chrono::high_resolution_clock::now();
    while (true) {
        auto now = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(now - start).count();
        if (duration >= microseconds) {
            break;
        }
        // 不调用 sleep/yield，死循环空转 CPU
    }
}
#define LOG_PIPE(fmt, ...) fprintf(stderr, "[%lld][TID:%ld] " fmt "\n", current_micros(), get_tid(), ##__VA_ARGS__)

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

struct GCBatch {
    aclrtEvent event;           // 用于标记这一批次何时完成
    std::vector<void*> buffers; // 待释放的 char* buffer
    std::vector<encryption_entry*> entries; // 待释放的 entry 对象
};

void process_garbage_collection(std::deque<GCBatch>& gc_queue) {
    while (!gc_queue.empty()) {
        GCBatch& front = gc_queue.front();
        aclrtEventStatus status = ACL_EVENT_STATUS_NOT_READY;
        
        // 非阻塞查询事件状态
        aclError ret = aclrtQueryEvent(front.event, &status);
        
        if (ret == ACL_SUCCESS && status == ACL_EVENT_STATUS_COMPLETE) {
            // 事件已完成，NPU 已经不再使用这些内存，安全释放
            for (void* buf : front.buffers) {
                delete[] (char*)buf;
            }
            for (auto* entry : front.entries) {
                delete entry;
            }
            // 销毁事件资源
            aclrtDestroyEvent(front.event);
            gc_queue.pop_front();
            
            // fprintf(stderr, "[GC] 释放了一批内存\n");
        } else {
            // 队列头部的事件还没完成，后面的肯定也没完成（因为是顺序流），直接退出
            break;
        }
    }
}
/*
 * ==========================================================================
 * 函数: encrypt_worker (增强版本)
 * ==========================================================================
 */
    void encrypt_worker(void *entry)
    {
        // while(1) sleep(1000);
        fprintf(stderr, "[encrypt_worker] 🚀 函数开始执行\n");
        fflush(stderr);
        
        auto x = (struct encrypt_worker_entry *)entry;
        auto worker_id = x->id;
        auto local_id = x->local_id;
        
        fprintf(stderr, "[encrypt_worker] worker_id=%d, local_id=%d, magic_sz=%zu\n", 
                worker_id, local_id, x->magic_sz);
        fflush(stderr);
        
        // 绑定核心
        // bind_core((x->encrypt_workers_per_commit + 1) * worker_id + local_id + 1);
        fprintf(stderr, "[encrypt_worker] ✅ 核心绑定完成\n");
        fflush(stderr);
    
        // 🎯 [关键修复] 增强的等待机制
        fprintf(stderr, "[encrypt_worker] 🔍 开始等待加密上下文初始化...\n");
        fflush(stderr);
        
        EVP_CIPHER_CTX *enc_ctx = nullptr;
     int retry_count = 0;
     const int max_retry = 500; // 最多重试500次，约5秒
     
     while (enc_ctx == nullptr && retry_count < max_retry) {
         // 安全地访问映射表
         { // [!!! 关键修复：添加大括号和锁 !!!]
            
            // 在访问 m_magic_encctx 之前加锁
            // std::lock_guard<std::mutex> lock(g_ctx_map_mutex);
            
            auto it = m_magic_encctx.find(x->magic_sz); 

            // (您之前的正确逻辑)
            if (it != m_magic_encctx.end()) {
                // 找到了 key
                if (it->second != nullptr) {
                    // 找到了有效的 ctx
                    enc_ctx = it->second;
                    fprintf(stderr, "[encrypt_worker] 循环 %d: ✅ 成功找到 key 且 ctx 有效: %p\n", retry_count, (void*)enc_ctx);
                    fflush(stderr);
                } else {
                    // 找到了 key，但是 value 是 nullptr (占位符)
                    // fprintf(stderr, "[encrypt_worker] 循环 %d: ⚠️  找到 key, 但 ctx 仍为 nullptr (正在等待 OpenSSL 握手...)\n", retry_count);
                    fflush(stderr);
                }
            } else {
                // 连 key 都没找到 (这不应该发生)
                fprintf(stderr, "[encrypt_worker] 循环 %d: ❌ 未找到 key (magic_sz: %zu)! pipellm.cpp 的 init 是否先运行了?\n", retry_count, x->magic_sz);
                fflush(stderr);
            }
        
        } // [!!! 锁在这里自动释放 !!!]
        if (enc_ctx != nullptr) {
            break; // 如果找到了，退出循环
        }
         // 等待并重试
         usleep(100); // 等待10ms
         retry_count++;
         
         if (retry_count % 50 == 0) {
             fprintf(stderr, "[encrypt_worker] ⏳ 等待加密上下文... (%d/%d)\n", retry_count, max_retry);
             fprintf(stderr, "[encrypt_worker] m_magic_encctx 大小: %zu\n", m_magic_encctx.size());
             fflush(stderr);
         }
     }
     
     // 🎯 [关键修复] 检查等待结果
     if (enc_ctx == nullptr) {
         fprintf(stderr, "[encrypt_worker] ❌ 致命错误: 无法获取加密上下文\n");
         fprintf(stderr, "[encrypt_worker] ❌ 重试次数: %d/%d\n", retry_count, max_retry);
         fprintf(stderr, "[encrypt_worker] ❌ m_magic_encctx 内容:\n");
         
         for (const auto& pair : m_magic_encctx) {
             fprintf(stderr, "  - magic_sz=%zu -> ctx=%p\n", pair.first, pair.second);
         }
         
         fprintf(stderr, "[encrypt_worker] ❌ 期望的 magic_sz: %zu\n", x->magic_sz);
         fflush(stderr);
         
         // 设置初始化完成标志，但退出线程
         x->enc_init_done = true;
         return;
     }
     
     fprintf(stderr, "[encrypt_worker] ✅ 加密上下文获取成功: %p (重试: %d次)\n", enc_ctx, retry_count);
     fflush(stderr);
 
     // 🎯 [关键修复] 安全地获取元数据
     if (m_ctx_metadata.find(enc_ctx) == m_ctx_metadata.end()) {
         fprintf(stderr, "[encrypt_worker] ❌ 错误: 加密上下文没有对应的元数据\n");
         fflush(stderr);
         x->enc_init_done = true;
         return;
     }
     
     auto &enc_metadata = *m_ctx_metadata.at(enc_ctx);
     fprintf(stderr, "[encrypt_worker] ✅ 元数据获取成功\n");
     fflush(stderr);
 
     // 复制密钥和IV
     unsigned char encrypt_key[key_length];
     unsigned char encrypt_init_iv[iv_length];
     unsigned char iv[iv_length];
     
     memcpy(encrypt_key, enc_metadata.key, key_length);
     memcpy(encrypt_init_iv, enc_metadata.init_iv, iv_length);
     fprintf(stderr, "[encrypt_worker] ✅ 密钥和IV复制完成\n");
     fflush(stderr);
     
     // 分配假数据缓冲区
     auto fake_src = new unsigned char [1 << 20];
     fprintf(stderr, "[encrypt_worker] ✅ 假数据缓冲区分配完成\n");
     fflush(stderr);
 
     // 🎯 [关键修复] 设置初始化完成标志
     x->enc_init_done = true;
     fprintf(stderr, "[encrypt_worker] ✅ 初始化标记设置完成，进入主循环\n");
     fflush(stderr);
 
     // 创建本地OpenSSL上下文
     auto local_ctx = real_EVP_CIPHER_CTX_new();
     if (local_ctx == nullptr) {
         fprintf(stderr, "[encrypt_worker] ❌ 错误: OpenSSL上下文创建失败\n");
         fflush(stderr);
         return;
     }
     fprintf(stderr, "[encrypt_worker] ✅ 本地OpenSSL上下文创建完成: %p\n", local_ctx);
     fflush(stderr);
 
     std::deque<encryption_entry *> enc_entries;
     
     // 主工作循环
     fprintf(stderr, "[encrypt_worker] 🔄 进入主工作循环\n");
     fflush(stderr);
     
     while (g_running.load()) {
         // 等待任务
         while (x->enc_entries[local_id].empty()) {
             constexpr int polling_us = 1;
             auto start = std::chrono::system_clock::now();
             while (1) {
                 auto end = std::chrono::system_clock::now();
                 if (std::chrono::duration_cast<std::chrono::microseconds>(end - start).count() >= polling_us) {
                     break;
                 }
             }
         }
 
         // 获取任务
         if (!x->enc_entries[local_id].empty()) {
             std::lock_guard<std::mutex> lock(x->enc_lock[local_id]);
             
             enc_entries = x->enc_entries[local_id];
            //  fprintf(stderr, "[ENC] 收到 %zu 个任务\n", enc_entries.size());
             x->enc_entries[local_id].clear();
         }
 
         // 处理加密任务
         for (auto &entry : enc_entries) {
            // if (entry->size > 1024) {
            //     LOG_PIPE("[ENC] 🔥 Doing Encryption | Size: %zu | Buffer: %p", entry->size, entry->buffer);
            // }
             int updated_len;
             next_iv(encrypt_init_iv, iv, entry->iv_offset);
             real_EVP_EncryptInit_ex(local_ctx, EVP_aes_128_ctr(), 0, encrypt_key, iv);
            //  long extra_delay_us = 1000;
            //  busy_wait_us(extra_delay_us);
             assert(real_EVP_EncryptUpdate(local_ctx, entry->buffer, &updated_len, (unsigned char*)entry->src, entry->size) == 1);
             real_EVP_EncryptFinal_ex(local_ctx, entry->buffer + updated_len, &updated_len);
             real_EVP_CIPHER_CTX_ctrl(local_ctx, EVP_CTRL_GCM_GET_TAG, 16, entry->tag);
             entry->busy = false;
         }
         enc_entries.clear();
     }
     
     // 清理资源
     real_EVP_CIPHER_CTX_free(local_ctx);
     delete[] fake_src;
 }
 
  
   
 void commit_worker(void *entry)
 {
     auto x = (struct encrypt_worker_entry *)entry;
     auto worker_id = x->id;
     auto encrypt_workers_per_commit = x->encrypt_workers_per_commit;
     
     // 绑定核心
     // bind_core((encrypt_workers_per_commit + 1) * worker_id);
     
     aclrtStream stream;
     void *fake_src; 
 
     // 等待 Context
     while (g_main_ctx == nullptr) { usleep(1000); }
     assert(aclrtSetCurrentContext(g_main_ctx) == ACL_SUCCESS);
     assert(real_aclrtCreateStream(&stream) == ACL_SUCCESS); 
     assert(real_aclrtMallocHost(&fake_src, 1ul << 30) == ACL_SUCCESS); 
     
     // 预分配临时显存
     void* reusable_temp_in = nullptr;
     void* reusable_temp_out = nullptr;
     size_t temp_buffer_size = 32 * 1024 * 1024; // 32MB
     assert(real_aclrtMalloc(&reusable_temp_in, temp_buffer_size) == ACL_SUCCESS);
     assert(real_aclrtMalloc(&reusable_temp_out, temp_buffer_size) == ACL_SUCCESS);
 
     sleep(1); 
     x->commit_init_done = true;
 
     // 获取元数据
     while(m_magic_encctx.find(x->magic_sz) == m_magic_encctx.end()) usleep(100);
     auto enc_ctx = m_magic_encctx[x->magic_sz];
     auto &enc_metadata = *m_ctx_metadata.at(enc_ctx);
     enc_metadata.commit_worker_id = worker_id;
     x->predict_iv_offset = enc_metadata.cur_iv_offset + 1;
 
     // 任务队列
     std::deque<encryption_task> enc_tasks;
     std::deque<commit_task> commit_tasks;
     std::deque<commit_task> remain_commit_tasks;
     // 预测结果存储
     std::vector<std::tuple<const void*, size_t, void*, encryption_entry*>> predict_vec;
     
     // [新增] 垃圾回收队列
     std::deque<GCBatch> gc_queue;
 
     fprintf(stderr, "[Commit Worker] 🚀 启动成功 (Pipeline Mode)\n");
 
     while (g_running.load()) {
         
         // 1. 尝试清理垃圾 (非阻塞)
         process_garbage_collection(gc_queue);
 
         // 2. Polling 任务
         while (g_running.load() && x->commit_tasks.empty() && x->enc_tasks.empty() && commit_tasks.empty()) {
             // 在空闲等待时也可以顺便清理垃圾
             process_garbage_collection(gc_queue);
             
             constexpr int polling_us = 1;
             auto start = std::chrono::system_clock::now();
             while (1) {
                 auto end = std::chrono::system_clock::now();
                 if (std::chrono::duration_cast<std::chrono::microseconds>(end - start).count() >= polling_us) break;
             }
         }
         if (!g_running.load()) break;
 
         // 3. 获取任务 (加锁)
         if (!x->commit_tasks.empty() || !x->enc_tasks.empty()){
             std::lock_guard<std::mutex> lock(x->lock);
             for (auto &task : x->commit_tasks) commit_tasks.push_back(task);
             x->commit_tasks.clear();
             enc_tasks = x->enc_tasks;
             x->enc_tasks.clear();
         }
 
         // 4. 处理纯加密任务 (Predict Prefetch)
         // 这部分保持不变，负责把加密后的数据放进 predict_vec
         for (auto &task : enc_tasks) {
              auto count = task.size;
              auto base = (const char*)task.src;
              x->predict_iv_offset += task.iv_increment;
              size_t div = (count + encrypt_workers_per_commit - 1) / encrypt_workers_per_commit;
              div = (div + block_unit - 1) / block_unit * block_unit;
              auto blocks = (div + block_unit - 1) / block_unit;
              for (int i = 0; i < encrypt_workers_per_commit; i++) {
                  for (int j = 0; j < blocks; j++) {
                      auto chunk_size = std::min(count, block_unit);
                      void* cipher_buffer = new char[chunk_size]; // 这里分配了内存
                      auto enc_entry = new encryption_entry;
                      enc_entry->src = base;
                      enc_entry->buffer = (unsigned char*)cipher_buffer;
                      enc_entry->size = chunk_size;
                      enc_entry->busy = true;
                      enc_entry->iv_offset = x->predict_iv_offset;
                      predict_vec.push_back(std::make_tuple((const void*)base, chunk_size, cipher_buffer, enc_entry));
                      {
                          std::lock_guard<std::mutex> lock(x->enc_lock[0]);
                          x->enc_entries[0].push_back(enc_entry);
                      }
                      x->predict_iv_offset += (chunk_size + 15) / 16;
                      base += chunk_size;
                      count -= chunk_size;
                      if (count == 0) break;
                  }
                  if (count == 0) break;
              }
         }
         enc_tasks.clear();
 
         // 5. 处理提交任务 (Pipeline Commit)
         // 准备当前的 GC Batch
         GCBatch current_gc_batch;
         bool has_async_ops = false;
 
         for (auto &task : commit_tasks) {
             if (task.using_predict) {
                 auto count = task.enc_task.size;
                 auto base_src = (const char*)task.enc_task.src;
                 auto base_dst = (char*)task.dst;
                 
                 size_t div = (count + encrypt_workers_per_commit - 1) / encrypt_workers_per_commit;
                 div = (div + block_unit - 1) / block_unit * block_unit;
                 auto blocks = (div + block_unit - 1) / block_unit;
 
                 for (int i = 0; i < encrypt_workers_per_commit; i++) {
                     for (int j = 0; j < blocks; j++) {
                         auto chunk_size = std::min(count, block_unit);
                         void* cipher_buffer = nullptr;
                         encryption_entry* entry_ptr = nullptr;
                         
                         // 在 predict_vec 中查找对应的加密块
                         auto it = predict_vec.begin();
                         bool found = false;
                         for (; it != predict_vec.end(); ++it) {
                             if (std::get<0>(*it) == base_src && std::get<1>(*it) == chunk_size) {
                                 cipher_buffer = std::get<2>(*it);
                                 entry_ptr = std::get<3>(*it);
                                 found = true;
                                 break;
                             }
                         }
 
                         if (found) {
                             // 等待 CPU 加密完成 (自旋等待)
                             while (entry_ptr->busy) { 
                                 // 这里可以插入 yield，防止死锁
                                 std::this_thread::yield(); 
                             } 
                             enc_metadata.cur_iv_offset++;
 
                             if (chunk_size < 0) {
                                 // 小数据走同步拷贝 (Safe)
                                 real_aclrtMemcpyAsync(base_dst, chunk_size, base_src, chunk_size, ACL_MEMCPY_HOST_TO_DEVICE, stream);
                                 // 小数据立即释放是安全的，因为HostToDevice默认会有一定同步行为，
                                 // 或者由于数据太小，我们在 GC 统一处理也没问题。
                                 // 为了一致性，放入 GC。
                             } 
                             else {
                                 // 1. H2D (异步)
                                 // 注意：cipher_buffer 不能立即 delete
                                 real_aclrtMemcpyAsync(reusable_temp_in, chunk_size, cipher_buffer, chunk_size, ACL_MEMCPY_HOST_TO_DEVICE, stream);
                                 
                                 // 2. Kernel (异步)
                                 if (g_dev_key && g_dev_iv) {
                                     uint32_t blockDim = 8; 
                                     uint32_t mode = 1; // CTR 解密
                                     printf("算子调用，大小为%d\n",(int)chunk_size);
                                     add_custom_do(
                                         blockDim,
                                         stream,
                                         (uint8_t*)reusable_temp_in,
                                         (uint8_t*)g_dev_key,
                                         (uint8_t*)g_dev_iv,
                                         (uint8_t*)reusable_temp_out,
                                         (uint32_t)chunk_size,
                                         mode
                                     );
                                 }
 
                                 // 3. D2D (异步)
                                 real_aclrtMemcpyAsync(base_dst, chunk_size, reusable_temp_out, chunk_size, ACL_MEMCPY_DEVICE_TO_DEVICE, stream);
 
                                 // 4. 处理非 16 字节对齐的尾部
                                 size_t remainder = chunk_size % 16;
                                 if (remainder > 0) {
                                     size_t aligned_len = chunk_size - remainder;
                                     real_aclrtMemcpyAsync(base_dst + aligned_len, remainder, base_src + aligned_len, remainder, ACL_MEMCPY_HOST_TO_DEVICE, stream);
                                 }
                             }
                             
                             // [关键修改]
                             // 不再立即 delete，而是加入当前 Batch
                             current_gc_batch.buffers.push_back(cipher_buffer);
                             current_gc_batch.entries.push_back(entry_ptr);
                             has_async_ops = true;
 
                             // 从搜索列表移除，防止重复
                             predict_vec.erase(it);
                         }
                         
                         // [关键修改] 移除了 aclrtSynchronizeStream(stream);
                         // 这实现了完全的流水线：CPU 不断塞任务，NPU 后台排队执行。
 
                         base_src += chunk_size;
                         base_dst += chunk_size;
                         count -= chunk_size;
                         if (count == 0) break;
                     }
                     if (count == 0) break;
                 }
             } else {
                 // Fallback (非预测路径)
                 auto chunk_size = task.enc_task.size;
                 assert(real_aclrtMemcpyAsync(task.dst, chunk_size, task.enc_task.src, chunk_size, ACL_MEMCPY_HOST_TO_DEVICE, stream) == ACL_SUCCESS);
                 // Fallback 路径为了安全可以保留同步，或者也可以优化
                //  aclrtSynchronizeStream(stream); 
             }


             if (task.sync_event != nullptr) {
                // 在 worker 的流 (stream) 上记录 event
                // 意义：当 NPU 执行完上面的 H2D 之后，触发这个 event
                assert(real_aclrtRecordEvent(task.sync_event, stream) == ACL_SUCCESS);
                if (task.submitted_flag != nullptr) {
                    task.submitted_flag->store(true);
                }
            }
         }
 
         // 6. 提交垃圾回收 Event
         if (has_async_ops) {
             aclrtEvent event;
             // 创建 Event (这里可能有点开销，为了性能可以考虑 Event 池，但目前先这样)
             aclrtCreateEvent(&event);
             // 在 Stream 中记录 Event。当 NPU 执行到这里时，意味着前面的 H2D/Kernel 都做完了。
             aclrtRecordEvent(event, stream);
             
             current_gc_batch.event = event;
             gc_queue.push_back(current_gc_batch);
         }
 
         commit_tasks = remain_commit_tasks;
         remain_commit_tasks.clear();
         x->commit = false;
     }
     
     // 退出前清理
     process_garbage_collection(gc_queue);
     // 如果还有剩余的，强制同步并释放
     if (!gc_queue.empty()) {
         aclrtSynchronizeStream(stream);
         for (auto& batch : gc_queue) {
             for (void* buf : batch.buffers) delete[] (char*)buf;
             for (auto* entry : batch.entries) delete entry;
             aclrtDestroyEvent(batch.event);
         }
     }
 
     real_aclrtFree(reusable_temp_in);
     real_aclrtFree(reusable_temp_out);
     real_aclrtFree(fake_src); // 修正: 使用对应的 FreeHost
 }
 
 /*
  * ==========================================================================
  * 函数: decrypt_worker (无需修改)
  * ==========================================================================
  */
 void decrypt_worker(void *entry)
 {
     // (此函数 100% 保持不变，它与 CUDA/ACL 无关)
     auto x = (struct decrypt_worker_entry *)entry;
     auto worker_id = x->id;
     bind_core(2 * worker_id + 4 + 1);
     
     std::deque<decryption_entry *> dec_entries;
     unsigned char decrypt_key[key_length];
 
     auto dec_ctx = m_magic_decctx[x->magic_sz];
     auto &dec_metadata = *m_ctx_dmetadata.at(dec_ctx);
 
     memcpy(decrypt_key, dec_metadata.key, key_length);
 
     x->dec_init_done = true;
     auto local_ctx = real_EVP_CIPHER_CTX_new();
     while (1) {
         while (x->dec_entries.empty()) {
             // Polling 1 us
             constexpr int polling_us = 1;
             auto start = std::chrono::system_clock::now();
             while (1) {
                 auto end = std::chrono::system_clock::now();
                 if (std::chrono::duration_cast<std::chrono::microseconds>(end - start).count() >= polling_us) {
                     break;
                 }
             }
         }
 
         if (!x->dec_entries.empty()){
             std::lock_guard<std::mutex> lock(x->dec_lock);
             dec_entries = x->dec_entries;
             x->dec_entries.clear();
         }
 
         for (auto &entry : dec_entries) {
             int updated_len;
             real_EVP_DecryptInit_ex(local_ctx, EVP_aes_128_ctr(), 0, decrypt_key, entry->iv);
             real_EVP_CIPHER_CTX_ctrl(local_ctx, EVP_CTRL_GCM_SET_TAG, 16, entry->tag);
             assert(real_EVP_DecryptUpdate(local_ctx, (unsigned char *)entry->dst, &updated_len, entry->buffer, entry->size) == 1);
             real_EVP_DecryptFinal_ex(local_ctx, (unsigned char *)entry->dst + updated_len, &updated_len);
             assert(updated_len == 0);
             entry->busy = false;
         }
         dec_entries.clear();
     }
 }
 
 /*
  * ==========================================================================
  * 函数: decrypt_manager (已迁移到 ACL)
  * ==========================================================================
  */
 void decrypt_manager(void *entry)
 {
     auto x = (struct decrypt_worker_entry *)entry;
     auto worker_id = x->id;
     bind_core(2 * worker_id + 4);
 
     // [!!! 关键修改: CUDA -> ACL !!!]
     // CUcontext cuda_ctx;  <-- [已移除]
     // CUdevice dev;         <-- [已移除]
     // cudaStream_t stream;  <-- [已移除]
     
     aclrtContext acl_ctx;    // <-- [新添加]
     aclrtStream stream;      // <-- [新添加]
 
     void *fake_src, *fake_dst; // fake_src 在您的代码中未被使用
 
     // [修改] 替换 CUDA Driver API 为 ACL Runtime API (直接调用)
    //  assert(aclInit(nullptr) == ACL_SUCCESS);
     assert(aclrtSetDevice(0) == ACL_SUCCESS);
     assert(aclrtCreateContext(&acl_ctx, 0) == ACL_SUCCESS);
     assert(aclrtSetCurrentContext(acl_ctx) == ACL_SUCCESS);
     
     // [修改] 替换 CUDA Runtime Hooks 为 ACL Runtime Hooks
     assert(real_aclrtCreateStream(&stream) == ACL_SUCCESS);
     assert(real_aclrtMallocHost(&fake_dst, 1ul << 20) == ACL_SUCCESS);
     sleep(1);
     void *dev_buffer;
     real_aclrtMalloc(&dev_buffer, 1ul << 20);
     
     // [修改] 调用 real_aclrt... 并添加 destMax 参数
     real_aclrtMemcpyAsync(fake_dst, x->magic_sz, dev_buffer, x->magic_sz, ACL_MEMCPY_DEVICE_TO_HOST, stream);
     real_aclrtMemcpyAsync(fake_dst, x->magic_sz, dev_buffer, x->magic_sz, ACL_MEMCPY_DEVICE_TO_HOST, stream);
     real_aclrtSynchronizeDevice();
     // [!!! 修改结束 !!!]
     
     x->commit_init_done = true;
 
     std::deque<decryption_task> dec_tasks;
     std::deque<decryption_entry *> dec_entries;
     std::deque<encryption_task> enc_tasks;
     auto dec_ctx = m_magic_decctx[x->magic_sz];
     auto &dec_metadata = *m_ctx_dmetadata.at(dec_ctx);
     dec_metadata.remain = 0;
 
     while (1) {
         // (内部轮询逻辑保持不变)
         while (x->dec_tasks.empty() && dec_entries.empty()) {
             // Polling 1 us
             constexpr int polling_us = 1;
             auto start = std::chrono::system_clock::now();
             while (1) {
                 auto end = std::chrono::system_clock::now();
                 if (std::chrono::duration_cast<std::chrono::microseconds>(end - start).count() >= polling_us) {
                     break;
                 }
             }
         }
 
         // (内部任务获取逻辑保持不变)
         if (!x->dec_tasks.empty()){
             std::lock_guard<std::mutex> lock(x->lock);
             dec_tasks = x->dec_tasks;
             x->dec_tasks.clear();
         }
 
         for (auto &task : dec_tasks) {
             dec_metadata.remain = 0;
             
             // [!!! 关键修改: CUDA -> ACL !!!]
             // (添加 destMax = task.size)
             assert(real_aclrtMemcpyAsync(dev_buffer, task.size, task.src, task.size, ACL_MEMCPY_DEVICE_TO_DEVICE, stream) == ACL_SUCCESS); // [修改]
             assert(real_aclrtMemcpyAsync(fake_dst, task.size, dev_buffer, task.size, ACL_MEMCPY_DEVICE_TO_HOST, stream) == ACL_SUCCESS); // [修改]
             // [!!! 修改结束 !!!]
 
             auto entry = new decryption_entry;
             entry->buffer = dec_metadata.cur_buffer;
             memcpy(entry->tag, dec_metadata.cur_tag, 16);
             memcpy(entry->iv, dec_metadata.cur_iv, iv_length);
             entry->size = task.size;
             entry->busy = true;
             entry->dst = task.dst;
             entry->busy = true;
             {
                 std::lock_guard<std::mutex> lock(x->dec_lock);
                 x->dec_entries.push_back(entry);
                 dec_entries.push_back(entry);
             }
         }
 
 
         // (内部 "Send to encryption" 逻辑保持不变)
         bool batch_first = true;
         for (auto iter = dec_entries.begin(); iter != dec_entries.end();) {
             auto &entry = *iter;
             if (entry->busy) {
                 ++iter;
                 continue;
             }
             encryption_task task;
             task.src = entry->dst;
             task.size = entry->size;
             task.iv_increment = batch_first ? dec_entries.size() : 0;
             batch_first = false;
             enc_tasks.push_back(task);
 
             // Remove buffer
             dec_metadata.allocator.free((unsigned char *)entry->buffer);
             delete entry;
             iter = dec_entries.erase(iter);
         }
 
         if (!enc_tasks.empty()) {
             auto worker_entry = x->enc_worker_entry;
             std::lock_guard<std::mutex> lock(worker_entry->lock);
             for (auto &task : enc_tasks) {
                 worker_entry->enc_tasks.push_back(task);
             }
         }
 
         dec_tasks.clear();
         enc_tasks.clear();
     }
 }