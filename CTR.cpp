/* --- START OF FILE CTR.cpp --- */

#include "kernel_operator.h" 
using namespace AscendC; 

// 基础配置
constexpr int32_t TILE_SIZE = 16 * 1024; // 16KB 分块
constexpr int32_t BUFFER_NUM = 2;        
constexpr int32_t BLOCK_SIZE = 16;       // AES Block 128-bit
constexpr int32_t KEY_ROUNDS = 11; 
constexpr int32_t MAX_RANKS = 16;        // 最大支持 16 卡聚合

// 模式定义
// 0: AES-CTR Standard
// 1: KeyStream Only
// 2: XOR Precomp
// 3: Mask Add (Legacy)
// 4: Mask Sub (Legacy)
// 5: Generate Pairwise Combined Mask (Secure Aggregation)
constexpr uint32_t MODE_GEN_PAIRWISE_MASK = 5;
constexpr int32_t BATCH_BLOCKS = 16;
constexpr int32_t BATCH_SIZE = 256;
// AES S-Box and Constants
const uint8_t sbox[256] = {
    0x63, 0x7C, 0x77, 0x7B, 0xF2, 0x6B, 0x6F, 0xC5, 0x30, 0x01, 0x67, 0x2B, 0xFE, 0xD7, 0xAB, 0x76,
    0xCA, 0x82, 0xC9, 0x7D, 0xFA, 0x59, 0x47, 0xF0, 0xAD, 0xD4, 0xA2, 0xAF, 0x9C, 0xA4, 0x72, 0xC0,
    0xB7, 0xFD, 0x93, 0x26, 0x36, 0x3F, 0xF7, 0xCC, 0x34, 0xA5, 0xE5, 0xF1, 0x71, 0xD8, 0x31, 0x15,
    0x04, 0xC7, 0x23, 0xC3, 0x18, 0x96, 0x05, 0x9A, 0x07, 0x12, 0x80, 0xE2, 0xEB, 0x27, 0xB2, 0x75,
    0x09, 0x83, 0x2C, 0x1A, 0x1B, 0x6E, 0x5A, 0xA0, 0x52, 0x3B, 0xD6, 0xB3, 0x29, 0xE3, 0x2F, 0x84,
    0x53, 0xD1, 0x00, 0xED, 0x20, 0xFC, 0xB1, 0x5B, 0x6A, 0xCB, 0xBE, 0x39, 0x4A, 0x4C, 0x58, 0xCF,
    0xD0, 0xEF, 0xAA, 0xFB, 0x43, 0x4D, 0x33, 0x85, 0x45, 0xF9, 0x02, 0x7F, 0x50, 0x3C, 0x9F, 0xA8,
    0x51, 0xA3, 0x40, 0x8F, 0x92, 0x9D, 0x38, 0xF5, 0xBC, 0xB6, 0xDA, 0x21, 0x10, 0xFF, 0xF3, 0xD2,
    0xCD, 0x0C, 0x13, 0xEC, 0x5F, 0x97, 0x44, 0x17, 0xC4, 0xA7, 0x7E, 0x3D, 0x64, 0x5D, 0x19, 0x73,
    0x60, 0x81, 0x4F, 0xDC, 0x22, 0x2A, 0x90, 0x88, 0x46, 0xEE, 0xB8, 0x14, 0xDE, 0x5E, 0x0B, 0xDB,
    0xE0, 0x32, 0x3A, 0x0A, 0x49, 0x06, 0x24, 0x5C, 0xC2, 0xD3, 0xAC, 0x62, 0x91, 0x95, 0xE4, 0x79,
    0xE7, 0xC8, 0x37, 0x6D, 0x8D, 0xD5, 0x4E, 0xA9, 0x6C, 0x56, 0xF4, 0xEA, 0x65, 0x7A, 0xAE, 0x08,
    0xBA, 0x78, 0x25, 0x2E, 0x1C, 0xA6, 0xB4, 0xC6, 0xE8, 0xDD, 0x74, 0x1F, 0x4B, 0xBD, 0x8B, 0x8A,
    0x70, 0x3E, 0xB5, 0x66, 0x48, 0x03, 0xF6, 0x0E, 0x61, 0x35, 0x57, 0xB9, 0x86, 0xC1, 0x1D, 0x9E,
    0xE1, 0xF8, 0x98, 0x11, 0x69, 0xD9, 0x8E, 0x94, 0x9B, 0x1E, 0x87, 0xE9, 0xCE, 0x55, 0x28, 0xDF,
    0x8C, 0xA1, 0x89, 0x0D, 0xBF, 0xE6, 0x42, 0x68, 0x41, 0x99, 0x2D, 0x0F, 0xB0, 0x54, 0xBB, 0x16
};
const uint8_t rcon[11] = {0x00, 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1B, 0x36};
const uint8_t gmul2[256] = {0, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 22, 24, 26, 28, 30, 32, 34, 36, 38, 40, 42, 44, 46, 48, 50, 52, 54, 56, 58, 60, 62, 64, 66, 68, 70, 72, 74, 76, 78, 80, 82, 84, 86, 88, 90, 92, 94, 96, 98, 100, 102, 104, 106, 108, 110, 112, 114, 116, 118, 120, 122, 124, 126, 128, 130, 132, 134, 136, 138, 140, 142, 144, 146, 148, 150, 152, 154, 156, 158, 160, 162, 164, 166, 168, 170, 172, 174, 176, 178, 180, 182, 184, 186, 188, 190, 192, 194, 196, 198, 200, 202, 204, 206, 208, 210, 212, 214, 216, 218, 220, 222, 224, 226, 228, 230, 232, 234, 236, 238, 240, 242, 244, 246, 248, 250, 252, 254, 27, 25, 31, 29, 19, 17, 23, 21, 11, 9, 15, 13, 3, 1, 7, 5, 59, 57, 63, 61, 51, 49, 55, 53, 43, 41, 47, 45, 35, 33, 39, 37, 91, 89, 95, 93, 83, 81, 87, 85, 75, 73, 79, 77, 67, 65, 71, 69, 123, 121, 127, 125, 115, 113, 119, 117, 107, 105, 111, 109, 99, 97, 103, 101, 155, 153, 159, 157, 147, 145, 151, 149, 139, 137, 143, 141, 131, 129, 135, 133, 187, 185, 191, 189, 179, 177, 183, 181, 171, 169, 175, 173, 163, 161, 167, 165, 219, 217, 223, 221, 211, 209, 215, 213, 203, 201, 207, 205, 195, 193, 199, 197, 251, 249, 255, 253, 243, 241, 247, 245, 235, 233, 239, 237, 227, 225, 231, 229};
const uint8_t gmul3[256] = {0, 3, 6, 5, 12, 15, 10, 9, 24, 27, 30, 29, 20, 23, 18, 17, 48, 51, 54, 53, 60, 63, 58, 57, 40, 43, 46, 45, 36, 39, 34, 33, 96, 99, 102, 101, 108, 111, 106, 105, 120, 123, 126, 125, 116, 119, 114, 113, 80, 83, 86, 85, 92, 95, 90, 89, 72, 75, 78, 77, 68, 71, 66, 65, 192, 195, 198, 197, 204, 207, 202, 201, 216, 219, 222, 221, 212, 215, 210, 209, 240, 243, 246, 245, 252, 255, 250, 249, 232, 235, 238, 237, 228, 231, 226, 225, 160, 163, 166, 165, 172, 175, 170, 169, 184, 187, 190, 189, 180, 183, 178, 177, 144, 147, 150, 149, 156, 159, 154, 153, 136, 139, 142, 141, 132, 135, 130, 129, 155, 152, 157, 158, 151, 148, 145, 146, 131, 128, 133, 134, 143, 140, 137, 138, 171, 168, 173, 174, 167, 164, 161, 162, 179, 176, 181, 182, 191, 188, 185, 186, 251, 248, 253, 254, 247, 244, 241, 242, 227, 224, 229, 230, 239, 236, 233, 234, 203, 200, 205, 206, 199, 196, 193, 194, 211, 208, 213, 214, 223, 220, 217, 218, 91, 88, 93, 94, 87, 84, 81, 82, 67, 64, 69, 70, 79, 76, 73, 74, 107, 104, 109, 110, 103, 100, 97, 98, 115, 112, 117, 118, 127, 124, 121, 122, 59, 56, 61, 62, 55, 52, 49, 50, 35, 32, 37, 38, 47, 44, 41, 42, 11, 8, 13, 14, 7, 4, 1, 2, 19, 16, 21, 22, 31, 28, 25, 26};
class AesKernel {
    public:
    __aicore__ inline AesKernel() {}
    
    __aicore__ inline void Init(GM_ADDR input, GM_ADDR key_or_mask, GM_ADDR iv_or_rank, GM_ADDR output, 
                                GM_ADDR integrity, 
                                uint32_t totalLength, uint32_t mode, uint32_t coreNum)
    {
        uint32_t avgLen = totalLength / coreNum;
        avgLen = (avgLen / 32) * 32; 
        
        if (GetBlockIdx() < coreNum - 1) {
            this->lenPerCore = avgLen;
        } else {
            this->lenPerCore = totalLength - avgLen * (coreNum - 1);
        }

        this->coreGlobalOffset = GetBlockIdx() * avgLen;
        this->mode = mode;
        this->useCoreNum = coreNum;

        if (this->coreGlobalOffset >= totalLength) {
            this->lenPerCore = 0;
        }

        outputGm.SetGlobalBuffer((__gm__ uint8_t *)output, totalLength);
        
        if (integrity != nullptr) {
            integrityGm.SetGlobalBuffer((__gm__ uint8_t *)integrity, coreNum * 32);
        }

        if (mode == 3 || mode == 4) { 
            inputGmHalf.SetGlobalBuffer((__gm__ half *)input, totalLength / 2);
            maskGmHalf.SetGlobalBuffer((__gm__ half *)key_or_mask, totalLength / 2);
            outputGmHalf.SetGlobalBuffer((__gm__ half *)output, totalLength / 2);
            if (mode == 4) rankGm.SetGlobalBuffer((__gm__ int32_t *)iv_or_rank, 8); 
        }
        else if (mode == MODE_GEN_PAIRWISE_MASK) {
            seedsGm.SetGlobalBuffer((__gm__ uint8_t *)key_or_mask, 16 * MAX_RANKS);
            rankInfoGm.SetGlobalBuffer((__gm__ int32_t *)iv_or_rank, 8); 
            outputGmHalf.SetGlobalBuffer((__gm__ half *)output, totalLength / 2);
        }
        else if (mode == 2) { 
            inputGm.SetGlobalBuffer((__gm__ uint8_t *)input, totalLength);
            keyStreamGm.SetGlobalBuffer((__gm__ uint8_t *)key_or_mask, totalLength);
        } 
        else { 
            if (input != nullptr) inputGm.SetGlobalBuffer((__gm__ uint8_t *)input, totalLength);
            keyGm.SetGlobalBuffer((__gm__ uint8_t *)key_or_mask, 32);
            ivGm.SetGlobalBuffer((__gm__ uint8_t *)iv_or_rank, 32); 
        }

        pipe.InitBuffer(inQueueInput, BUFFER_NUM, TILE_SIZE);
        pipe.InitBuffer(outQueueOutput, BUFFER_NUM, TILE_SIZE);
        
        if (mode == 3 || mode == 4) {
            pipe.InitBuffer(precompKeyQueue, BUFFER_NUM, TILE_SIZE);
            if (mode == 4) pipe.InitBuffer(rankBuf, 1, 32); 
        }
        else if (mode == MODE_GEN_PAIRWISE_MASK) {
            pipe.InitBuffer(rankBuf, 1, 64);
            pipe.InitBuffer(seedsQueue, 1, 16 * MAX_RANKS); 
            pipe.InitBuffer(tempBuf, 2, 64); 
            pipe.InitBuffer(roundKeysBuf, 1, KEY_ROUNDS * BLOCK_SIZE);
            pipe.InitBuffer(keyStreamBuf, BUFFER_NUM, 32); 
        }
        else if (mode == 2) {
            pipe.InitBuffer(precompKeyQueue, BUFFER_NUM, TILE_SIZE);
            pipe.InitBuffer(checksumBuf, 1, 32);
        }
        else {
            pipe.InitBuffer(tempBuf, 1, 4); 
            pipe.InitBuffer(roundKeysBuf, 1, KEY_ROUNDS * BLOCK_SIZE);
            pipe.InitBuffer(ivBuf, 1, 32);
            pipe.InitBuffer(keyStreamBuf, BUFFER_NUM, 32); 
            if (mode == 0) pipe.InitBuffer(checksumBuf, 1, 32);
        }

        pipe.InitBuffer(dstBuf, BLOCK_SIZE);      // 16 字节的独立输出区
        pipe.InitBuffer(sharedTmpBuf, 4096);      // 2KB 的底层运算临时工作台

        pipe.InitBuffer(stateBufA, TILE_SIZE);
        pipe.InitBuffer(stateBufB, TILE_SIZE);
        pipe.InitBuffer(batchedRkBuf, 3584);

        pipe.InitBuffer(sboxBuf, 512);
        pipe.InitBuffer(shiftOffsetsBuf, 1, 1024);
        pipe.InitBuffer(gmul2Buf, 1, 512);          // 256个 int16_t = 512 字节
        pipe.InitBuffer(rotOffsetsBuf, 1, 3072);    // 存放 rot1, rot2, rot3 各 256个 uint32_t (3 * 256 * 4 = 3072 字节)
        // LocalTensor<uint8_t> sboxLocal = sboxBuf.AllocTensor<uint8_t>();
        // // 将全局 sbox 常量拷贝到本地
        // for (int i = 0; i < 256; i++) sboxLocal(i) = sbox[i];
    }

    __aicore__ inline void Process()
    {
        if (this->lenPerCore == 0) return;

        if (mode == 0 || mode == 1) {
            KeyExpansion();
            CopyIvToLocal(this->coreGlobalOffset); 
            pipe_barrier(PIPE_V);
        } 
        
        if (mode == 0 || mode == 2) {
            InitChecksum();
        }

        int32_t myRank = 0;
        int32_t worldSize = 8;
        if (mode == MODE_GEN_PAIRWISE_MASK) {
            AscendC::LocalTensor<int32_t> rT = rankBuf.AllocTensor<int32_t>();
            AscendC::DataCopy(rT, rankInfoGm[0], 8); 
            myRank = rT.GetValue(0);
            worldSize = rT.GetValue(1);
            rankBuf.FreeTensor(rT);
        }
        
        if (mode == 4) {
            AscendC::LocalTensor<int32_t> rT = rankBuf.AllocTensor<int32_t>();
            AscendC::DataCopy(rT, rankGm[0], 1); 
            rankBuf.FreeTensor(rT);
        }

        uint32_t offsetInCore = 0;
        uint32_t remain = this->lenPerCore;

        // 计算完整块数和尾部大小
        uint32_t fullTileCount = remain / TILE_SIZE;
        uint32_t tailSize = remain % TILE_SIZE;
        uint32_t loopCount = fullTileCount + (tailSize > 0 ? 1 : 0);

        for (uint32_t loopIdx = 0; loopIdx < loopCount; loopIdx++) {
            uint32_t curLen = (loopIdx < fullTileCount) ? TILE_SIZE : tailSize;
            uint32_t copyLen = (curLen + 31) & ~31;  // 32B 对齐
            uint32_t globalAbsOffset = this->coreGlobalOffset + offsetInCore;

            if (mode == 2) {
                ProcessPrecomputed(globalAbsOffset, copyLen);
            } 
            else if (mode == 3) {
                ProcessMaskAdd(globalAbsOffset, copyLen); 
            }
            else if (mode == 4) {
                ProcessMaskSub(globalAbsOffset, copyLen, 8); 
            }
            else if (mode == MODE_GEN_PAIRWISE_MASK) {
                ProcessPairwiseMask(globalAbsOffset, copyLen, myRank, worldSize); 
            }
            else {
                if (mode == 0) CopyIn(globalAbsOffset, copyLen); 
                pipe_barrier(PIPE_V);
                ComputeCtr(globalAbsOffset, copyLen); 
                CopyOut(globalAbsOffset, copyLen);
                pipe_barrier(PIPE_V);
            }

            offsetInCore += curLen;  // 用原始长度累计偏移
        }

        if (mode == 0 || mode == 2) {
            OutputChecksum();
        }
        
        if (mode == 0 || mode == 1) {
            AscendC::LocalTensor<uint8_t> finalRoundKeys = roundKeysBuf.DeQue<uint8_t>();
            roundKeysBuf.FreeTensor(finalRoundKeys);
            AscendC::LocalTensor<uint8_t> finalIv = ivBuf.DeQue<uint8_t>();
            ivBuf.FreeTensor(finalIv);
        }
    }

    private:

    // -----------------------------------------------------------
    // Mode 5: Secure Aggregation (Robust Implementation)
    // -----------------------------------------------------------
    __aicore__ inline void ProcessPairwiseMask(uint32_t globalOffset, uint32_t len, int32_t myRank, int32_t worldSize) {
        AscendC::LocalTensor<half> outputLocal = outQueueOutput.AllocTensor<half>();
        // Read Input
        AscendC::DataCopy(outputLocal, outputGmHalf[globalOffset / 2], len / 2);

        AscendC::LocalTensor<uint8_t> scratchPad = inQueueInput.AllocTensor<uint8_t>();
        
        const uint32_t OFFSET_SEED = 0;
        const uint32_t OFFSET_AES_BLOCK = 32;
        const uint32_t OFFSET_ROUND_KEYS = 64;
        const uint32_t OFFSET_MASK_HALF = 256; 

        AscendC::LocalTensor<int8_t> aesBlockTensor = scratchPad[OFFSET_AES_BLOCK].ReinterpretCast<int8_t>();
        AscendC::LocalTensor<half> maskHalfTensor = scratchPad[OFFSET_MASK_HALF].ReinterpretCast<half>();

        uint32_t totalElements = len / 2;
        // Each AES block gives 16 bytes = 8 half elements?
        // Wait, maskHalfTensor is 16 elements (32 bytes). AES Block is 16 bytes.
        // We expand 16 bytes AES -> 16 int8 -> 16 half (32 bytes).
        // So 1 AES Op covers 16 half elements.
        uint32_t totalBatches = (totalElements + 15) / 16; 
        
        for (int32_t v = 0; v < worldSize; ++v) {
            if (v == myRank) continue;
            if (v >= MAX_RANKS) break;

            // A. 加载 Seed
            AscendC::DataCopy(scratchPad[OFFSET_SEED], seedsGm[v * 16], 16);
            
            // B. 密钥扩展 (只做一次)
            for(int i=0; i<16; i++) {
                scratchPad.SetValue(OFFSET_ROUND_KEYS + i, scratchPad.GetValue(OFFSET_SEED + i));
            }
            for(uint16_t round = 1; round <= 10; round++) {
                uint8_t p0 = scratchPad.GetValue(OFFSET_ROUND_KEYS + round*16 - 4);
                uint8_t p1 = scratchPad.GetValue(OFFSET_ROUND_KEYS + round*16 - 3);
                uint8_t p2 = scratchPad.GetValue(OFFSET_ROUND_KEYS + round*16 - 2);
                uint8_t p3 = scratchPad.GetValue(OFFSET_ROUND_KEYS + round*16 - 1);
                
                uint8_t tmp = p0; p0 = p1; p1 = p2; p2 = p3; p3 = tmp;
                p0 = sbox[p0]; p1 = sbox[p1]; p2 = sbox[p2]; p3 = sbox[p3];
                p0 ^= rcon[round];
                
                for(int i=0; i<4; i++) {
                    uint8_t rk_prev = scratchPad.GetValue(OFFSET_ROUND_KEYS + (round-1)*16 + i);
                    uint8_t new_byte = rk_prev ^ (i==0?p0 : i==1?p1 : i==2?p2 : p3);
                    scratchPad.SetValue(OFFSET_ROUND_KEYS + round*16 + i, new_byte);
                }
                for(int i=4; i<16; i++) {
                    uint8_t rk_prev = scratchPad.GetValue(OFFSET_ROUND_KEYS + (round-1)*16 + i);
                    uint8_t rk_curr_prev = scratchPad.GetValue(OFFSET_ROUND_KEYS + round*16 + i - 4);
                    scratchPad.SetValue(OFFSET_ROUND_KEYS + round*16 + i, rk_prev ^ rk_curr_prev);
                }
            }

            // C. 快速加掩码 (Mask Reuse)
            // 优化：只计算一次 AES Mask，然后复用
            for (uint32_t b = 0; b < totalBatches; ++b) {
                // [Optimization] Only compute AES for the first batch
                if (b == 0) {
                    uint32_t counter = 0xCAFEBABE; // Fixed counter for reuse mode
                    for(int k=0; k<12; k++) scratchPad.SetValue(OFFSET_AES_BLOCK + k, 0);
                    scratchPad.SetValue(OFFSET_AES_BLOCK + 12, (uint8_t)(counter >> 24));
                    scratchPad.SetValue(OFFSET_AES_BLOCK + 13, (uint8_t)(counter >> 16));
                    scratchPad.SetValue(OFFSET_AES_BLOCK + 14, (uint8_t)(counter >> 8));
                    scratchPad.SetValue(OFFSET_AES_BLOCK + 15, (uint8_t)(counter));

                    // AddRoundKey 0
                    for(int i=0; i<16; i++) {
                        uint8_t s = scratchPad.GetValue(OFFSET_AES_BLOCK + i);
                        uint8_t k = scratchPad.GetValue(OFFSET_ROUND_KEYS + i);
                        scratchPad.SetValue(OFFSET_AES_BLOCK + i, s ^ k);
                    }
                    
                    // Rounds 1-9
                    for (int round = 1; round < 10; round++) {
                        // SubBytes
                        for(int i=0; i<16; i++) scratchPad.SetValue(OFFSET_AES_BLOCK+i, sbox[scratchPad.GetValue(OFFSET_AES_BLOCK+i)]);
                        // ShiftRows
                        uint8_t t;
                        #define S(idx) scratchPad.GetValue(OFFSET_AES_BLOCK + idx)
                        #define W(idx, v) scratchPad.SetValue(OFFSET_AES_BLOCK + idx, v)
                        t=S(1); W(1,S(5)); W(5,S(9)); W(9,S(13)); W(13,t);
                        t=S(2); W(2,S(10)); W(10,t);
                        t=S(6); W(6,S(14)); W(14,t);
                        t=S(15); W(15,S(11)); W(11,S(7)); W(7,S(3)); W(3,t);
                        // MixColumns
                        for(int c=0; c<4; c++) {
                            uint8_t a0=S(4*c), a1=S(4*c+1), a2=S(4*c+2), a3=S(4*c+3);
                            W(4*c, gmul2[a0]^gmul3[a1]^a2^a3);
                            W(4*c+1, a0^gmul2[a1]^gmul3[a2]^a3);
                            W(4*c+2, a0^a1^gmul2[a2]^gmul3[a3]);
                            W(4*c+3, gmul3[a0]^a1^a2^gmul2[a3]);
                        }
                        // AddRoundKey
                        for(int i=0; i<16; i++) W(i, S(i) ^ scratchPad.GetValue(OFFSET_ROUND_KEYS + round*16 + i));
                    }
                    
                    // Final Round
                    for(int i=0; i<16; i++) scratchPad.SetValue(OFFSET_AES_BLOCK+i, sbox[scratchPad.GetValue(OFFSET_AES_BLOCK+i)]);
                    uint8_t t; 
                    t=S(1); W(1,S(5)); W(5,S(9)); W(9,S(13)); W(13,t);
                    t=S(2); W(2,S(10)); W(10,t);
                    t=S(6); W(6,S(14)); W(14,t);
                    t=S(15); W(15,S(11)); W(11,S(7)); W(7,S(3)); W(3,t);
                    for(int i=0; i<16; i++) W(i, S(i) ^ scratchPad.GetValue(OFFSET_ROUND_KEYS + 10*16 + i));
                    #undef S
                    #undef W

                    // 3. 向量转换 (Vector)
                    AscendC::Cast(maskHalfTensor, aesBlockTensor, RoundMode::CAST_NONE, 16);
                    AscendC::Muls(maskHalfTensor, maskHalfTensor, (half)0.01f, 16); 
                }

                // [FAST PATH] Reuse the mask computed in b==0
                uint32_t outOffset = b * 16;
                // [SAFETY] Boundary Check
                if (outOffset + 16 <= totalElements) {
                    if (myRank < v) {
                        AscendC::Add(outputLocal[outOffset], outputLocal[outOffset], maskHalfTensor, 16);
                    } else {
                        AscendC::Sub(outputLocal[outOffset], outputLocal[outOffset], maskHalfTensor, 16);
                    }
                }
            }
        }
        
        inQueueInput.FreeTensor(scratchPad);

        outQueueOutput.EnQue(outputLocal);
        AscendC::LocalTensor<half> tOut = outQueueOutput.DeQue<half>();
        AscendC::DataCopy(outputGmHalf[globalOffset / 2], tOut, len / 2);
        outQueueOutput.FreeTensor(tOut);
    }

    __aicore__ inline void KeyExpansion()
    {
        AscendC::LocalTensor<uint8_t>  prekey = tempBuf.AllocTensor<uint8_t>();
        AscendC::LocalTensor<uint8_t> roundKeysLocal = roundKeysBuf.AllocTensor<uint8_t>();
        AscendC::DataCopy(roundKeysLocal,keyGm[0],32);

        for(uint16_t round = 1;round<=10;round++)
        {
            prekey(0) = roundKeysLocal(round*16-4);
            prekey(1) = roundKeysLocal(round*16-3);
            prekey(2) = roundKeysLocal(round*16-2);
            prekey(3) = roundKeysLocal(round*16-1);

            T_keygen(prekey,round);

            for(uint16_t i = round * 16,j = 0; j < 4; i++,j++)
                roundKeysLocal(i) = roundKeysLocal(i-16) ^ prekey(j);
            for(uint16_t i = round * 16 + 4;i < round * 16 + 16;i++)
                roundKeysLocal(i) = roundKeysLocal(i-4) ^ roundKeysLocal(i-16);
        }
        roundKeysBuf.EnQue<uint8_t>(roundKeysLocal);
        tempBuf.FreeTensor(prekey);
    }

    __aicore__ inline void T_keygen(AscendC::LocalTensor<uint8_t>& W,uint32_t round)
    {
        uint8_t tmp = W(0); W(0) = W(1); W(1) = W(2); W(2) = W(3); W(3) = tmp;
        for(uint8_t i = 0;i < 4;i++) W(i) = sbox[W(i)];
        W(0) = W(0) ^ rcon[round];
    }

    __aicore__ inline void CopyIn(uint32_t globalOffset, uint32_t len)
    {
        AscendC::LocalTensor<uint8_t> inputLocal = inQueueInput.AllocTensor<uint8_t>();
        AscendC::DataCopy(inputLocal, inputGm[globalOffset], len);
        inQueueInput.EnQue<uint8_t>(inputLocal);
    }

    // __aicore__ inline void CopyIvToLocal(uint32_t globalByteOffset)
    // {
    //     AscendC::LocalTensor<uint8_t> ivLocal = ivBuf.AllocTensor<uint8_t>();
    //     AscendC::DataCopy(ivLocal, ivGm[0], 32);
    //     uint32_t blockOffset = globalByteOffset / 16;
    //     uint32_t counter = (uint32_t)ivLocal(12) << 24 | (uint32_t)ivLocal(13) << 16 | (uint32_t)ivLocal(14) << 8  | (uint32_t)ivLocal(15);
    //     counter += blockOffset;
    //     ivLocal(12) = (counter >> 24) & 0xFF; ivLocal(13) = (counter >> 16) & 0xFF;
    //     ivLocal(14) = (counter >> 8) & 0xFF; ivLocal(15) = counter & 0xFF;
    //     ivBuf.EnQue<uint8_t>(ivLocal);
    // }

    __aicore__ inline void CopyIvToLocal(uint32_t globalByteOffset) {
        AscendC::LocalTensor<uint8_t> ivLocal = ivBuf.AllocTensor<uint8_t>();
        // 仅仅发起异步搬运
        AscendC::DataCopy(ivLocal, ivGm[0], 32);
        // 入队
        ivBuf.EnQue(ivLocal);
    }
    
    // 【修改点 1】：函数签名加上 globalAbsOffset
    __aicore__ inline void ComputeCtr(uint32_t globalAbsOffset, uint32_t len)
    {
        pipe_barrier(PIPE_V);
        AscendC::LocalTensor<uint8_t> roundKeysLocal = roundKeysBuf.DeQue<uint8_t>();
        AscendC::LocalTensor<uint8_t> ivLocal = ivBuf.DeQue<uint8_t>();
        AscendC::LocalTensor<uint8_t> outputLocal = outQueueOutput.AllocTensor<uint8_t>();
        AscendC::LocalTensor<uint8_t> inputLocal;
        if (mode == 0) {
            inputLocal = inQueueInput.DeQue<uint8_t>();
            CalcChecksumLocal(inputLocal, len); 
        }

        // ================= 【修改点 2：修复 IV 计算】 =================
        // 1. 读取绝对纯净的起始 IV (因为我们后面不再修改 ivLocal，所以它永远是纯净的)
        uint32_t base_ctr = ((uint32_t)ivLocal(12) << 24) | ((uint32_t)ivLocal(13) << 16) | ((uint32_t)ivLocal(14) << 8) | (uint32_t)ivLocal(15);
        
        // 2. 根据全局绝对坐标，算出当前这个 16KB 切片的真正起始 Counter
        uint32_t tile_start_ctr = base_ctr + (globalAbsOffset / 16);

        // 3. 用一个临时的 current_iv 数组存下来，接下来的循环只改它，绝不碰 ivLocal！
        uint8_t current_iv[16];
        for(int i = 0; i < 12; i++) {
            current_iv[i] = ivLocal(i);
        }
        current_iv[12] = (tile_start_ctr >> 24) & 0xFF;
        current_iv[13] = (tile_start_ctr >> 16) & 0xFF;
        current_iv[14] = (tile_start_ctr >> 8) & 0xFF;
        current_iv[15] = (tile_start_ctr) & 0xFF;
        // =============================================================

        // ================= 终极防御：在循环外一次性分配全量内存 =================
        AscendC::LocalTensor<uint8_t> stateABase = stateBufA.AllocTensor<uint8_t>();
        AscendC::LocalTensor<uint8_t> stateBBase = stateBufB.AllocTensor<uint8_t>();
        AscendC::LocalTensor<uint8_t> sharedTmp  = sharedTmpBuf.AllocTensor<uint8_t>();
        AscendC::LocalTensor<uint8_t> batchedRk  = batchedRkBuf.AllocTensor<uint8_t>();
        // =======================================================================

        AscendC::LocalTensor<int16_t> sboxLocal = sboxBuf.AllocTensor<int16_t>();
        for (int i = 0; i < 256; i++) {
            sboxLocal(i) = (int16_t)sbox[i];
        }

        constexpr int32_t CURRENT_BATCH_BLOCKS = 16; 
        constexpr int32_t BATCH_BYTES = 256; 

        AscendC::LocalTensor<uint32_t> shiftOffsetsU32 = shiftOffsetsBuf.AllocTensor<uint32_t>();
        for (int b = 0; b < CURRENT_BATCH_BLOCKS; b++) { 
            uint32_t base = b * 16;
            shiftOffsetsU32(base + 0)  = (base + 0) * 2; shiftOffsetsU32(base + 1)  = (base + 5) * 2;
            shiftOffsetsU32(base + 2)  = (base + 10) * 2; shiftOffsetsU32(base + 3)  = (base + 15) * 2;
            shiftOffsetsU32(base + 4)  = (base + 4) * 2; shiftOffsetsU32(base + 5)  = (base + 9) * 2;
            shiftOffsetsU32(base + 6)  = (base + 14) * 2; shiftOffsetsU32(base + 7)  = (base + 3) * 2;
            shiftOffsetsU32(base + 8)  = (base + 8) * 2; shiftOffsetsU32(base + 9)  = (base + 13) * 2;
            shiftOffsetsU32(base + 10) = (base + 2) * 2; shiftOffsetsU32(base + 11) = (base + 7) * 2;
            shiftOffsetsU32(base + 12) = (base + 12) * 2; shiftOffsetsU32(base + 13) = (base + 1) * 2;
            shiftOffsetsU32(base + 14) = (base + 6) * 2; shiftOffsetsU32(base + 15) = (base + 11) * 2;
        }

        AscendC::LocalTensor<int16_t> gmul2Local16 = gmul2Buf.AllocTensor<int16_t>();
        for (int i = 0; i < 256; i++) {
            uint8_t g2 = (i << 1) ^ ((i & 0x80) ? 0x1B : 0x00);
            gmul2Local16(i) = (int16_t)g2;
        }

        AscendC::LocalTensor<uint32_t> rotOffsetsU32 = rotOffsetsBuf.AllocTensor<uint32_t>();
        AscendC::LocalTensor<uint32_t> rot1Offsets = rotOffsetsU32;
        AscendC::LocalTensor<uint32_t> rot2Offsets = rotOffsetsU32[256]; 
        AscendC::LocalTensor<uint32_t> rot3Offsets = rotOffsetsU32[512]; 
        for (int b = 0; b < CURRENT_BATCH_BLOCKS; b++) {
            for (int c = 0; c < 4; c++) { 
                uint32_t idx = b * 16 + c * 4;
                rot1Offsets(idx+0)=(idx+1)*2; rot1Offsets(idx+1)=(idx+2)*2; rot1Offsets(idx+2)=(idx+3)*2; rot1Offsets(idx+3)=(idx+0)*2;
                rot2Offsets(idx+0)=(idx+2)*2; rot2Offsets(idx+1)=(idx+3)*2; rot2Offsets(idx+2)=(idx+0)*2; rot2Offsets(idx+3)=(idx+1)*2;
                rot3Offsets(idx+0)=(idx+3)*2; rot3Offsets(idx+1)=(idx+0)*2; rot3Offsets(idx+2)=(idx+1)*2; rot3Offsets(idx+3)=(idx+2)*2;
            }
        }

        for (int r = 0; r < 11; r++) {
            for (int b = 0; b < CURRENT_BATCH_BLOCKS; b++) {
                for (int i = 0; i < 16; i++) {
                    batchedRk(r * 256 + b * 16 + i) = roundKeysLocal(r * 16 + i);
                }
            }
        }

        // len 已经是 32B 对齐，向上取整到 BATCH_BYTES
        uint32_t alignedLen = (len + BATCH_BYTES - 1) / BATCH_BYTES * BATCH_BYTES;
        int32_t currentBatches = alignedLen / BATCH_BYTES;
        constexpr uint32_t XOR_COUNT = 128; 

        for (uint32_t batchIdx = 0; batchIdx < currentBatches; batchIdx++) {
            
            // ================= 绝对物理隔离：按批次偏移切片 =================
            AscendC::LocalTensor<uint8_t> stateA = stateABase[batchIdx * BATCH_BYTES];
            AscendC::LocalTensor<uint8_t> stateB = stateBBase[batchIdx * BATCH_BYTES];
            // ==============================================================

            for (int b = 0; b < CURRENT_BATCH_BLOCKS; b++) {
                for (int j = 0; j < 16; j++) {
                    // 【修改点 3】：不再使用 ivLocal，改用 current_iv
                    stateA(b * 16 + j) = current_iv[j];
                }
                // 【修改点 3】：仅在 current_iv 上自增
                if (++current_iv[15] == 0) { if (++current_iv[14] == 0) { if (++current_iv[13] == 0) ++current_iv[12]; } }
            }

            AscendC::LocalTensor<uint16_t> stateA_16 = stateA.ReinterpretCast<uint16_t>();
            AscendC::LocalTensor<uint16_t> stateB_16 = stateB.ReinterpretCast<uint16_t>();

            AscendC::Xor(stateB_16, stateA_16, batchedRk[0 * 256].ReinterpretCast<uint16_t>(), XOR_COUNT);

            SubBytes_Batch(stateB, sboxLocal, sharedTmp); ShiftRows_Batch(stateB, shiftOffsetsU32, sharedTmp); MixColumns_Batch(stateB, gmul2Local16, rot1Offsets, rot2Offsets, rot3Offsets, sharedTmp);
            AscendC::Xor(stateA_16, stateB_16, batchedRk[1 * 256].ReinterpretCast<uint16_t>(), XOR_COUNT);

            SubBytes_Batch(stateA, sboxLocal, sharedTmp); ShiftRows_Batch(stateA, shiftOffsetsU32, sharedTmp); MixColumns_Batch(stateA, gmul2Local16, rot1Offsets, rot2Offsets, rot3Offsets, sharedTmp);
            AscendC::Xor(stateB_16, stateA_16, batchedRk[2 * 256].ReinterpretCast<uint16_t>(), XOR_COUNT);

            SubBytes_Batch(stateB, sboxLocal, sharedTmp); ShiftRows_Batch(stateB, shiftOffsetsU32, sharedTmp); MixColumns_Batch(stateB, gmul2Local16, rot1Offsets, rot2Offsets, rot3Offsets, sharedTmp);
            AscendC::Xor(stateA_16, stateB_16, batchedRk[3 * 256].ReinterpretCast<uint16_t>(), XOR_COUNT);

            SubBytes_Batch(stateA, sboxLocal, sharedTmp); ShiftRows_Batch(stateA, shiftOffsetsU32, sharedTmp); MixColumns_Batch(stateA, gmul2Local16, rot1Offsets, rot2Offsets, rot3Offsets, sharedTmp);
            AscendC::Xor(stateB_16, stateA_16, batchedRk[4 * 256].ReinterpretCast<uint16_t>(), XOR_COUNT);

            SubBytes_Batch(stateB, sboxLocal, sharedTmp); ShiftRows_Batch(stateB, shiftOffsetsU32, sharedTmp); MixColumns_Batch(stateB, gmul2Local16, rot1Offsets, rot2Offsets, rot3Offsets, sharedTmp);
            AscendC::Xor(stateA_16, stateB_16, batchedRk[5 * 256].ReinterpretCast<uint16_t>(), XOR_COUNT);

            SubBytes_Batch(stateA, sboxLocal, sharedTmp); ShiftRows_Batch(stateA, shiftOffsetsU32, sharedTmp); MixColumns_Batch(stateA, gmul2Local16, rot1Offsets, rot2Offsets, rot3Offsets, sharedTmp);
            AscendC::Xor(stateB_16, stateA_16, batchedRk[6 * 256].ReinterpretCast<uint16_t>(), XOR_COUNT);

            SubBytes_Batch(stateB, sboxLocal, sharedTmp); ShiftRows_Batch(stateB, shiftOffsetsU32, sharedTmp); MixColumns_Batch(stateB, gmul2Local16, rot1Offsets, rot2Offsets, rot3Offsets, sharedTmp);
            AscendC::Xor(stateA_16, stateB_16, batchedRk[7 * 256].ReinterpretCast<uint16_t>(), XOR_COUNT);

            SubBytes_Batch(stateA, sboxLocal, sharedTmp); ShiftRows_Batch(stateA, shiftOffsetsU32, sharedTmp); MixColumns_Batch(stateA, gmul2Local16, rot1Offsets, rot2Offsets, rot3Offsets, sharedTmp);
            AscendC::Xor(stateB_16, stateA_16, batchedRk[8 * 256].ReinterpretCast<uint16_t>(), XOR_COUNT);

            SubBytes_Batch(stateB, sboxLocal, sharedTmp); ShiftRows_Batch(stateB, shiftOffsetsU32, sharedTmp); MixColumns_Batch(stateB, gmul2Local16, rot1Offsets, rot2Offsets, rot3Offsets, sharedTmp);
            AscendC::Xor(stateA_16, stateB_16, batchedRk[9 * 256].ReinterpretCast<uint16_t>(), XOR_COUNT);

            SubBytes_Batch(stateA, sboxLocal, sharedTmp); ShiftRows_Batch(stateA, shiftOffsetsU32, sharedTmp);
            AscendC::Xor(stateB_16, stateA_16, batchedRk[10 * 256].ReinterpretCast<uint16_t>(), XOR_COUNT);

            if (mode == 0) {
                AscendC::LocalTensor<uint16_t> out16_f = outputLocal[batchIdx * BATCH_BYTES].ReinterpretCast<uint16_t>();
                AscendC::LocalTensor<uint16_t> in16_f = inputLocal[batchIdx * BATCH_BYTES].ReinterpretCast<uint16_t>();
                AscendC::Xor(out16_f, in16_f, stateB_16, XOR_COUNT); 
            } else {
                AscendC::DataCopy(outputLocal[batchIdx * BATCH_BYTES], stateB, BATCH_BYTES);
            }
        }
        
        // 集中在循环外执行一次性的回收
        sharedTmpBuf.FreeTensor(sharedTmp);
        stateBufB.FreeTensor(stateBBase);
        stateBufA.FreeTensor(stateABase);
        sboxBuf.FreeTensor(sboxLocal);
        batchedRkBuf.FreeTensor(batchedRk);
        rotOffsetsBuf.FreeTensor(rotOffsetsU32);
        gmul2Buf.FreeTensor(gmul2Local16);
        shiftOffsetsBuf.FreeTensor(shiftOffsetsU32);
        
        outQueueOutput.EnQue<uint8_t>(outputLocal);
        roundKeysBuf.EnQue<uint8_t>(roundKeysLocal);
        
        // 【修改点 4】：因为我们全过程没修改 ivLocal，现在它干干净净地回到队列！
        ivBuf.EnQue<uint8_t>(ivLocal); 
        pipe_barrier(PIPE_V);
        
        if (mode == 0) {
            inQueueInput.FreeTensor(inputLocal);
        }
    }
    __aicore__ inline void CopyOut(uint32_t globalOffset, uint32_t len)
    {
        AscendC::LocalTensor<uint8_t> outputLocal = outQueueOutput.DeQue<uint8_t>();
        AscendC::DataCopy(outputGm[globalOffset], outputLocal, len);
        outQueueOutput.FreeTensor(outputLocal);
        pipe_barrier(PIPE_V);
    }
    
    __aicore__ inline void SubBytes(AscendC::LocalTensor<uint8_t>& state) { for (int i = 0; i < 16; i++) state(i) = sbox[state(i)]; }
    __aicore__ inline void ShiftRows(AscendC::LocalTensor<uint8_t>& state) { uint8_t temp; temp = state(1); state(1) = state(5); state(5) = state(9); state(9) = state(13); state(13) = temp; temp = state(2); state(2) = state(10); state(10) = temp; temp = state(6); state(6) = state(14); state(14) = temp; temp = state(15); state(15) = state(11); state(11) = state(7); state(7) = state(3); state(3) = temp; }
    __aicore__ inline void MixColumns(AscendC::LocalTensor<uint8_t>& state) { for (int i = 0; i < 4; i++) { uint8_t a0 = state(4 * i), a1 = state(4 * i + 1), a2 = state(4 * i + 2), a3 = state(4 * i + 3); state(4 * i) = gmul2[a0] ^ gmul3[a1] ^ a2 ^ a3; state(4 * i + 1) = a0 ^ gmul2[a1] ^ gmul3[a2] ^ a3; state(4 * i + 2) = a0 ^ a1 ^ gmul2[a2] ^ gmul3[a3]; state(4 * i + 3) = gmul3[a0] ^ a1 ^ a2 ^ gmul2[a3]; } }

    // 修改函数的传参，增加 sboxLocal 和一块用于类型转换的 sharedTmp
    // 注意：传进来的 sbox 变成了 int16_t 类型！
    __aicore__ inline void SubBytes_Batch(
        AscendC::LocalTensor<uint8_t>& stateBatch, 
        const AscendC::LocalTensor<int16_t>& sboxLocal16, 
        AscendC::LocalTensor<uint8_t>& sharedTmp) 
    {
        // 1. 严格切分 2048 字节的临时内存 (你改了 4096 所以绝对够用)
        AscendC::LocalTensor<half> tmpHalf = sharedTmp.ReinterpretCast<half>();
        AscendC::LocalTensor<int32_t> offsetI32 = sharedTmp[512].ReinterpretCast<int32_t>();
        AscendC::LocalTensor<int16_t> state16 = sharedTmp[1536].ReinterpretCast<int16_t>();

        // 2. 将 uint8_t 提升到 half
        AscendC::Cast(tmpHalf, stateBatch, AscendC::RoundMode::CAST_NONE, 256);
        
        // 3. 【核心修正】因为现在 S-Box 是 16-bit (2字节)，真实的字节偏移 = 索引 * 2
        // 用浮点乘法最安全，保证全平台兼容
        AscendC::Muls(tmpHalf, tmpHalf, (half)2.0, 256);

        // 4. 将乘以 2 后的偏移转为 uint32_t
        AscendC::Cast(offsetI32, tmpHalf, AscendC::RoundMode::CAST_RINT, 256);
        AscendC::LocalTensor<uint32_t> offsetU32 = offsetI32.ReinterpretCast<uint32_t>();

        // 5. Gather 向量查表！拿到 16-bit 的正确结果
        AscendC::Gather(state16, sboxLocal16, offsetU32, 0, 256);

        // 6. 结果降维：int16_t 不能直接转 uint8_t，我们通过 half 中转一下
        AscendC::Cast(tmpHalf, state16, AscendC::RoundMode::CAST_NONE, 256);
        AscendC::Cast(stateBatch, tmpHalf, AscendC::RoundMode::CAST_RINT, 256);
    }
    
    __aicore__ inline void ShiftRows_Batch(
        AscendC::LocalTensor<uint8_t>& stateBatch,
        const AscendC::LocalTensor<uint32_t>& shiftOffsetsU32,
        AscendC::LocalTensor<uint8_t>& sharedTmp)
    {
        // 利用 sharedTmp 切分临时空间 (需要 1536 字节)
        AscendC::LocalTensor<half> tmpHalf = sharedTmp.ReinterpretCast<half>();
        AscendC::LocalTensor<int16_t> state16 = sharedTmp[512].ReinterpretCast<int16_t>();
        AscendC::LocalTensor<int16_t> shifted16 = sharedTmp[1024].ReinterpretCast<int16_t>();
    
        // 1. 状态升维：uint8 -> half -> int16
        AscendC::Cast(tmpHalf, stateBatch, AscendC::RoundMode::CAST_NONE, 256);
        AscendC::Cast(state16, tmpHalf, AscendC::RoundMode::CAST_RINT, 256);
    
        // 2. 向量乱序重排！(一次性把 16 个 block 的行移位全部搞定)
        AscendC::Gather(shifted16, state16, shiftOffsetsU32, 0, 256);
    
        // 3. 状态降维：int16 -> half -> uint8 写回
        AscendC::Cast(tmpHalf, shifted16, AscendC::RoundMode::CAST_NONE, 256);
        AscendC::Cast(stateBatch, tmpHalf, AscendC::RoundMode::CAST_RINT, 256);
    }
    
    __aicore__ inline void MixColumns_Batch(
        AscendC::LocalTensor<uint8_t>& stateBatch,
        const AscendC::LocalTensor<int16_t>& gmul2Local16,
        const AscendC::LocalTensor<uint32_t>& rot1Offsets,
        const AscendC::LocalTensor<uint32_t>& rot2Offsets,
        const AscendC::LocalTensor<uint32_t>& rot3Offsets,
        AscendC::LocalTensor<uint8_t>& sharedTmp)
    {
        // 1. 严格切分临时空间 (都在 sharedTmp 的 4096 字节内)
        AscendC::LocalTensor<half> tmpHalf     = sharedTmp.ReinterpretCast<half>();           // 0 ~ 512
        AscendC::LocalTensor<int32_t> offsetI32= sharedTmp[512].ReinterpretCast<int32_t>();   // 512 ~ 1536
        AscendC::LocalTensor<uint32_t> offsetU32= offsetI32.ReinterpretCast<uint32_t>();
        
        AscendC::LocalTensor<int16_t> state16  = sharedTmp[1536].ReinterpretCast<int16_t>();  // 1536 ~ 2048
        AscendC::LocalTensor<int16_t> rot1     = sharedTmp[2048].ReinterpretCast<int16_t>();  // 2048 ~ 2560
        AscendC::LocalTensor<int16_t> temp16   = sharedTmp[2560].ReinterpretCast<int16_t>();  // 2560 ~ 3072
        AscendC::LocalTensor<int16_t> result16 = sharedTmp[3072].ReinterpretCast<int16_t>();  // 3072 ~ 3584
    
        // 2. 状态升维 uint8 -> half -> int16
        AscendC::Cast(tmpHalf, stateBatch, AscendC::RoundMode::CAST_NONE, 256);
        AscendC::Cast(state16, tmpHalf, AscendC::RoundMode::CAST_RINT, 256);
    
        // 3. 构造基础循环重排: rot1, rot2, rot3 并累加到 result16
        AscendC::Gather(rot1, state16, rot1Offsets, 0, 256);
        AscendC::Gather(temp16, state16, rot2Offsets, 0, 256);
        AscendC::Xor(result16, rot1, temp16, 256);
        AscendC::Gather(temp16, state16, rot3Offsets, 0, 256);
        AscendC::Xor(result16, result16, temp16, 256); // 此时 result16 = rot1 ^ rot2 ^ rot3
    
        // 4. 计算 gmul2(S) 并异或
        AscendC::Cast(tmpHalf, state16, AscendC::RoundMode::CAST_NONE, 256);
        AscendC::Muls(tmpHalf, tmpHalf, (half)2.0, 256); // 字节偏移乘 2
        AscendC::Cast(offsetI32, tmpHalf, AscendC::RoundMode::CAST_RINT, 256);
        AscendC::Gather(temp16, gmul2Local16, offsetU32, 0, 256); // temp16 = gmul2(S)
        AscendC::Xor(result16, result16, temp16, 256);
    
        // 5. 计算 gmul2(rot1) 并异或
        AscendC::Cast(tmpHalf, rot1, AscendC::RoundMode::CAST_NONE, 256);
        AscendC::Muls(tmpHalf, tmpHalf, (half)2.0, 256);
        AscendC::Cast(offsetI32, tmpHalf, AscendC::RoundMode::CAST_RINT, 256);
        AscendC::Gather(temp16, gmul2Local16, offsetU32, 0, 256); // temp16 = gmul2(rot1)
        AscendC::Xor(result16, result16, temp16, 256);
    
        // 6. 最终结果降维写回 result16 -> half -> uint8
        AscendC::Cast(tmpHalf, result16, AscendC::RoundMode::CAST_NONE, 256);
        AscendC::Cast(stateBatch, tmpHalf, AscendC::RoundMode::CAST_RINT, 256);
    }
    
    // 辅助函数：将 16 字节的单轮密钥广播复制到整个 Batch
    __aicore__ inline void BroadcastRoundKey(AscendC::LocalTensor<uint8_t>& batchedRk, const AscendC::LocalTensor<uint8_t>& singleRk) {
        for (int b = 0; b < 16; b++) {
            for (int i = 0; i < 16; i++) {
                batchedRk(b * 16 + i) = singleRk(i);
            }
        }
    }

    __aicore__ inline void AddRoundKey(
        AscendC::LocalTensor<uint8_t>& state, 
        const AscendC::LocalTensor<uint8_t>& roundKey,
        AscendC::LocalTensor<uint8_t>& dstBlock,    // 来自 dstBuf
        AscendC::LocalTensor<uint8_t>& sharedTmp)   // 来自 sharedTmpBuf
    { 
        // 1. 类型强转，满足要求
        AscendC::LocalTensor<uint16_t> src0 = state.ReinterpretCast<uint16_t>();
        AscendC::LocalTensor<uint16_t> src1 = roundKey.ReinterpretCast<uint16_t>();
        AscendC::LocalTensor<uint16_t> dst  = dstBlock.ReinterpretCast<uint16_t>();
        
        // 2. 完美的官方调用姿势 (输出，输入0，输入1，工作台，元素个数)
        // 16 字节 = 8 个 uint16_t，所以 calCount = 8
        AscendC::Xor(dst, src0, src1, sharedTmp, 8); 
        
        // 3. 把算好的独立结果拷回到 state 中
        for (int i = 0; i < 16; i++) {
            state(i) = dstBlock(i);
        }
    }
    // Legacy Modes
    __aicore__ inline void InitChecksum() {
        AscendC::LocalTensor<int32_t> sumLocal = checksumBuf.AllocTensor<int32_t>();
        AscendC::Duplicate(sumLocal, (int32_t)0, 8); 
        checksumBuf.EnQue<int32_t>(sumLocal);
    }

    __aicore__ inline void OutputChecksum() {
        AscendC::LocalTensor<int32_t> sumLocal = checksumBuf.DeQue<int32_t>();
        AscendC::LocalTensor<uint8_t> sumLocalBytes = sumLocal.ReinterpretCast<uint8_t>();
        AscendC::DataCopy(integrityGm[GetBlockIdx() * 32], sumLocalBytes, 32);
        checksumBuf.FreeTensor(sumLocal);
    }
    
    __aicore__ inline void CalcChecksumLocal(AscendC::LocalTensor<uint8_t>& data, uint32_t len) {
        AscendC::LocalTensor<int32_t> sumLocal = checksumBuf.DeQue<int32_t>();
        AscendC::LocalTensor<int32_t> dataU32 = data.ReinterpretCast<int32_t>();
        uint32_t totalBlocks32 = len / 32; 
        AscendC::BinaryRepeatParams repeatParams;
        repeatParams.dstRepStride = 0;
        repeatParams.src0RepStride = 0;
        repeatParams.src1RepStride = 0; 
        for (uint32_t i = 0; i < totalBlocks32; ++i) {
            AscendC::Add(sumLocal, sumLocal, dataU32[i * 8], (uint64_t)8, 1, repeatParams);
        }
        checksumBuf.EnQue<int32_t>(sumLocal);
    }

    __aicore__ inline void ProcessPrecomputed(uint32_t globalOffset, uint32_t len)
    {
        AscendC::LocalTensor<uint8_t> inputLocal = inQueueInput.AllocTensor<uint8_t>();
        AscendC::LocalTensor<uint8_t> keyLocal = precompKeyQueue.AllocTensor<uint8_t>();
        AscendC::LocalTensor<uint8_t> outputLocal = outQueueOutput.AllocTensor<uint8_t>();

        AscendC::DataCopy(inputLocal, inputGm[globalOffset], len);
        AscendC::DataCopy(keyLocal, keyStreamGm[globalOffset], len);
        inQueueInput.EnQue(inputLocal);
        precompKeyQueue.EnQue(keyLocal);

        AscendC::LocalTensor<uint8_t> tIn = inQueueInput.DeQue<uint8_t>();
        AscendC::LocalTensor<uint8_t> tKey = precompKeyQueue.DeQue<uint8_t>();
        CalcChecksumLocal(tIn, len);

        AscendC::LocalTensor<uint16_t> tIn16 = tIn.ReinterpretCast<uint16_t>();
        AscendC::LocalTensor<uint16_t> tKey16 = tKey.ReinterpretCast<uint16_t>();
        AscendC::LocalTensor<uint16_t> tOut16 = outputLocal.ReinterpretCast<uint16_t>();
        AscendC::Xor(tOut16, tIn16, tKey16, len / 2);

        outQueueOutput.EnQue(outputLocal);
        inQueueInput.FreeTensor(tIn);
        precompKeyQueue.FreeTensor(tKey);
        AscendC::LocalTensor<uint8_t> tOut = outQueueOutput.DeQue<uint8_t>();
        AscendC::DataCopy(outputGm[globalOffset], tOut, len);
        outQueueOutput.FreeTensor(tOut);
    }
    
    __aicore__ inline void ProcessMaskAdd(uint32_t globalOffset, uint32_t len)
    {
        uint32_t copyLen = (len + 31) & ~31;  // 32B 对齐
        uint32_t copyLenHalf = copyLen / 2;
        AscendC::LocalTensor<half> inputLocal = inQueueInput.AllocTensor<half>();
        AscendC::LocalTensor<half> maskLocal = precompKeyQueue.AllocTensor<half>();
        AscendC::LocalTensor<half> outputLocal = outQueueOutput.AllocTensor<half>();

        AscendC::DataCopy(inputLocal, inputGmHalf[globalOffset/2], copyLenHalf);
        AscendC::DataCopy(maskLocal, maskGmHalf[globalOffset/2], copyLenHalf);
        inQueueInput.EnQue(inputLocal);
        precompKeyQueue.EnQue(maskLocal);

        AscendC::LocalTensor<half> tIn = inQueueInput.DeQue<half>();
        AscendC::LocalTensor<half> tMask = precompKeyQueue.DeQue<half>();
        AscendC::Add(outputLocal, tIn, tMask, copyLenHalf);

        outQueueOutput.EnQue(outputLocal);
        inQueueInput.FreeTensor(tIn);
        precompKeyQueue.FreeTensor(tMask);
        AscendC::LocalTensor<half> tOut = outQueueOutput.DeQue<half>();
        AscendC::DataCopy(outputGmHalf[globalOffset/2], tOut, copyLenHalf);
        outQueueOutput.FreeTensor(tOut);
    }

    __aicore__ inline void ProcessMaskSub(uint32_t globalOffset, uint32_t len, int32_t rankSize)
    {
        uint32_t copyLen = (len + 31) & ~31;  // 32B 对齐
        uint32_t copyLenHalf = copyLen / 2;
        AscendC::LocalTensor<half> inputLocal = inQueueInput.AllocTensor<half>();
        AscendC::LocalTensor<half> maskLocal = precompKeyQueue.AllocTensor<half>();
        AscendC::LocalTensor<half> outputLocal = outQueueOutput.AllocTensor<half>();

        AscendC::DataCopy(inputLocal, inputGmHalf[globalOffset/2], copyLenHalf);
        AscendC::DataCopy(maskLocal, maskGmHalf[globalOffset/2], copyLenHalf);
        inQueueInput.EnQue(inputLocal);
        precompKeyQueue.EnQue(maskLocal);

        AscendC::LocalTensor<half> tIn = inQueueInput.DeQue<half>();
        AscendC::LocalTensor<half> tMask = precompKeyQueue.DeQue<half>();
        AscendC::Muls(tMask, tMask, (half)rankSize, copyLenHalf);
        AscendC::Sub(outputLocal, tIn, tMask, copyLenHalf);

        outQueueOutput.EnQue(outputLocal);
        inQueueInput.FreeTensor(tIn);
        precompKeyQueue.FreeTensor(tMask);
        AscendC::LocalTensor<half> tOut = outQueueOutput.DeQue<half>();
        AscendC::DataCopy(outputGmHalf[globalOffset/2], tOut, copyLenHalf);
        outQueueOutput.FreeTensor(tOut);
    }

    private:
    TPipe pipe;
    TQue<QuePosition::VECIN, BUFFER_NUM> inQueueInput;
    TQue<QuePosition::VECOUT, BUFFER_NUM> outQueueOutput;
    
    TQue<QuePosition::VECIN, 1> roundKeysBuf;
    TQue<QuePosition::VECIN, 2> tempBuf; 
    TQue<QuePosition::VECIN, 1> ivBuf; 
    TQue<QuePosition::VECIN, BUFFER_NUM> keyStreamBuf; 
    
    TQue<QuePosition::VECIN, BUFFER_NUM> precompKeyQueue; 
    TQue<QuePosition::VECIN, 1> rankBuf; 
    TQue<QuePosition::VECIN, 1> seedsQueue; 
    TQue<QuePosition::VECIN, 1> checksumBuf;

    TBuf<QuePosition::VECCALC> dstBuf;      // 专门用作独立的输出 Tensor
    TBuf<QuePosition::VECCALC> sharedTmpBuf;// 专门喂给 Xor 算子当工作台

    TBuf<QuePosition::VECCALC> stateBufA;      // 乒乓缓存 A
    TBuf<QuePosition::VECCALC> stateBufB;      // 乒乓缓存 B
    TBuf<QuePosition::VECCALC> batchedRkBuf;   // 用于存放广播到 256 字节的轮密钥
    AscendC::TQue<AscendC::QuePosition::VECCALC, 1> shiftOffsetsBuf;
    AscendC::TQue<AscendC::QuePosition::VECCALC, 1> gmul2Buf;
    AscendC::TQue<AscendC::QuePosition::VECCALC, 1> rotOffsetsBuf;

    TBuf<QuePosition::VECCALC> sboxBuf;

    GlobalTensor<uint8_t> inputGm;
    GlobalTensor<uint8_t> keyGm;      
    GlobalTensor<uint8_t> keyStreamGm;
    GlobalTensor<uint8_t> ivGm; 
    GlobalTensor<uint8_t> outputGm;
    GlobalTensor<uint8_t> integrityGm;
    
    GlobalTensor<half> inputGmHalf;
    GlobalTensor<half> maskGmHalf;
    GlobalTensor<half> outputGmHalf;
    GlobalTensor<int32_t> rankGm;
    
    GlobalTensor<uint8_t> seedsGm; 
    GlobalTensor<int32_t> rankInfoGm;

    uint32_t mode;
    uint32_t lenPerCore;
    uint32_t coreGlobalOffset;
    uint32_t useCoreNum;
};

extern "C" __global__ __aicore__ void aes_cipher_kernel_npu(GM_ADDR input_x, GM_ADDR key, GM_ADDR iv, GM_ADDR output, 
                                                            GM_ADDR integrity, 
                                                            uint32_t totalLength, uint32_t mode)
{
    AesKernel encrypt;
    encrypt.Init(input_x, key, iv, output, integrity, totalLength, mode, GetBlockNum()); 
    encrypt.Process();
}

#ifndef ASCENDC_CPU_DEBUG
extern "C" __attribute__((visibility("default"))) void add_custom_do(uint32_t blockDim, void *stream, uint8_t *x, uint8_t *y, uint8_t *iv, uint8_t *z, uint32_t totallength, uint32_t mode)
{
    uint8_t* integrity_ptr = nullptr;
    if (mode == 0 || mode == 2) {
        integrity_ptr = z + totallength;
    } 
    
    aes_cipher_kernel_npu<<<blockDim, nullptr, stream>>>(x, y, iv, z, integrity_ptr, totallength, mode);
}
#endif
/* --- END OF FILE CTR.cpp --- */