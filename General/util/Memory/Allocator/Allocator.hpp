#pragma once
#include <stdlib.h>
namespace zzj
{
class Allocator
{
  public:
    template <typename T> static T *Allocate(int count)
    {
        T *ret = (T *)malloc(sizeof(T) * count);
        return ret;
    }
    template <typename T> static void Deallocate(T *ptr)
    {
        free(ptr);
    }

    static char *AllocMemory(int size)
    {
        return (char *)malloc(size);
    }
    static void FreeMemory(void *ptr)
    {
        free(ptr);
    }
};
}; // namespace zzj