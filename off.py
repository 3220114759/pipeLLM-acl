import sys
import os
import time
import ctypes
import ctypes.util

# 1. 优先导入 torch_npu
import torch
import torch_npu 

# 2. 导入 vLLM 及相关工具
from vllm import LLM, SamplingParams
from transformers import AutoTokenizer # 新增：用于精确统计Token数

# -----------------------------------------------------------------
# 全局配置
# -----------------------------------------------------------------
MODEL_PATH = "/data/lzy/models/Qwen2-0.5B/Qwen/Qwen2-0.5B"
PROMPT = "介绍一下浙江大学"
MAGIC_SZ = 0xabcde  # 必须与 C++ 保持一致

# -----------------------------------------------------------------
# 握手函数 (完全保持原样)
# -----------------------------------------------------------------
def perform_openssl_handshake():
    print("====== [握手阶段] 开始 ======")
    
    # 1. 查找 libcrypto
    libcrypto_path = ctypes.util.find_library('crypto')
    if not libcrypto_path:
        paths = [
            "/usr/lib/x86_64-linux-gnu/libcrypto.so.1.1", 
            "/home/lzy/miniconda3/envs/npu_env/lib/libcrypto.so.1.1",
            "/usr/lib64/libcrypto.so.1.1"
        ]
        for p in paths:
            if os.path.exists(p):
                libcrypto_path = p
                break
    
    if not libcrypto_path:
        print("错误: 找不到 libcrypto")
        sys.exit(1)
        
    print(f"--- [握手] 加载真实 OpenSSL: {libcrypto_path} ---")
    real_lib = ctypes.CDLL(libcrypto_path)

    # 2. 加载钩子库
    acl_path = os.path.abspath("/data/lzy/pipellm_acl/build/lib/pipellm_acl.so")
    print(f"--- [握手] 加载钩子库: {acl_path} ---")
    hook_lib = ctypes.CDLL(acl_path)

    # 函数原型定义...
    real_lib.EVP_CIPHER_CTX_new.restype = ctypes.c_void_p
    real_lib.EVP_CIPHER_CTX_free.argtypes = [ctypes.c_void_p]
    real_lib.EVP_aes_256_gcm.restype = ctypes.c_void_p

    hook_lib.EVP_DecryptUpdate.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_int]
    hook_lib.EVP_DecryptUpdate.restype = ctypes.c_int
    hook_lib.EVP_DecryptInit_ex.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p]
    hook_lib.EVP_DecryptInit_ex.restype = ctypes.c_int
    hook_lib.EVP_CIPHER_CTX_ctrl.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int, ctypes.c_void_p]
    hook_lib.EVP_CIPHER_CTX_ctrl.restype = ctypes.c_int

    hook_lib.EVP_EncryptInit_ex.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p]
    hook_lib.EVP_EncryptInit_ex.restype = ctypes.c_int
    hook_lib.EVP_EncryptUpdate.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_int]
    hook_lib.EVP_EncryptUpdate.restype = ctypes.c_int

    # 执行握手...
    cipher = real_lib.EVP_aes_256_gcm()
    dummy_buf = ctypes.create_string_buffer(MAGIC_SZ)
    outl = ctypes.c_int(0)
    dummy_key = b'\x00'*32
    dummy_iv = b'\x00'*12
    dummy_tag = b'\x00'*16

    try:
        print("\n>>> 正在执行【解密】握手...")
        ctx_dec = real_lib.EVP_CIPHER_CTX_new()
        hook_lib.EVP_DecryptUpdate(ctx_dec, dummy_buf, ctypes.byref(outl), dummy_buf, MAGIC_SZ)
        hook_lib.EVP_DecryptInit_ex(ctx_dec, cipher, None, None, None)
        hook_lib.EVP_DecryptInit_ex(ctx_dec, None, None, dummy_key, dummy_iv)
        hook_lib.EVP_CIPHER_CTX_ctrl(ctx_dec, 0x11, 16, dummy_tag)
        print("✅ 解密握手完成")

        print("\n>>> 正在执行【加密】握手...")
        ctx_enc = real_lib.EVP_CIPHER_CTX_new()
        hook_lib.EVP_EncryptInit_ex(ctx_enc, cipher, None, dummy_key, dummy_iv)
        hook_lib.EVP_EncryptUpdate(ctx_enc, dummy_buf, ctypes.byref(outl), dummy_buf, MAGIC_SZ)
        print("✅ 加密握手完成")
        print("\n====== [握手阶段] 全部成功 ======\n")

    except Exception as e:
        print(f"ERROR: {e}")
        sys.exit(1)

# -----------------------------------------------------------------
# 主程序
# -----------------------------------------------------------------
if __name__ == "__main__":
    
    # 1. 握手
    # perform_openssl_handshake()
    
    print("====== [vLLM 推理阶段] 开始 ======")
    
    # 2. 配置采样参数
    sampling_params = SamplingParams(temperature=0.7, top_p=0.8, max_tokens=100)

    # 3. 初始化 vLLM
    print(f"--- 正在初始化 vLLM (模型: {MODEL_PATH}) ---")
    llm = LLM(
        model=MODEL_PATH,
        trust_remote_code=True,
        tensor_parallel_size=1,
        dtype="float16",
        gpu_memory_utilization=0.99,
        enforce_eager=True
    )
    
    # 加载 Tokenizer 以便于后续统计 Token 数 (如果 vLLM 内部有，也可以直接用)
    print(f"--- 正在加载 Tokenizer ---")
    tokenizer = AutoTokenizer.from_pretrained(MODEL_PATH, trust_remote_code=True)
    
    print(f"--- vLLM 初始化完毕，开始推理 ---")

    # 4. 执行推理并计时
    prompts = [PROMPT]
    
    # 预热一次 (可选，防止第一次加载开销影响数据)
    # print("--- Pre-warming ---")
    # llm.generate(["Hello"], sampling_params)

    print(f"--- 开始生成 ---")
    start_time = time.time()
    outputs = llm.generate(prompts, sampling_params)
    end_time = time.time()
    
    total_latency = end_time - start_time

    # 5. 计算与输出详细指标
    print("\n" + "="*60)
    print(f"{'性能指标报告':^60}")
    print("="*60)

    total_input_tokens = 0
    total_output_tokens = 0
    
    for i, output in enumerate(outputs):
        prompt = output.prompt
        generated_text = output.outputs[0].text
        
        # 统计 Token 数量
        # vLLM output 对象通常包含 token_ids，可以直接使用，无需再次 tokenize
        # 如果 output.prompt_token_ids 存在则直接用，否则用 tokenizer
        if hasattr(output, 'prompt_token_ids') and output.prompt_token_ids:
            n_input_tokens = len(output.prompt_token_ids)
        else:
            n_input_tokens = len(tokenizer.encode(prompt))
            
        if hasattr(output.outputs[0], 'token_ids') and output.outputs[0].token_ids:
            n_output_tokens = len(output.outputs[0].token_ids)
        else:
            n_output_tokens = len(tokenizer.encode(generated_text))

        total_input_tokens += n_input_tokens
        total_output_tokens += n_output_tokens

        print(f"--- 请求 {i+1} ---")
        print(f"Prompt: {prompt!r}")
        print(f"Output: {generated_text!r}")
        print(f"Tokens: Input={n_input_tokens}, Output={n_output_tokens}")
        print("-" * 30)

    # 计算吞吐量
    total_tokens = total_input_tokens + total_output_tokens
    throughput = total_tokens / total_latency
    output_throughput = total_output_tokens / total_latency # 生成速度

    print(f"\n{'统计结果':^20}")
    print(f"总耗时 (Latency)    : {total_latency:.4f} s")
    print(f"输入 Tokens 总数    : {total_input_tokens}")
    print(f"输出 Tokens 总数    : {total_output_tokens}")
    print(f"总 Tokens 数        : {total_tokens}")
    print(f"总吞吐量 (Throughput): {throughput:.2f} tokens/s")
    print(f"生成吞吐量 (Gen Speed): {output_throughput:.2f} tokens/s")
    print("="*60)