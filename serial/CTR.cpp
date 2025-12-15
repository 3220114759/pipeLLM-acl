
#include "kernel_operator.h" 
using namespace AscendC; 

// 基础配置
constexpr int32_t TILE_SIZE = 16 * 1024; 
constexpr int32_t BUFFER_NUM = 2;        
constexpr int32_t BLOCK_SIZE = 16;       
constexpr int32_t KEY_ROUNDS = 11; 

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
    
    // 初始化函数：支持多核和多种模式
    __aicore__ inline void Init(GM_ADDR input, GM_ADDR key_or_mask, GM_ADDR iv_or_rank, GM_ADDR output, 
                                GM_ADDR integrity, 
                                uint32_t totalLength, uint32_t mode, uint32_t coreNum)
    {
        // 计算每个核处理的数据量
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

        if (mode == 3 || mode == 4) { // [New] Mode 3/4: Masking (FP16 Add/Sub)
            // 绑定为 half 类型指针，方便 DataCopy
            inputGmHalf.SetGlobalBuffer((__gm__ half *)input, totalLength / 2);
            maskGmHalf.SetGlobalBuffer((__gm__ half *)key_or_mask, totalLength / 2);
            outputGmHalf.SetGlobalBuffer((__gm__ half *)output, totalLength / 2);
            
            if (mode == 4) {
                // Mode 4 需要 RankSize。我们约定 Host 把 RankSize 传在 iv_or_rank 指向的内存里
                rankGm.SetGlobalBuffer((__gm__ int32_t *)iv_or_rank, 8); // 读取一点点就够了
            }
        }
        else if (mode == 2) { // Mode 2: XOR KeyStream
            inputGm.SetGlobalBuffer((__gm__ uint8_t *)input, totalLength);
            keyStreamGm.SetGlobalBuffer((__gm__ uint8_t *)key_or_mask, totalLength);
        } 
        else { // Mode 0/1: AES
            if (input != nullptr) inputGm.SetGlobalBuffer((__gm__ uint8_t *)input, totalLength);
            keyGm.SetGlobalBuffer((__gm__ uint8_t *)key_or_mask, 32);
            ivGm.SetGlobalBuffer((__gm__ uint8_t *)iv_or_rank, 32); 
        }


        pipe.InitBuffer(inQueueInput, BUFFER_NUM, TILE_SIZE);
        pipe.InitBuffer(outQueueOutput, BUFFER_NUM, TILE_SIZE);
        
        if (mode == 3 || mode == 4) {
            // Masking 模式：需要队列读 Mask
            pipe.InitBuffer(precompKeyQueue, BUFFER_NUM, TILE_SIZE);
            if (mode == 4) pipe.InitBuffer(rankBuf, 1, 32); // 用于存 RankSize
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
    }

    __aicore__ inline void Process()
    {
        if (this->lenPerCore == 0) return;

        // AES 模式初始化
        if (mode == 0 || mode == 1) {
            KeyExpansion();
            CopyIvToLocal(this->coreGlobalOffset); 
        } 
        
        // Mode 4 准备 RankSize
        int32_t rankSize = 8; // 默认 8 卡
        if (mode == 4) {

            AscendC::LocalTensor<int32_t> rT = rankBuf.AllocTensor<int32_t>();
            AscendC::DataCopy(rT, rankGm[0], 1); // 读 1 个 int32

            rankSize = 8; 
            rankBuf.FreeTensor(rT);
        }

        if (mode == 0 || mode == 2) {
            InitChecksum();
        }

        uint32_t offsetInCore = 0;
        uint32_t remain = this->lenPerCore;

        while (remain > 0) {
            uint32_t curLen = (remain > TILE_SIZE) ? TILE_SIZE : remain;
            uint32_t globalAbsOffset = this->coreGlobalOffset + offsetInCore;

            if (mode == 2) {
                ProcessPrecomputed(globalAbsOffset, curLen);
            } 
            else if (mode == 3) {
                ProcessMaskAdd(globalAbsOffset, curLen); // 加密
            }
            else if (mode == 4) {
                ProcessMaskSub(globalAbsOffset, curLen, rankSize); // 解密
            }
            else {
                if (mode == 0) CopyIn(globalAbsOffset, curLen); 
                ComputeCtr(curLen); 
                CopyOut(globalAbsOffset, curLen);
            }

            offsetInCore += curLen;
            remain -= curLen;
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
    // Mode 3: Mask Encryption (Add)
    // Out = In + Mask
    // -----------------------------------------------------------
    __aicore__ inline void ProcessMaskAdd(uint32_t globalOffset, uint32_t len)
    {

        AscendC::LocalTensor<half> inputLocal = inQueueInput.AllocTensor<half>();
        AscendC::LocalTensor<half> maskLocal = precompKeyQueue.AllocTensor<half>();
        AscendC::LocalTensor<half> outputLocal = outQueueOutput.AllocTensor<half>();


        uint32_t elemOffset = globalOffset / 2;
        uint32_t elemLen = len / 2;

        AscendC::DataCopy(inputLocal, inputGmHalf[elemOffset], elemLen);
        AscendC::DataCopy(maskLocal, maskGmHalf[elemOffset], elemLen);
        
        inQueueInput.EnQue(inputLocal);
        precompKeyQueue.EnQue(maskLocal);

        AscendC::LocalTensor<half> tIn = inQueueInput.DeQue<half>();
        AscendC::LocalTensor<half> tMask = precompKeyQueue.DeQue<half>();
        
        // 核心：向量加法
        AscendC::Add(outputLocal, tIn, tMask, elemLen);

        outQueueOutput.EnQue(outputLocal);
        
        inQueueInput.FreeTensor(tIn);
        precompKeyQueue.FreeTensor(tMask);

        AscendC::LocalTensor<half> tOut = outQueueOutput.DeQue<half>();
        AscendC::DataCopy(outputGmHalf[elemOffset], tOut, elemLen);
        outQueueOutput.FreeTensor(tOut);
    }

    // -----------------------------------------------------------
    // Mode 4: Mask Decryption (Sub)
    // Out = In - (RankSize * Mask)
    // -----------------------------------------------------------
    __aicore__ inline void ProcessMaskSub(uint32_t globalOffset, uint32_t len, int32_t rankSize)
    {
        AscendC::LocalTensor<half> inputLocal = inQueueInput.AllocTensor<half>();
        AscendC::LocalTensor<half> maskLocal = precompKeyQueue.AllocTensor<half>();
        AscendC::LocalTensor<half> outputLocal = outQueueOutput.AllocTensor<half>();

        uint32_t elemOffset = globalOffset / 2;
        uint32_t elemLen = len / 2;

        AscendC::DataCopy(inputLocal, inputGmHalf[elemOffset], elemLen);
        AscendC::DataCopy(maskLocal, maskGmHalf[elemOffset], elemLen);
        
        inQueueInput.EnQue(inputLocal);
        precompKeyQueue.EnQue(maskLocal);

        AscendC::LocalTensor<half> tIn = inQueueInput.DeQue<half>();
        AscendC::LocalTensor<half> tMask = precompKeyQueue.DeQue<half>();
        
        // 1. 放大 Mask: tMask = tMask * rankSize
        // 使用 Muls (Scalar Multiply)
        AscendC::Muls(tMask, tMask, (half)rankSize, elemLen);

        // 2. 减法: Out = In - ScaledMask
        AscendC::Sub(outputLocal, tIn, tMask, elemLen);

        outQueueOutput.EnQue(outputLocal);
        
        inQueueInput.FreeTensor(tIn);
        precompKeyQueue.FreeTensor(tMask);

        AscendC::LocalTensor<half> tOut = outQueueOutput.DeQue<half>();
        AscendC::DataCopy(outputGmHalf[elemOffset], tOut, elemLen);
        outQueueOutput.FreeTensor(tOut);
    }

    // -----------------------------------------------------------
    // Legacy Code (AES / Checksum)
    // -----------------------------------------------------------
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
        
        uint32_t len16 = len / 2;
        AscendC::Xor(tOut16, tIn16, tKey16, len16);

        outQueueOutput.EnQue(outputLocal);
        
        inQueueInput.FreeTensor(tIn);
        precompKeyQueue.FreeTensor(tKey);

        AscendC::LocalTensor<uint8_t> tOut = outQueueOutput.DeQue<uint8_t>();
        AscendC::DataCopy(outputGm[globalOffset], tOut, len);
        outQueueOutput.FreeTensor(tOut);
    }

    __aicore__ inline void T_keygen(AscendC::LocalTensor<uint8_t>& W,uint32_t round)
    {
        uint8_t tmp = W(0);
        W(0) = W(1); W(1) = W(2); W(2) = W(3); W(3) = tmp;
        for(uint8_t i = 0;i < 4;i++) W(i) = sbox[W(i)];
        W(0) = W(0) ^ rcon[round];
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

    __aicore__ inline void CopyIn(uint32_t globalOffset, uint32_t len)
    {
        AscendC::LocalTensor<uint8_t> inputLocal = inQueueInput.AllocTensor<uint8_t>();
        AscendC::DataCopy(inputLocal, inputGm[globalOffset], len);
        inQueueInput.EnQue<uint8_t>(inputLocal);
    }

    __aicore__ inline void CopyIvToLocal(uint32_t globalByteOffset)
    {
        AscendC::LocalTensor<uint8_t> ivLocal = ivBuf.AllocTensor<uint8_t>();
        AscendC::DataCopy(ivLocal, ivGm[0], 32);

        uint32_t blockOffset = globalByteOffset / 16;
        uint32_t counter = (uint32_t)ivLocal(12) << 24 | (uint32_t)ivLocal(13) << 16 | (uint32_t)ivLocal(14) << 8  | (uint32_t)ivLocal(15);
        counter += blockOffset;
        ivLocal(12) = (counter >> 24) & 0xFF; ivLocal(13) = (counter >> 16) & 0xFF;
        ivLocal(14) = (counter >> 8) & 0xFF; ivLocal(15) = counter & 0xFF;

        ivBuf.EnQue<uint8_t>(ivLocal);
    }
    
    __aicore__ inline void ComputeCtr(uint32_t len)
    {
        AscendC::LocalTensor<uint8_t> roundKeysLocal = roundKeysBuf.DeQue<uint8_t>();
        AscendC::LocalTensor<uint8_t> ivLocal = ivBuf.DeQue<uint8_t>();
        AscendC::LocalTensor<uint8_t> outputLocal = outQueueOutput.AllocTensor<uint8_t>();
        AscendC::LocalTensor<uint8_t> keyStreamBlock = keyStreamBuf.AllocTensor<uint8_t>();
        
        AscendC::LocalTensor<uint8_t> inputLocal;
        if (mode == 0) {
            inputLocal = inQueueInput.DeQue<uint8_t>();
            CalcChecksumLocal(inputLocal, len);
        }

        int32_t currentBlocks = len / BLOCK_SIZE;
        for (uint32_t blockIdx = 0; blockIdx < currentBlocks; blockIdx++) {
            
            for (int j = 0; j < BLOCK_SIZE; j++) keyStreamBlock(j) = ivLocal(j);
            AddRoundKey(keyStreamBlock, roundKeysLocal[0]);
            
            for (int round = 1; round < 10; round++) {
                SubBytes(keyStreamBlock); ShiftRows(keyStreamBlock);
                MixColumns(keyStreamBlock); AddRoundKey(keyStreamBlock, roundKeysLocal[round * 16]);
            }
            SubBytes(keyStreamBlock); ShiftRows(keyStreamBlock);
            AddRoundKey(keyStreamBlock, roundKeysLocal[10 * 16]);
    
            for (int j = 0; j < 16; j++) {
                if (mode == 0) {
                    outputLocal(blockIdx * BLOCK_SIZE + j) = inputLocal(blockIdx * BLOCK_SIZE + j) ^ keyStreamBlock(j);
                } else {
                    outputLocal(blockIdx * BLOCK_SIZE + j) = keyStreamBlock(j);
                }
            }
            
            if (++ivLocal(15) == 0) { if (++ivLocal(14) == 0) { if (++ivLocal(13) == 0) ++ivLocal(12); } }
        }
        
        outQueueOutput.EnQue<uint8_t>(outputLocal);
        roundKeysBuf.EnQue<uint8_t>(roundKeysLocal);
        ivBuf.EnQue<uint8_t>(ivLocal); 
        
        if (mode == 0) inQueueInput.FreeTensor(inputLocal);
        keyStreamBuf.FreeTensor(keyStreamBlock); 
    }

    __aicore__ inline void CopyOut(uint32_t globalOffset, uint32_t len)
    {
        AscendC::LocalTensor<uint8_t> outputLocal = outQueueOutput.DeQue<uint8_t>();
        AscendC::DataCopy(outputGm[globalOffset], outputLocal, len);
        outQueueOutput.FreeTensor(outputLocal);
    }
    
    __aicore__ inline void SubBytes(AscendC::LocalTensor<uint8_t>& state) { for (int i = 0; i < 16; i++) state(i) = sbox[state(i)]; }
    __aicore__ inline void ShiftRows(AscendC::LocalTensor<uint8_t>& state) { uint8_t temp; temp = state(1); state(1) = state(5); state(5) = state(9); state(9) = state(13); state(13) = temp; temp = state(2); state(2) = state(10); state(10) = temp; temp = state(6); state(6) = state(14); state(14) = temp; temp = state(15); state(15) = state(11); state(11) = state(7); state(7) = state(3); state(3) = temp; }
    __aicore__ inline void MixColumns(AscendC::LocalTensor<uint8_t>& state) { for (int i = 0; i < 4; i++) { uint8_t a0 = state(4 * i), a1 = state(4 * i + 1), a2 = state(4 * i + 2), a3 = state(4 * i + 3); state(4 * i) = gmul2[a0] ^ gmul3[a1] ^ a2 ^ a3; state(4 * i + 1) = a0 ^ gmul2[a1] ^ gmul3[a2] ^ a3; state(4 * i + 2) = a0 ^ a1 ^ gmul2[a2] ^ gmul3[a3]; state(4 * i + 3) = gmul3[a0] ^ a1 ^ a2 ^ gmul2[a3]; } }
    __aicore__ inline void AddRoundKey(AscendC::LocalTensor<uint8_t>& state, const AscendC::LocalTensor<uint8_t>& roundKey) { for (int i = 0; i < 16; i++) state(i) = state(i) ^ roundKey(i); }

    private:
    TPipe pipe;
    TQue<QuePosition::VECIN, BUFFER_NUM> inQueueInput;
    TQue<QuePosition::VECOUT, BUFFER_NUM> outQueueOutput;
    
    TQue<QuePosition::VECIN, 1> roundKeysBuf;
    TQue<QuePosition::VECIN, 1> tempBuf;
    TQue<QuePosition::VECIN, 1> ivBuf; 
    TQue<QuePosition::VECIN, BUFFER_NUM> keyStreamBuf; 
    
    TQue<QuePosition::VECIN, BUFFER_NUM> precompKeyQueue;
    TQue<QuePosition::VECIN, 1> rankBuf; 
    
    TQue<QuePosition::VECIN, 1> checksumBuf;

    GlobalTensor<uint8_t> inputGm;
    GlobalTensor<uint8_t> keyGm;      
    GlobalTensor<uint8_t> keyStreamGm;
    GlobalTensor<uint8_t> ivGm; 
    GlobalTensor<uint8_t> outputGm;
    GlobalTensor<uint8_t> integrityGm;
    
    // [New] Mask Global Buffers (FP16)
    GlobalTensor<half> inputGmHalf;
    GlobalTensor<half> maskGmHalf;
    GlobalTensor<half> outputGmHalf;
    GlobalTensor<int32_t> rankGm;

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
    // 使用 GetBlockNum() 获取实际启动的核数
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