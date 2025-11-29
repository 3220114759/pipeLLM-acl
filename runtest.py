import os
# 完全禁用多进程相关功能
os.environ["TOKENIZERS_PARALLELISM"] = "false"
os.environ["OMP_NUM_THREADS"] = "1"
os.environ["MKL_NUM_THREADS"] = "1"
os.environ["OPENBLAS_NUM_THREADS"] = "1"
os.environ["NUMEXPR_NUM_THREADS"] = "1"

import torch
import torch_npu
from transformers import AutoModelForCausalLM, AutoTokenizer
import time
import ctypes

# --- 配置 ---
MODEL_PATH = "/data/lzy/models/Qwen2-7B/Qwen/Qwen2-7B"
DEVICE = "npu:0"
MAX_NEW_TOKENS = 50 
REPEAT_TIMES = 3   
PROMPT = "你好，请介绍一下你自己。"

def trigger_pipellm_handshake(device):
    print("="*60)
    print("🤝 正在执行 PipeLLM 握手程序...")
    print("   1. 触发 NPU (填充 s_magic)...")
    try:
        trigger_size = (0x4c000) + 1024 
        dummy_tensor = torch.ones(trigger_size, dtype=torch.uint8).to(device)
        torch.npu.synchronize()
        del dummy_tensor
        print("   ✅ NPU Hook 已触发 (pipellm.cpp)。")
    except Exception as e:
        print(f"   ❌ NPU 触发失败: {e}")
        raise e

    print("   2. 触发 OpenSSL (填充 metadata maps)...")
    try:
        libcrypto = ctypes.CDLL("libcrypto.so.1.1") 
    except OSError:
        try:
            libcrypto = ctypes.CDLL("libcrypto.so.3")
        except OSError:
            print("   ❌ 错误: 找不到 'libcrypto.so.1.1' 或 'libcrypto.so.3'。")
            raise

    magic_sz_enc = 0xabcde
    EVP_CIPHER_CTX_p = ctypes.c_void_p
    c_int = ctypes.c_int
    c_void_p = ctypes.c_void_p
    c_uchar_p = ctypes.c_void_p 

    EVP_CIPHER_CTX_new = libcrypto.EVP_CIPHER_CTX_new
    EVP_CIPHER_CTX_new.restype = EVP_CIPHER_CTX_p
    EVP_aes_256_gcm = libcrypto.EVP_aes_256_gcm
    EVP_aes_256_gcm.restype = c_void_p
    EVP_EncryptInit_ex = libcrypto.EVP_EncryptInit_ex
    EVP_EncryptInit_ex.argtypes = [EVP_CIPHER_CTX_p, c_void_p, c_void_p, c_uchar_p, c_uchar_p]
    EVP_EncryptInit_ex.restype = c_int
    EVP_EncryptUpdate = libcrypto.EVP_EncryptUpdate
    EVP_EncryptUpdate.argtypes = [EVP_CIPHER_CTX_p, c_uchar_p, ctypes.POINTER(c_int), c_uchar_p, c_int]
    EVP_EncryptUpdate.restype = c_int
    EVP_CIPHER_CTX_free = libcrypto.EVP_CIPHER_CTX_free
    EVP_CIPHER_CTX_free.argtypes = [EVP_CIPHER_CTX_p]

    ctx = EVP_CIPHER_CTX_new()
    key = (ctypes.c_ubyte * 32)()
    iv = (ctypes.c_ubyte * 16)()
    in_buffer = (ctypes.c_ubyte * magic_sz_enc)()
    out_buffer = (ctypes.c_ubyte * (magic_sz_enc + 64))()
    out_len = ctypes.c_int()

    EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), None, key, iv)
    EVP_EncryptUpdate(ctx, out_buffer, ctypes.byref(out_len), in_buffer, magic_sz_enc)
    EVP_CIPHER_CTX_free(ctx)
    
    print("   ✅ OpenSSL Hook 已触发 (openssl.cpp)。")
    print("🤝 握手完成，pipellm_acl.so 已准备就绪。")
    print("="*60)

def run_inference(model, tokenizer, prompt, device, max_new_tokens):
    input_ids = tokenizer(prompt, return_tensors="pt").input_ids.to(device)
    
    with torch.no_grad():
        outputs = model.generate(
            input_ids,
            max_new_tokens=max_new_tokens,
            do_sample=False
        )
    
    result_text = tokenizer.decode(outputs[0], skip_special_tokens=True)
    return result_text

def main():
    print("="*60)
    print("📊 大模型NPU推理性能测试（修复版）")
    print("="*60)

    if "LD_PRELOAD" not in os.environ or "pipellm_acl.so" not in os.environ["LD_PRELOAD"]:
        print("未检测到 'LD_PRELOAD=./pipellm_acl.so'，将作为纯净基准测试运行。")
    else:
        print("✅ 检测到 LD_PRELOAD，将激活 pipeLLM.so 钩子。")

    try:
        # 1. 初始化NPU环境
        print(f"\n1. 初始化NPU设备...")
        torch.npu.set_device(DEVICE)
        
        # 2. 执行握手
        if "LD_PRELOAD" in os.environ and "pipellm_acl.so" in os.environ["LD_PRELOAD"]:
            trigger_pipellm_handshake(DEVICE)
        else:
            print("\n未加载 pipeLLM.so，跳过握手程序。")

        # 3. 加载Tokenizer和模型
        print(f"\n2. 加载Tokenizer...")
        tokenizer = AutoTokenizer.from_pretrained(MODEL_PATH, trust_remote_code=True)
        print(f"✅ Tokenizer加载成功")

        print(f"\n3. 加载模型到NPU...")
        model_load_start = time.perf_counter()
        model = AutoModelForCausalLM.from_pretrained(
            MODEL_PATH,
            torch_dtype=torch.float16,
            trust_remote_code=True
        ).to(DEVICE).eval()
        model_load_time = time.perf_counter() - model_load_start
        print(f"✅ 模型加载完成 (耗时: {model_load_time:.2f} 秒)")

        # 4. 多轮推理
        print(f"\n4. 开始多轮推理（共 {REPEAT_TIMES} 轮）...")
        
        total_infer_time = 0.0
        all_results = []

        for round in range(REPEAT_TIMES):
            print(f"\n   🔄 轮次 {round+1}/{REPEAT_TIMES}...")
            infer_start = time.perf_counter()
            
            result = run_inference(
                model=model,
                tokenizer=tokenizer,
                prompt=PROMPT,
                device=DEVICE,
                max_new_tokens=MAX_NEW_TOKENS
            )
            
            infer_time = time.perf_counter() - infer_start
            total_infer_time += infer_time
            all_results.append(result)
            print(f"   ✅ 轮次 {round+1} 完成，耗时: {infer_time:.2f}秒")
            print(f"   结果: {result}")

        print("\n" + "="*60)
        print("🎉 多轮推理完成")
        print("="*60)
        print(f"   总推理时间: {total_infer_time:.2f} 秒")
        print(f"   平均每轮时间: {total_infer_time/REPEAT_TIMES:.2f} 秒")
        print("="*60)

    except Exception as e:
        print(f"\n❌ 推理过程发生致命错误：{e}")
        import traceback
        traceback.print_exc()

if __name__ == "__main__":
    # 完全不使用多进程
    main()