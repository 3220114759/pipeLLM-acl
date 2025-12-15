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
MODEL_PATH = "/data/lzy/models/Qwen3-14B/Qwen/Qwen3-14B"
# MODEL_PATH = "/data/lzy/models/Qwen2.5-72B/Qwen/Qwen2.5-72B"
PROMPT = "介绍一下浙江大学"
MAGIC_SZ = 0xabcde  # 必须与 C++ 保持一致



# -----------------------------------------------------------------
# 主程序
# -----------------------------------------------------------------
if __name__ == "__main__":
    
    print("====== [vLLM 推理阶段] 开始 ======")
    
    # 2. 配置采样参数
    sampling_params = SamplingParams(temperature=0.7, top_p=0.8, max_tokens=100)

    # 3. 初始化 vLLM
    print(f"--- 正在初始化 vLLM (模型: {MODEL_PATH}) ---")
    llm = LLM(
        model=MODEL_PATH,
        trust_remote_code=True,
        tensor_parallel_size=4,
        dtype="float16",
        gpu_memory_utilization=0.9,
        enforce_eager=True
    )
    
    print(f"--- 正在加载 Tokenizer ---")
    tokenizer = AutoTokenizer.from_pretrained(MODEL_PATH, trust_remote_code=True)
    
    print(f"--- vLLM 初始化完毕，开始推理 ---")

    # 4. 执行推理并计时
    prompts = [PROMPT]
    

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
    
    import gc

    # 1. 删除 llm 对象，触发析构函数 (__del__)
    if 'llm' in globals():
        del llm

    # 2. 强制进行垃圾回收，确保子进程被依序回收
    gc.collect()

    print("[Info] LLM Engine shutdown successfully.")