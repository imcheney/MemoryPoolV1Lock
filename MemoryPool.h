#include <cstddef>
#include <mutex>
#include <new>
#include <utility>

namespace memorypool 
{

#define MEMORY_POOL_NUM 64
#define SLOT_BASE_SIZE 8
#define MAX_SLOT_SIZE 512

// hierarchy: one HashBucket -> many MemoryPool -> many Block -> many Slot

struct Slot
{
    Slot* next;
};

class MemoryPool
{
public:
    MemoryPool(size_t BlockSize = 4096);
    ~MemoryPool();

    void init(size_t);

    void* allocate();
    void deallocate(void*);

private:
    void allocateNewBlock();
    size_t padPointer(char* p, size_t align);

    std::size_t BlockSize_;
    std::size_t SlotSize_;
    std::size_t slotAdvance_;
    Slot* firstBlock_;  // ptr to the real first block that the pool manages
    Slot* curSlot_;  // ptr to the current slot that has never been used
    Slot* freeList_;
    Slot* endSlot_;  // one-past-the-end slot marker for the current block
    std::mutex mutexForFreeList_;  // mutex for free list
    std::mutex mutexForBlock_;  // mutex for block allocation
};

class HashBucket
{
public:
    static void initMemoryPool();
    static MemoryPool& getMemoryPool(int index);
    static void ensureInitialized();

    // this function allocates memory from memory pool or global new based on size
    // it will decide which memory pool to use based on size
    // for example, size <= 8 bytes, use memory pool 0; size <= 16 bytes, use memory pool 1; ...
    // the reason is that each memory pool manages slots of size 8, 16, 24, ..., 512 bytes
    static void* useMemory(size_t size)
    {
        ensureInitialized();

        if (size <= 0) 
        {
            return nullptr;
        }

        if (size > MAX_SLOT_SIZE)  // > 512 bytes, use global new
        {
            return operator new(size);
        }
        
        // 8 bytes, then index = 0; 9 bytes, then index = 1;  16 bytes, index = 1;  17 bytes, index = 2
        return getMemoryPool((size + 7) / SLOT_BASE_SIZE - 1).allocate();
    }

    static void freeMemory(void* ptr, size_t size)
    {
        ensureInitialized();

        if (!ptr)
        {
            return;
        }

        if (size > MAX_SLOT_SIZE)
        {
            operator delete(ptr);
            return;
        }

        getMemoryPool((size + 7) / SLOT_BASE_SIZE - 1).deallocate(ptr);
    }

    // TODO: 不太理解这是做啥的
    template<typename T, typename... Args>
    friend T* newElement(Args&&... args);

    template<typename T>
    friend void deleteElement(T* p);
};

// Note: 对外暴露的接口是这两个模板函数，用于分配和释放特定类型的对象
template<typename T, typename... Args>
T* newElement(Args&&... args)
{
    T* p = nullptr;
    // select the right memory pool based on size of T
    if ((p = reinterpret_cast<T*>(HashBucket::useMemory(sizeof(T)))) != nullptr)
    {
        new (p) T(std::forward<Args>(args)...);  // placement new
    }
    return p;
}

template<typename T>
void deleteElement(T* p)
{
    if (p != nullptr)
    {
        p->~T();  // call destructor
        HashBucket::freeMemory(reinterpret_cast<void*>(p), sizeof(T));
    }
}

}  // namespace memorypool