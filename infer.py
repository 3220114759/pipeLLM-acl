import sys
import os
os.environ["ASCEND_RT_VISIBLE_DEVICES"] = "7"

import time
import ctypes
import ctypes.util


import torch
import torch_npu 

if 'vllm' in sys.modules:
    del sys.modules['vllm']
from vllm import LLM, SamplingParams
from vllm.config import KVTransferConfig 
from transformers import AutoTokenizer 


MODEL_PATH = "/data/lzy/models/Qwen3-14B/Qwen/Qwen3-14B"
# MODEL_PATH = "/data/lzy/models/Qwen2-7B-crypto"
# MODEL_PATH = "/data/lzy/models/Qwen2-7B"
PROMPT = "介绍一下浙江大学"
MAGIC_SZ = 0xabcde  



if __name__ == "__main__":
    
    print("====== [vLLM 推理阶段] 开始 ======")
    
    sampling_params = SamplingParams(temperature=0.7, top_p=0.8, max_tokens=100)

    print(f"--- 正在初始化 vLLM (模型: {MODEL_PATH}) ---")
    
    kv_transfer_config = KVTransferConfig(  
        kv_connector="OffloadingConnector",  
        kv_role="kv_both",  
        kv_connector_extra_config={  
            "num_cpu_blocks": 1000,       
            "block_size": 128,            
            "spec_name": "NPUOffloadingSpec",  
            "spec_module_path": "vllm_ascend.kv_offload.npu",  
        },  
    )

    llm = LLM(
        model=MODEL_PATH,
        trust_remote_code=True,
        tensor_parallel_size=1,
        dtype="float16",
        gpu_memory_utilization=0.9,
        enforce_eager=True,
        kv_transfer_config=kv_transfer_config 
    )
    
    print(f"--- 正在加载 Tokenizer ---")
    tokenizer = AutoTokenizer.from_pretrained(MODEL_PATH, trust_remote_code=True)
    
    print(f"--- vLLM 初始化完毕，开始推理 ---")

    prompts = [PROMPT]
    

    print(f"--- 开始生成 ---")
    start_time = time.time()
    outputs = llm.generate(prompts, sampling_params)
    end_time = time.time()
    
    total_latency = end_time - start_time

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