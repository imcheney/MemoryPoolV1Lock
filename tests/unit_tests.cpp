#include "MemoryPool.h"

#include <atomic>
#include <cassert>
#include <cstdint>
#include <iostream>

using namespace memorypool;

namespace
{
// Counted 用于验证构造/析构是否由内存池辅助函数触发
struct Counted
{
    Counted() { ++liveCount; }
    ~Counted() { --liveCount; }
    int value{0};

    static std::atomic<int> liveCount;
};

std::atomic<int> Counted::liveCount{0};

// AlignedPayload 测试高对齐需求的类型
struct alignas(32) AlignedPayload
{
    unsigned char buffer[32];
};
}  // namespace

int main()
{
    HashBucket::ensureInitialized();
    LockFreeHashBucket::ensureInitialized();

    // basic reuse behaviour：空闲链表应复用 slot
    void* slotA = HashBucket::useMemory(8);
    void* slotB = HashBucket::useMemory(8);
    assert(slotA != nullptr && slotB != nullptr && "allocation should succeed");

    HashBucket::freeMemory(slotA, 8);
    void* slotC = HashBucket::useMemory(8);
    assert(slotC == slotA && "free list should reuse slots");

    HashBucket::freeMemory(slotB, 8);
    HashBucket::freeMemory(slotC, 8);

    // placement new helper should invoke constructors/destructors：newElement/deleteElement 应维护对象生命周期
    Counted::liveCount.store(0, std::memory_order_relaxed);
    Counted* counted = newElement<Counted>();
    assert(counted != nullptr);
    assert(Counted::liveCount.load(std::memory_order_relaxed) == 1);
    deleteElement(counted);
    assert(Counted::liveCount.load(std::memory_order_relaxed) == 0);

    // requests larger than MAX_SLOT_SIZE should fall back to global new/delete：超大尺寸应走全局分配
    void* bigBlock = HashBucket::useMemory(MAX_SLOT_SIZE + 128);
    assert(bigBlock != nullptr);
    HashBucket::freeMemory(bigBlock, MAX_SLOT_SIZE + 128);

    // ensure alignment for strongly-aligned types：校验内存池能满足对齐需求
    AlignedPayload* aligned = newElement<AlignedPayload>();
    auto alignedAddr = reinterpret_cast<std::uintptr_t>(aligned);
    assert(alignedAddr % alignof(AlignedPayload) == 0);
    deleteElement(aligned);

    // lock-free pool should mirror the locking variant's semantics
    void* lfSlotA = LockFreeHashBucket::useMemory(8);
    void* lfSlotB = LockFreeHashBucket::useMemory(8);
    assert(lfSlotA != nullptr && lfSlotB != nullptr);
    LockFreeHashBucket::freeMemory(lfSlotA, 8);
    void* lfSlotC = LockFreeHashBucket::useMemory(8);
    assert(lfSlotC == lfSlotA);
    LockFreeHashBucket::freeMemory(lfSlotB, 8);
    LockFreeHashBucket::freeMemory(lfSlotC, 8);

    Counted::liveCount.store(0, std::memory_order_relaxed);
    Counted* countedLF = newElementLockFree<Counted>();
    assert(countedLF != nullptr);
    assert(Counted::liveCount.load(std::memory_order_relaxed) == 1);
    deleteElementLockFree(countedLF);
    assert(Counted::liveCount.load(std::memory_order_relaxed) == 0);

    void* bigBlockLF = LockFreeHashBucket::useMemory(MAX_SLOT_SIZE + 256);
    assert(bigBlockLF != nullptr);
    LockFreeHashBucket::freeMemory(bigBlockLF, MAX_SLOT_SIZE + 256);

    AlignedPayload* alignedLF = newElementLockFree<AlignedPayload>();
    auto alignedLFAddr = reinterpret_cast<std::uintptr_t>(alignedLF);
    assert(alignedLFAddr % alignof(AlignedPayload) == 0);
    deleteElementLockFree(alignedLF);

    std::cout << "All unit tests passed\n";
    return 0;
}
