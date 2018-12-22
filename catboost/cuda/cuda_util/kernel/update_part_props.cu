#include "update_part_props.cuh"
#include "fill.cuh"
#include <catboost/cuda/cuda_lib/kernel/arch.cuh>
#include <catboost/cuda/cuda_util/kernel/kernel_helpers.cuh>
#include <catboost/cuda/cuda_util/gpu_data/partitions.h>


namespace NKernel {



    template <int BlockSize>
    __forceinline__ __device__  double ComputeSum(const float* __restrict__ stat, ui32 offset, ui32 size, int blockIdx, int blockCount) {

        float4 sum;
        sum.x =  sum.y = sum.z = sum.w = 0;

        stat += offset;

        const int warpSize = 32;
        const int alignSize = 4 * warpSize;

        {
            int lastId = min(size, alignSize - (offset % alignSize));

            if (blockIdx == 0) {
                if (threadIdx.x < lastId) {
                    sum.x += Ldg(stat + threadIdx.x);
                }
            }

            size = max(size - lastId, 0);
            stat += lastId;
        }

        //now lets align end
        const int unalignedTail = (size % alignSize);

        if (unalignedTail != 0) {
            if (blockIdx == 0) {
                const int tailOffset = size - unalignedTail;
                if (threadIdx.x < unalignedTail) {
                    sum.y += Ldg(stat + tailOffset + threadIdx.x);
                }
            }
        }
        size -= unalignedTail;

        const int entriesPerWarp = warpSize * 4;
        const int warpsPerBlock = (BlockSize / 32);
        const int globalWarpId = (blockIdx * warpsPerBlock) + (threadIdx.x / 32);
        stat += globalWarpId * entriesPerWarp;
        size = max(size - globalWarpId * entriesPerWarp, 0);

        const int stripeSize = entriesPerWarp * warpsPerBlock * blockCount;

        const int localIdx = (threadIdx.x & 31) * 4;
        const int iterCount = (size - localIdx + stripeSize - 1)  / stripeSize;

        stat += localIdx;

        if (size > 0) {
            for (int i = 0; i < iterCount; ++i) {
                const float4* stat4 = (const float4*) stat;
                float4 val = Ldg(stat4);
                sum.x += val.x;
                sum.y += val.y;
                sum.z += val.z;
                sum.w += val.w;
                stat += stripeSize;
            }
        }

        return (double)sum.x + (double)sum.y + (double)sum.z + (double)sum.w;
    };


    template <class TOutput>
    __global__ void SaveResultsImpl(const ui32* partIds,
                                    const double* tempVars,
                                    ui32 partCount,
                                    ui32 statCount,
                                    int tempVarsBlockCount,
                                    TOutput* statSums) {
        const ui32 i = blockIdx.x * blockDim.x + threadIdx.x;
        const ui32 statId = i % statCount;
        const ui32 y =  i / statCount;
        if (i < partCount * statCount) {
            const ui32 leafId = partIds != nullptr ? partIds[y] : y;
            double total = 0;
            for (int x = 0; x < tempVarsBlockCount; ++x) {
                total += tempVars[i];
                tempVars += statCount * partCount;
            }
            statSums[leafId * statCount + statId] = total;
        }
    }


    template <int BlockSize>
    __launch_bounds__(BlockSize, 2)
    __global__ void UpdatePartitionsPropsForOffsetsImpl(const ui32* offsets,
                                                        const float* source,
                                                        ui64 statLineSize,
                                                        double* statPartSums) {

        const ui32 partOffset = __ldg(offsets + blockIdx.y);
        const ui32 partSize = __ldg(offsets + blockIdx.y + 1) - partOffset;
        const ui32 statId = blockIdx.z;

        __shared__ volatile double localBuffer[BlockSize];
        source += statId * statLineSize;

        const int minDocsPerBlock = BlockSize * 16;
        const int effectiveBlockCount = min(gridDim.x, (partSize + minDocsPerBlock - 1) / minDocsPerBlock);

        double result = 0;

        if (blockIdx.x < effectiveBlockCount) {
            const int blockId = blockIdx.x % effectiveBlockCount;

            localBuffer[threadIdx.x] = ComputeSum <BlockSize> (source, partOffset, partSize, blockId, effectiveBlockCount);
            __syncthreads();

            result = FastInBlockReduce(threadIdx.x, localBuffer, BlockSize);
        }

        if (threadIdx.x == 0) {
            const int statCount = gridDim.z;
            const int partCount = gridDim.y;
            const int lineSize = statCount * partCount;
            ui64 idx = blockIdx.x * lineSize + blockIdx.y * statCount + statId;
            statPartSums[idx]  = result;
        }
    }

        template <int BlockSize>
    __launch_bounds__(BlockSize, 2)
    __global__ void UpdatePartitionsPropsImpl(const ui32* partIds,
                                              const TDataPartition* parts,
                                              const float* source,
                                              ui64 statLineSize,
                                              double* tempVars) {
        const ui32 leafId = partIds[blockIdx.y];
        TDataPartition part = parts[leafId];

        const ui32 statId = blockIdx.z;

        __shared__ volatile double localBuffer[BlockSize];
        source += statId * statLineSize;


        const int minDocsPerBlock = BlockSize * 16;
        const int effectiveBlockCount = min(gridDim.x, (part.Size + minDocsPerBlock - 1) / minDocsPerBlock);

        double result = 0;

        if (blockIdx.x < effectiveBlockCount) {
            const int blockId = blockIdx.x % effectiveBlockCount;

            localBuffer[threadIdx.x] = ComputeSum < BlockSize > (source, part.Offset, part.Size, blockId, effectiveBlockCount);
            __syncthreads();

            result = FastInBlockReduce(threadIdx.x, localBuffer, BlockSize);
        }

        if (threadIdx.x == 0) {
            tempVars[gridDim.z * gridDim.y * blockIdx.x + blockIdx.y * gridDim.z + blockIdx.z] = result;
        }
    }


    void UpdatePartitionsProps(const TDataPartition* parts,
                               const ui32* partIds,
                               ui32 partCount,
                               const float* source,
                               ui32 statCount,
                               ui64 statLineSize,
                               ui32 tempVarsCount,
                               double* tempVars,
                               double* statSums,
                               TCudaStream stream
    ) {

        const ui32 blockSize = 512;

        dim3 numBlocks;

        numBlocks.y = partCount;
        numBlocks.z = statCount;
        numBlocks.x = CeilDivide(2 * TArchProps::SMCount(), (int)statCount);
        Y_VERIFY(numBlocks.x * numBlocks.y * numBlocks.z <= tempVarsCount);

        UpdatePartitionsPropsImpl<blockSize><<<numBlocks, blockSize, 0, stream>>>(partIds, parts, source, statLineSize, tempVars);
        {
            const ui32 saveBlockSize = 256;
            const ui32 numSaveBlocks = (numBlocks.y * numBlocks.z + saveBlockSize - 1) / saveBlockSize;
            SaveResultsImpl<<<numSaveBlocks, saveBlockSize, 0, stream>>>(partIds, tempVars, partCount, statCount, numBlocks.x, statSums);
        }
    }


    void UpdatePartitionsPropsForSplit(const TDataPartition* parts,
                                       const ui32* leftPartIds,
                                       const ui32* rightPartIds,
                                       ui32 partCount,
                                       const float* source,
                                       ui32 statCount,
                                       ui64 statLineSize,
                                       ui32 tempVarsCount,
                                       double* tempVars,
                                       double* statSums,
                                       TCudaStream stream) {
        //TODO(noxoomo): if it'll be "slow", could be made in one kernel
        UpdatePartitionsProps(parts, leftPartIds, partCount, source, statCount, statLineSize, tempVarsCount, tempVars, statSums, stream);
        UpdatePartitionsProps(parts, rightPartIds, partCount, source, statCount, statLineSize, tempVarsCount, tempVars, statSums, stream);
    }



    void UpdatePartitionsPropsForOffsets(const ui32* offsets,
                                         ui32 count,
                                         const float* source,
                                         ui32 statCount,
                                         ui64 statLineSize,
                                         ui32 tempVarsCount,
                                         double* tempVars,
                                         double* statSums,
                                         TCudaStream stream
    ) {
        const ui32 blockSize = 512;

        dim3 numBlocks;

        numBlocks.y = count;
        numBlocks.z = statCount;
        numBlocks.x = CeilDivide(2 * TArchProps::SMCount(), (int)statCount);
        Y_VERIFY(numBlocks.x * numBlocks.y * numBlocks.z <= tempVarsCount);

        UpdatePartitionsPropsForOffsetsImpl<blockSize><<<numBlocks, blockSize, 0, stream>>>(offsets, source,  statLineSize, tempVars);
        {
            const ui32 saveBlockSize = 256;
            const ui32 numSaveBlocks = (count * statCount + saveBlockSize - 1) / saveBlockSize;
            SaveResultsImpl<<<numSaveBlocks, saveBlockSize, 0, stream>>>(nullptr, tempVars, count, statCount, numBlocks.x, statSums);
        }
    }


    __global__ void FloatToDoubleImpl(const float* src, ui32 size, double* dst) {

        const ui32 i = blockIdx.x * blockDim.x + threadIdx.x;
        if (i < size) {
            WriteThrough(dst + i, (double)__ldg(src + i));
        }
    }

    void CopyFloatToDouble(const float* src, ui32 size, double* dst, TCudaStream stream) {

        const ui32 blockSize = 128;
        const ui32 numBlocks = CeilDivide(size, blockSize);
        if (numBlocks) {
            FloatToDoubleImpl<<<numBlocks, blockSize, 0, stream>>>(src, size, dst);
        }
    }

    ui32 GetTempVarsCount(ui32 statCount, ui32 count) {
        return CeilDivide(2 * TArchProps::SMCount(), (int)statCount) * statCount * count;
    }
}
