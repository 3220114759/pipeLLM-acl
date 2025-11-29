#include "kernel_operator.h" 
#include <stdio.h>
using namespace AscendC; 

constexpr int32_t USE_CORE_NUM = 1;
constexpr int32_t TILE_NUM = 8;     
constexpr int32_t BUFFER_NUM = 2;    
constexpr int32_t BLOCK_SIZE = 16; // AES块大小
constexpr int32_t KEY_ROUNDS = 11; // AES-128的轮密钥数量

// AES S-Box
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
    
    __aicore__ inline void Init(GM_ADDR input, GM_ADDR key, GM_ADDR iv, GM_ADDR output, 
                                 uint32_t totalLength, uint32_t mode)
    {

        int32_t blockLengthInBytes = totalLength / USE_CORE_NUM;
        int32_t tileLengthInBytes = blockLengthInBytes / TILE_NUM / BUFFER_NUM;

        if (tileLengthInBytes % BLOCK_SIZE != 0) {
            tileLengthInBytes = (tileLengthInBytes / BLOCK_SIZE) * BLOCK_SIZE;
        }

        this->loopCount = TILE_NUM * BUFFER_NUM;
        this->tileLengthInBytes = tileLengthInBytes;
        this->tileLengthInAesBlocks = tileLengthInBytes / BLOCK_SIZE;
        this->totalBlockNum = blockLengthInBytes / BLOCK_SIZE;

        inputGm.SetGlobalBuffer((__gm__ uint8_t *)input + blockLengthInBytes * GetBlockIdx(), blockLengthInBytes);
        keyGm.SetGlobalBuffer((__gm__ uint8_t *)key, 32);
        ivGm.SetGlobalBuffer((__gm__ uint8_t *)iv, 32); 
        outputGm.SetGlobalBuffer((__gm__ uint8_t *)output + blockLengthInBytes * GetBlockIdx(), blockLengthInBytes);
        
        pipe.InitBuffer(inQueueInput, BUFFER_NUM, this->tileLengthInBytes);
        pipe.InitBuffer(outQueueOutput, BUFFER_NUM, this->tileLengthInBytes);
        
        pipe.InitBuffer(tempBuf, 1, 4); 
        pipe.InitBuffer(roundKeysBuf, 1, KEY_ROUNDS * BLOCK_SIZE);
        pipe.InitBuffer(ivBuf, 1, 32);
        pipe.InitBuffer(keyStreamBuf, BUFFER_NUM, 32); 
        
        this->mode = mode;
    }
    

    __aicore__ inline void Process()
    {
        if (this->totalBlockNum == 0 || this->loopCount == 0) {
            return; 
        }

        KeyExpansion();
        CopyIvToLocal(); //拷贝和设置IV
        // AscendC::printf("finish copy\n"); 
        
        for (int32_t i = 0; i < this->loopCount; i++) {
            CopyIn(i);
            ComputeCtr(i); 
            CopyOut(i);
        }


        AscendC::LocalTensor<uint8_t> finalRoundKeys = roundKeysBuf.DeQue<uint8_t>();
        roundKeysBuf.FreeTensor(finalRoundKeys);

        AscendC::LocalTensor<uint8_t> finalIv = ivBuf.DeQue<uint8_t>();
        ivBuf.FreeTensor(finalIv);
    }

    private:

    __aicore__ inline void T_keygen(AscendC::LocalTensor<uint8_t>& W,uint32_t round)
    {
        uint8_t tmp = W(0);
        W(0) = W(1);
        W(1) = W(2);
        W(2) = W(3);
        W(3) = tmp;
        for(uint8_t i = 0;i < 4;i++) {
            W(i) = sbox[W(i)];
        }
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
            {
                roundKeysLocal(i) = roundKeysLocal(i-16) ^ prekey(j);
            }
            for(uint16_t i = round * 16 + 4;i < round * 16 + 16;i++)
            {
                roundKeysLocal(i) = roundKeysLocal(i-4) ^ roundKeysLocal(i-16);
            }
        }
        
        roundKeysBuf.EnQue<uint8_t>(roundKeysLocal);
        tempBuf.FreeTensor(prekey);
    }

    __aicore__ inline void CopyIn(int32_t progress)
    {
        AscendC::LocalTensor<uint8_t> inputLocal = inQueueInput.AllocTensor<uint8_t>();
        

        int32_t offsetInBytes = progress * this->tileLengthInBytes;
        AscendC::DataCopy(inputLocal, inputGm[offsetInBytes], this->tileLengthInBytes);
        
        inQueueInput.EnQue<uint8_t>(inputLocal);
    }

    __aicore__ inline void CopyIvToLocal()
    {
        AscendC::LocalTensor<uint8_t> ivLocal = ivBuf.AllocTensor<uint8_t>();
        AscendC::DataCopy(ivLocal, ivGm[0], 32);

        uint32_t coreStartBlockOffset = AscendC::GetBlockIdx() * this->totalBlockNum;

        // 将此偏移量添加到IV的计数器部分
        uint32_t counter = (uint32_t)ivLocal(12) << 24 | 
                           (uint32_t)ivLocal(13) << 16 | 
                           (uint32_t)ivLocal(14) << 8  | 
                           (uint32_t)ivLocal(15);
        
        counter += coreStartBlockOffset;

        ivLocal(12) = (counter >> 24) & 0xFF;
        ivLocal(13) = (counter >> 16) & 0xFF;
        ivLocal(14) = (counter >> 8) & 0xFF;
        ivLocal(15) = counter & 0xFF;

        ivBuf.EnQue<uint8_t>(ivLocal);
    }
    
    __aicore__ inline void ComputeCtr(int32_t progress)
    {
        AscendC::LocalTensor<uint8_t> inputLocal = inQueueInput.DeQue<uint8_t>();
        AscendC::LocalTensor<uint8_t> roundKeysLocal = roundKeysBuf.DeQue<uint8_t>();
        AscendC::LocalTensor<uint8_t> ivLocal = ivBuf.DeQue<uint8_t>();
        AscendC::LocalTensor<uint8_t> outputLocal = outQueueOutput.AllocTensor<uint8_t>();
        AscendC::LocalTensor<uint8_t> keyStreamBlock = keyStreamBuf.AllocTensor<uint8_t>();
        
        int32_t currentBlocks = this->tileLengthInAesBlocks;
        
        for (uint32_t blockIdx = 0; blockIdx < currentBlocks; blockIdx++) {
            
            for (int j = 0; j < BLOCK_SIZE; j++) {
                keyStreamBlock(j) = ivLocal(j);
            }

            AddRoundKey(keyStreamBlock, roundKeysLocal[0]);
            
            for (int round = 1; round < 10; round++) {
                SubBytes(keyStreamBlock);
                ShiftRows(keyStreamBlock);
                MixColumns(keyStreamBlock);
                AddRoundKey(keyStreamBlock, roundKeysLocal[round * 16]);
            }
            
            SubBytes(keyStreamBlock);
            ShiftRows(keyStreamBlock);
            AddRoundKey(keyStreamBlock, roundKeysLocal[10 * 16]);
    
            if (this->mode == 2) {
                //解密 -> 计算 -> 重加密
                for (int j = 0; j < 16; j++) {
                    //解密
                    uint8_t decryptedByte = inputLocal(blockIdx * BLOCK_SIZE + j) ^ keyStreamBlock(j);
                    //计算
                    uint8_t computedByte = decryptedByte + 1; 
                    //加密
                    outputLocal(blockIdx * BLOCK_SIZE + j) = computedByte ^ keyStreamBlock(j);
                }
            } else {
                // (mode 0, 1)
                for (int j = 0; j < 16; j++) {
                    outputLocal(blockIdx * BLOCK_SIZE + j) = 
                        inputLocal(blockIdx * BLOCK_SIZE + j) ^ keyStreamBlock(j);
                }
            }
            
            if (++ivLocal(15) == 0) { 
                if (++ivLocal(14) == 0) {
                    if (++ivLocal(13) == 0) {
                        ++ivLocal(12);
                    }
                }
            }
        }
        
        outQueueOutput.EnQue<uint8_t>(outputLocal);
        roundKeysBuf.EnQue<uint8_t>(roundKeysLocal);
        ivBuf.EnQue<uint8_t>(ivLocal); 
        inQueueInput.FreeTensor(inputLocal);
        keyStreamBuf.FreeTensor(keyStreamBlock); 
    }
    
    __aicore__ inline void CopyOut(int32_t progress)
    {
        AscendC::LocalTensor<uint8_t> outputLocal = outQueueOutput.DeQue<uint8_t>();

        int32_t offsetInBytes = progress * this->tileLengthInBytes;
        AscendC::DataCopy(outputGm[offsetInBytes], outputLocal, this->tileLengthInBytes);

        outQueueOutput.FreeTensor(outputLocal);
    }
    
    __aicore__ inline void SubBytes(AscendC::LocalTensor<uint8_t>& state)
    {
        for (int i = 0; i < 16; i++) {
            state(i) = sbox[state(i)];
        }
    }
    
    __aicore__ inline void ShiftRows(AscendC::LocalTensor<uint8_t>& state)
    {
        uint8_t temp;
        temp = state(1); state(1) = state(5); state(5) = state(9); state(9) = state(13); state(13) = temp;
        temp = state(2); state(2) = state(10); state(10) = temp;
        temp = state(6); state(6) = state(14); state(14) = temp;
        temp = state(15); state(15) = state(11); state(11) = state(7); state(7) = state(3); state(3) = temp;
    }
    
    __aicore__ inline void MixColumns(AscendC::LocalTensor<uint8_t>& state)
    {
        for (int i = 0; i < 4; i++) {
            uint8_t a0 = state(4 * i);
            uint8_t a1 = state(4 * i + 1);
            uint8_t a2 = state(4 * i + 2);
            uint8_t a3 = state(4 * i + 3);
            state(4 * i)     = gmul2[a0] ^ gmul3[a1] ^ a2       ^ a3;
            state(4 * i + 1) = a0       ^ gmul2[a1] ^ gmul3[a2] ^ a3;
            state(4 * i + 2) = a0       ^ a1       ^ gmul2[a2] ^ gmul3[a3];
            state(4 * i + 3) = gmul3[a0] ^ a1       ^ a2       ^ gmul2[a3];
        }
    }

    __aicore__ inline void AddRoundKey(AscendC::LocalTensor<uint8_t>& state, 
                                     const AscendC::LocalTensor<uint8_t>& roundKey)
    {
        for (int i = 0; i < 16; i++) {
            state(i) = state(i) ^ roundKey(i);
        }
    }

    private:
    TPipe pipe;
    TQue<QuePosition::VECIN, BUFFER_NUM> inQueueInput;
    TQue<QuePosition::VECOUT, BUFFER_NUM> outQueueOutput;
    TQue<QuePosition::VECIN, 1> roundKeysBuf;
    TQue<QuePosition::VECIN, 1> tempBuf;
    TQue<QuePosition::VECIN, 1> ivBuf; 
    TQue<QuePosition::VECIN, BUFFER_NUM> keyStreamBuf; 

    GlobalTensor<uint8_t> inputGm;
    GlobalTensor<uint8_t> keyGm;
    GlobalTensor<uint8_t> ivGm; 
    GlobalTensor<uint8_t> outputGm;
    
    uint32_t mode;
    
    int32_t loopCount;
    int32_t tileLengthInBytes;
    int32_t tileLengthInAesBlocks;
    uint32_t totalBlockNum; 
};



#ifndef ASCENDC_CPU_DEBUG
extern "C" __global__ __aicore__ void aes_cipher_kernel_npu(GM_ADDR input_x, GM_ADDR key, GM_ADDR iv, GM_ADDR output,uint32_t totalLength,uint32_t mode)
{
    AesKernel encrypt;
    // AscendC::printf("enter kernel!!!\n");
    encrypt.Init(input_x, key, iv, output, totalLength, mode); 
    encrypt.Process();
}
#else
extern "C" __global__ __aicore__ void aes_cipher_kernel_cpu(GM_ADDR input_x, GM_ADDR key, GM_ADDR iv, GM_ADDR output,uint32_t totalLength,uint32_t mode)
{
    AesKernel encrypt;
    encrypt.Init(input_x, key, iv, output, totalLength, mode); 
    double time1= AscendC::GetSystemCycle();
    encrypt.Process();
    AscendC::printf("用时%fms\n",(AscendC::GetSystemCycle()-time1)/50/1000);
}
#endif

#ifndef ASCENDC_CPU_DEBUG
void add_custom_do(uint32_t blockDim, void *stream, uint8_t *x, uint8_t *y, uint8_t *iv, uint8_t *z,uint32_t totallength,uint32_t mode)
{
    // printf("enter add_custom_do!!\n");
    aes_cipher_kernel_npu<<<blockDim,nullptr,stream>>>(x, y, iv, z, totallength, mode); 
}
#endif