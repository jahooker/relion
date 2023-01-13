#ifndef CUDA_UTILS_CUB_CUH_
#define CUDA_UTILS_CUB_CUH_

#include <cuda_runtime.h>
#include "src/acc/cuda/cuda_settings.h"
#include "src/acc/cuda/cuda_mem_utils.h"
#include <stdio.h>
#include <signal.h>
#include <vector>
// Because thrust uses CUB, thrust defines CubLog and CUB tries to redefine it,
// resulting in warnings. This avoids those warnings.
#if(defined(CubLog) && defined(__CUDA_ARCH__) && (__CUDA_ARCH__<= 520)) // Intetionally force a warning for new arch
    #undef CubLog
#endif

#define CUB_NS_QUALIFIER ::cub # for compatibility with CUDA 11.5
#include "src/acc/cuda/cub/device/device_radix_sort.cuh"
#include "src/acc/cuda/cub/device/device_reduce.cuh"
#include "src/acc/cuda/cub/device/device_scan.cuh"
#include "src/acc/cuda/cub/device/device_select.cuh"

namespace CudaKernels
{
template <typename T>
static std::pair<int, T> getArgMaxOnDevice(AccPtr<T> &ptr)
{
#ifdef DEBUG_CUDA
if (ptr.getSize() == 0)
    printf("DEBUG_WARNING: getArgMaxOnDevice called with pointer of zero size.\n");
if (ptr.getDevicePtr() == nullptr)
    printf("DEBUG_WARNING: getArgMaxOnDevice called with null device pointer.\n");
if (ptr.getAllocator() == nullptr)
    printf("DEBUG_WARNING: getArgMaxOnDevice called with null allocator.\n");
#endif
    AccPtr<cub::KeyValuePair<int, T> >  max_pair(1, ptr.getAllocator(), ptr.getStream());
    max_pair.deviceAlloc();
    size_t temp_storage_size = 0;

    DEBUG_HANDLE_ERROR(cub::DeviceReduce::ArgMax( nullptr, temp_storage_size, ptr.getAccPtr(), max_pair.getAccPtr(), ptr.getSize()));

    if(temp_storage_size==0)
        temp_storage_size=1;

    CudaCustomAllocator::Alloc* alloc = ptr.getAllocator()->alloc(temp_storage_size);

    DEBUG_HANDLE_ERROR(cub::DeviceReduce::ArgMax( alloc->getPtr(), temp_storage_size, ptr.getAccPtr(), max_pair.getAccPtr(), ptr.getSize(), ptr.getStream()));

    max_pair.cpToHost();
    ptr.streamSync();

    ptr.getAllocator()->free(alloc);

    return {max_pair.getHostPtr()->key, max_pair.getHostPtr()->value};
}

template <typename T>
static std::pair<int, T> getArgMinOnDevice(AccPtr<T> &ptr)
{
#ifdef DEBUG_CUDA
if (ptr.getSize() == 0)
    printf("DEBUG_WARNING: getArgMinOnDevice called with pointer of zero size.\n");
if (ptr.getDevicePtr() == nullptr)
    printf("DEBUG_WARNING: getArgMinOnDevice called with null device pointer.\n");
if (ptr.getAllocator() == nullptr)
    printf("DEBUG_WARNING: getArgMinOnDevice called with null allocator.\n");
#endif
    AccPtr<cub::KeyValuePair<int, T> >  min_pair(1, ptr.getAllocator(), ptr.getStream());
    min_pair.deviceAlloc();
    size_t temp_storage_size = 0;

    DEBUG_HANDLE_ERROR(cub::DeviceReduce::ArgMin( nullptr, temp_storage_size, ptr.getAccPtr(), min_pair.getAccPtr(), ptr.getSize()));

    if (temp_storage_size == 0) temp_storage_size = 1;

    CudaCustomAllocator::Alloc* alloc = ptr.getAllocator()->alloc(temp_storage_size);

    DEBUG_HANDLE_ERROR(cub::DeviceReduce::ArgMin( alloc->getPtr(), temp_storage_size, ptr.getAccPtr(), min_pair.getAccPtr(), ptr.getSize(), ptr.getStream()));

    min_pair.cpToHost();
    ptr.streamSync();

    ptr.getAllocator()->free(alloc);

    return {min_pair.getHostPtr()->key, min_pair.getHostPtr()->value};
}

template <typename T>
static T getMaxOnDevice(AccPtr<T> &ptr)
{
#ifdef DEBUG_CUDA
if (ptr.getSize() == 0)
    printf("DEBUG_ERROR: getMaxOnDevice called with pointer of zero size.\n");
if (ptr.getDevicePtr() == nullptr)
    printf("DEBUG_ERROR: getMaxOnDevice called with null device pointer.\n");
if (ptr.getAllocator() == nullptr)
    printf("DEBUG_ERROR: getMaxOnDevice called with null allocator.\n");
#endif
    AccPtr<T>  max_val(1, ptr.getAllocator(), ptr.getStream());
    max_val.deviceAlloc();
    size_t temp_storage_size = 0;

    DEBUG_HANDLE_ERROR(cub::DeviceReduce::Max( nullptr, temp_storage_size, ptr.getAccPtr(), max_val.getAccPtr(), ptr.getSize()));

    if (temp_storage_size == 0) temp_storage_size = 1;

    CudaCustomAllocator::Alloc* alloc = ptr.getAllocator()->alloc(temp_storage_size);

    DEBUG_HANDLE_ERROR(cub::DeviceReduce::Max( alloc->getPtr(), temp_storage_size, ptr.getAccPtr(), max_val.getAccPtr(), ptr.getSize(), ptr.getStream()));

    max_val.cpToHost();
    ptr.streamSync();

    ptr.getAllocator()->free(alloc);

    return *max_val.getHostPtr();
}

template <typename T>
static T getMinOnDevice(AccPtr<T> &ptr) {
    #ifdef DEBUG_CUDA
    if (ptr.getSize() == 0)
        printf("DEBUG_ERROR: getMinOnDevice called with pointer of zero size.\n");
    if (!ptr.getDevicePtr())
        printf("DEBUG_ERROR: getMinOnDevice called with null device pointer.\n");
    if (!ptr.getAllocator())
        printf("DEBUG_ERROR: getMinOnDevice called with null allocator.\n");
    #endif
    AccPtr<T> min_val(1, ptr.getAllocator(), ptr.getStream());
    min_val.deviceAlloc();
    size_t temp_storage_size = 0;

    DEBUG_HANDLE_ERROR(cub::DeviceReduce::Min( nullptr, temp_storage_size, ptr.getAccPtr(), min_val.getAccPtr(), ptr.getSize()));

    if (temp_storage_size == 0) temp_storage_size = 1;

    CudaCustomAllocator::Alloc* alloc = ptr.getAllocator()->alloc(temp_storage_size);

    DEBUG_HANDLE_ERROR(cub::DeviceReduce::Min( alloc->getPtr(), temp_storage_size, ptr.getAccPtr(), min_val.getAccPtr(), ptr.getSize(), ptr.getStream()));

    min_val.cpToHost();
    ptr.streamSync();

    ptr.getAllocator()->free(alloc);

    return *min_val.getHostPtr();
}

template <typename T>
static T getSumOnDevice(AccPtr<T> &ptr)
{
#ifdef DEBUG_CUDA
if (ptr.getSize() == 0)
    printf("DEBUG_ERROR: getSumOnDevice called with pointer of zero size.\n");
if (ptr.getDevicePtr() == nullptr)
    printf("DEBUG_ERROR: getSumOnDevice called with null device pointer.\n");
if (ptr.getAllocator() == nullptr)
    printf("DEBUG_ERROR: getSumOnDevice called with null allocator.\n");
#endif
    AccPtr<T> val (1, ptr.getAllocator(), ptr.getStream());
    val.deviceAlloc();
    size_t temp_storage_size = 0;
    DEBUG_HANDLE_ERROR(cub::DeviceReduce::Sum(nullptr, temp_storage_size, ptr.getAccPtr(), val.getAccPtr(), ptr.getSize()));
    if (temp_storage_size == 0) temp_storage_size = 1;

    CudaCustomAllocator::Alloc* alloc = ptr.getAllocator()->alloc(temp_storage_size);

    DEBUG_HANDLE_ERROR(cub::DeviceReduce::Sum( alloc->getPtr(), temp_storage_size, ptr.getAccPtr(), val.getAccPtr(), ptr.getSize(), ptr.getStream()));

    val.cpToHost();
    ptr.streamSync();

    ptr.getAllocator()->free(alloc);

    return val.getHostPtr()[0];
}

template <typename T>
static void sortOnDevice(AccPtr<T> &in, AccPtr<T> &out)
{
#ifdef DEBUG_CUDA
if (in.getSize() == 0 || out.getSize() == 0)
    printf("DEBUG_ERROR: sortOnDevice called with pointer of zero size.\n");
if (in.getDevicePtr() == nullptr || out.getDevicePtr() == nullptr)
    printf("DEBUG_ERROR: sortOnDevice called with null device pointer.\n");
if (in.getAllocator() == nullptr)
    printf("DEBUG_ERROR: sortOnDevice called with null allocator.\n");
#endif
    size_t temp_storage_size = 0;

    cudaStream_t stream = in.getStream();

    DEBUG_HANDLE_ERROR(cub::DeviceRadixSort::SortKeys( nullptr, temp_storage_size, in.getAccPtr(), out.getAccPtr(), in.getSize()));

    if(temp_storage_size==0)
        temp_storage_size=1;

    CudaCustomAllocator::Alloc* alloc = in.getAllocator()->alloc(temp_storage_size);

    DEBUG_HANDLE_ERROR(cub::DeviceRadixSort::SortKeys( alloc->getPtr(), temp_storage_size, in.getAccPtr(), out.getAccPtr(), in.getSize(), 0, sizeof(T) * 8, stream));

    alloc->markReadyEvent(stream);
    alloc->doFreeWhenReady();
}

template <typename T>
static void sortDescendingOnDevice(AccPtr<T> &in, AccPtr<T> &out)
{
#ifdef DEBUG_CUDA
if (in.getSize() == 0 || out.getSize() == 0)
    printf("DEBUG_ERROR: sortDescendingOnDevice called with pointer of zero size.\n");
if (in.getDevicePtr() == nullptr || out.getDevicePtr() == nullptr)
    printf("DEBUG_ERROR: sortDescendingOnDevice called with null device pointer.\n");
if (in.getAllocator() == nullptr)
    printf("DEBUG_ERROR: sortDescendingOnDevice called with null allocator.\n");
#endif
    size_t temp_storage_size = 0;

    cudaStream_t stream = in.getStream();

    DEBUG_HANDLE_ERROR(cub::DeviceRadixSort::SortKeysDescending( nullptr, temp_storage_size, in.getAccPtr(), out.getAccPtr(), in.getSize()));

    if(temp_storage_size==0)
        temp_storage_size=1;

    CudaCustomAllocator::Alloc* alloc = in.getAllocator()->alloc(temp_storage_size);

    DEBUG_HANDLE_ERROR(cub::DeviceRadixSort::SortKeysDescending( alloc->getPtr(), temp_storage_size, in.getAccPtr(), out.getAccPtr(), in.getSize(), 0, sizeof(T) * 8, stream));

    alloc->markReadyEvent(stream);
    alloc->doFreeWhenReady();

}

class AllocatorThrustWrapper
{
public:
    // just allocate bytes
    typedef char value_type;
    std::vector<CudaCustomAllocator::Alloc*> allocs;
    CudaCustomAllocator *allocator;

    AllocatorThrustWrapper(CudaCustomAllocator *allocator):
        allocator(allocator)
    {}

    ~AllocatorThrustWrapper()
    {
        for (int i = 0; i < allocs.size(); i ++)
            allocator->free(allocs[i]);
    }

    char* allocate(std::ptrdiff_t num_bytes)
    {
        CudaCustomAllocator::Alloc* alloc = allocator->alloc(num_bytes);
        allocs.push_back(alloc);
        return (char*) alloc->getPtr();
    }

    void deallocate(char* ptr, size_t n)
    {
        //TODO fix this (works fine without it though) /Dari
    }
};

template <typename T>
struct device_greater_than {

    T infimum;

    device_greater_than(T infimum): infimum (infimum) {}

    __device__ __forceinline__
    bool operator () (const T &x) const { return x > infimum; }

};

template <typename T, typename SelectOp>
static int filterOnDevice(AccPtr<T> &in, AccPtr<T> &out, SelectOp select_op)
{
#ifdef DEBUG_CUDA
if (in.getSize() == 0 || out.getSize() == 0)
    printf("DEBUG_ERROR: filterOnDevice called with pointer of zero size.\n");
if (in.getDevicePtr() == nullptr || out.getDevicePtr() == nullptr)
    printf("DEBUG_ERROR: filterOnDevice called with null device pointer.\n");
if (in.getAllocator() == nullptr)
    printf("DEBUG_ERROR: filterOnDevice called with null allocator.\n");
#endif
    size_t temp_storage_size = 0;

    cudaStream_t stream = in.getStream();

    AccPtr<int> num_selected_out(1, in.getAllocator(), stream);
    num_selected_out.deviceAlloc();

    DEBUG_HANDLE_ERROR(cub::DeviceSelect::If(nullptr, temp_storage_size, in.getAccPtr(), out.getAccPtr(), num_selected_out.getAccPtr(), in.getSize(), select_op, stream));

    if(temp_storage_size==0)
        temp_storage_size=1;

    CudaCustomAllocator::Alloc* alloc = in.getAllocator()->alloc(temp_storage_size);

    DEBUG_HANDLE_ERROR(cub::DeviceSelect::If(alloc->getPtr(), temp_storage_size, in.getAccPtr(), out.getAccPtr(), num_selected_out.getAccPtr(), in.getSize(), select_op, stream));

    num_selected_out.cpToHost();
    DEBUG_HANDLE_ERROR(cudaStreamSynchronize(stream));

    in.getAllocator()->free(alloc);
    return num_selected_out.getHostPtr()[0];
}

template <typename T>
static void scanOnDevice(AccPtr<T> &in, AccPtr<T> &out)
{
#ifdef DEBUG_CUDA
if (in.getSize() == 0 || out.getSize() == 0)
    printf("DEBUG_ERROR: scanOnDevice called with pointer of zero size.\n");
if (in.getDevicePtr() == nullptr || out.getDevicePtr() == nullptr)
    printf("DEBUG_ERROR: scanOnDevice called with null device pointer.\n");
if (in.getAllocator() == nullptr)
    printf("DEBUG_ERROR: scanOnDevice called with null allocator.\n");
#endif
    size_t temp_storage_size = 0;

    cudaStream_t stream = in.getStream();

    DEBUG_HANDLE_ERROR(cub::DeviceScan::InclusiveSum( nullptr, temp_storage_size, in.getAccPtr(), out.getAccPtr(), in.getSize()));

    if(temp_storage_size==0)
        temp_storage_size=1;

    CudaCustomAllocator::Alloc* alloc = in.getAllocator()->alloc(temp_storage_size);

    DEBUG_HANDLE_ERROR(cub::DeviceScan::InclusiveSum( alloc->getPtr(), temp_storage_size, in.getAccPtr(), out.getAccPtr(), in.getSize(), stream));

    alloc->markReadyEvent(stream);
    alloc->doFreeWhenReady();
}

} // namespace CudaKernels
#endif
