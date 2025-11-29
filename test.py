import sys
import os
import time
import ctypes
import ctypes.util
import torch
import torch_npu 
from vllm import LLM, SamplingParams
from transformers import AutoTokenizer

# -----------------------------------------------------------------
# 全局配置 (压力测试版)
# -----------------------------------------------------------------
MODEL_PATH = "/data/lzy/models/Qwen2-0.5B/Qwen/Qwen2-0.5B"
# 增加一个长文本 Prompt (约 500-1000 tokens)
LONG_PROMPT = """
浙江大学（Zhejiang University），简称“浙大”，位于浙江省杭州市，是中华人民共和国教育部直属的综合性全国重点大学，位列国家“双一流”、“211工程”、“985工程”。
学校的前身求是书院创立于1897年，为中国人自己最早创办的新式高等学校之一。1928年定名为国立浙江大学。抗战期间，浙大举校西迁，在贵州遵义、湄潭等地办学七年，1946年秋回迁杭州。
1952年全国高等学校院系调整时，浙江大学部分系科转入中国科学院和其他高校，主体部分在杭州重组为若干所院校，后分别发展为原浙江大学、杭州大学、浙江农业大学和浙江医科大学。
1998年，同根同源的四校实现合并，组建了新的浙江大学。
请详细阐述浙江大学的历史沿革、学科建设、科研成就以及在国际上的学术地位，并结合当前的高等教育发展趋势，分析浙江大学未来的发展机遇与挑战。
另外，请列举浙江大学的几个著名校区及其特色，包括但不限于紫金港校区、玉泉校区、西溪校区等。
""" * 5  # 重复 5 次，构造超长 Prompt

# 模拟并发请求 (Batch Size = 8 或 16)
BATCH_SIZE = 8 
PROMPTS = [LONG_PROMPT] * BATCH_SIZE 

MAGIC_SZ = 0xabcde

# -----------------------------------------------------------------
# 握手函数 (如果需要预注入 Context，请取消注释)
# -----------------------------------------------------------------
# ... (perform_openssl_handshake 代码与之前相同，此处省略以节省篇幅) ...

# -----------------------------------------------------------------
# 主程序
# -----------------------------------------------------------------
if __name__ == "__main__":
    
    print("====== [vLLM 压力测试] 开始 ======")
    print(f"Batch Size: {BATCH_SIZE}")
    print(f"Prompt Length Multiplier: 5x")

    # 2. 配置采样参数 (增加生成长度)
    # ignore_eos=True 强制模型生成直到 max_tokens，不提前结束
    sampling_params = SamplingParams(
        temperature=0.7, 
        top_p=0.8, 
        max_tokens=512,  # 增加输出长度
        ignore_eos=True  
    )

    # 3. 初始化 vLLM
    print(f"--- 正在初始化 vLLM (模型: {MODEL_PATH}) ---")
    # 适当增加 gpu_memory_utilization 以容纳更多 KV Cache
    llm = LLM(
        model=MODEL_PATH,
        trust_remote_code=True,
        tensor_parallel_size=1,
        dtype="float16",
        gpu_memory_utilization=0.95, 
        enforce_eager=True,
        max_num_batched_tokens=8192, # 允许更大的 Batch
        max_model_len=8192
    )
    
    tokenizer = AutoTokenizer.from_pretrained(MODEL_PATH, trust_remote_code=True)
    print(f"--- vLLM 初始化完毕 ---")

    # 4. 预热 (Warmup)
    print("--- 正在预热 (Warmup) ---")
    llm.generate(["Warmup query"], SamplingParams(max_tokens=10))
    torch.npu.synchronize() # 确保预热完成
    print("--- 预热完成 ---")

    # 5. 执行压力测试
    print(f"\n--- 开始压力测试 (Batch={BATCH_SIZE}) ---")
    start_time = time.time()
    
    outputs = llm.generate(PROMPTS, sampling_params)
    
    torch.npu.synchronize() # 等待所有计算完成
    end_time = time.time()
    
    total_latency = end_time - start_time

    # 6. 计算详细指标
    total_input_tokens = 0
    total_output_tokens = 0
    
    for output in outputs:
        # 统计 Input Tokens
        if hasattr(output, 'prompt_token_ids') and output.prompt_token_ids:
            n_in = len(output.prompt_token_ids)
        else:
            n_in = len(tokenizer.encode(output.prompt))
            
        # 统计 Output Tokens
        if hasattr(output.outputs[0], 'token_ids') and output.outputs[0].token_ids:
            n_out = len(output.outputs[0].token_ids)
        else:
            n_out = len(tokenizer.encode(output.outputs[0].text))

        total_input_tokens += n_in
        total_output_tokens += n_out

    # 计算吞吐量
    total_tokens = total_input_tokens + total_output_tokens
    throughput = total_tokens / total_latency
    output_throughput = total_output_tokens / total_latency 

    print("\n" + "="*60)
    print(f"{'压力测试报告':^60}")
    print("="*60)
    print(f"并发请求数 (Batch Size) : {BATCH_SIZE}")
    print(f"总耗时 (Latency)        : {total_latency:.4f} s")
    print(f"平均每条请求延迟        : {total_latency / BATCH_SIZE:.4f} s")
    print("-" * 60)
    print(f"输入 Tokens 总数        : {total_input_tokens}")
    print(f"输出 Tokens 总数        : {total_output_tokens}")
    print(f"总 Tokens 数            : {total_tokens}")
    print("-" * 60)
    print(f"总吞吐量 (Throughput)   : {throughput:.2f} tokens/s")
    print(f"生成吞吐量 (Gen Speed)  : {output_throughput:.2f} tokens/s")
    print("="*60)