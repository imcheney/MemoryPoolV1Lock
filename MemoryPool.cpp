#include "MemoryPool.h"

#include <mutex>

namespace memorypool 
{
namespace
{
std::once_flag g_memoryPoolInitFlag;
std::once_flag g_lockFreePoolInitFlag;
}

MemoryPool::MemoryPool(size_t BlockSize)
        : BlockSize_(BlockSize), SlotSize_(0), slotAdvance_(0), firstBlock_(nullptr),
            curSlot_(nullptr), freeList_(nullptr), endSlot_(nullptr)
{
}

MemoryPool::~MemoryPool()
{
    Slot* currentBlock = firstBlock_;
    while (currentBlock != nullptr)
    {
        Slot* nextBlock = currentBlock->next;
        ::operator delete(static_cast<void*>(currentBlock));
        currentBlock = nextBlock;
    }
}

void MemoryPool::init(size_t slotSize)
{
    SlotSize_ = slotSize;
    if (SlotSize_ == 0)
    {
        SlotSize_ = sizeof(Slot);
    }

    if (SlotSize_ % sizeof(Slot) != 0)
    {
        SlotSize_ = ((SlotSize_ + sizeof(Slot) - 1) / sizeof(Slot)) * sizeof(Slot);
    }

    slotAdvance_ = SlotSize_ / sizeof(Slot);
    if (slotAdvance_ == 0)
    {
        slotAdvance_ = 1;
        SlotSize_ = sizeof(Slot);
    }

    firstBlock_ = nullptr;
    curSlot_ = nullptr;
    freeList_ = nullptr;
    endSlot_ = nullptr;
}

void* MemoryPool::allocate()
{
    std::lock_guard<std::mutex> lock(mutexForFreeList_);

    // First check the free list
    if (freeList_ != nullptr)
    {
        Slot* slot = freeList_;
        freeList_ = freeList_->next;
        return static_cast<void*>(slot);
    }

    void* temp;
    {
        std::lock_guard<std::mutex> lockBlock(mutexForBlock_);
        // If no available slots in the current block, allocate a new block
        if (curSlot_ == nullptr || endSlot_ == nullptr || curSlot_ == endSlot_)
        {
            allocateNewBlock();
        }

        // normal logic: allocate from the current block
        temp = curSlot_;
        curSlot_ += slotAdvance_;
    }
    return temp;
}

void MemoryPool::deallocate(void* p)
{
    if (p == nullptr)
    {
        return;
    }

    // add the slot back to the free list's head
    std::lock_guard<std::mutex> lock(mutexForFreeList_);
    Slot* slot = static_cast<Slot*>(p);
    slot->next = freeList_;
    freeList_ = slot;
}

void MemoryPool::allocateNewBlock()
{
    // add a new block to the pool's managed blocks list head
    void* newBlock = ::operator new(BlockSize_);
    Slot* newBlockSlot = static_cast<Slot*>(newBlock);
    newBlockSlot->next = firstBlock_;
    firstBlock_ = newBlockSlot;

    // handle current slot and last slot
    char* blockBody = reinterpret_cast<char*>(newBlock) + sizeof(Slot*);
    size_t bodyPadding = padPointer(blockBody, SlotSize_);
    char* alignedBody = blockBody + bodyPadding;
    std::size_t usableBytes = BlockSize_ - static_cast<std::size_t>(alignedBody - reinterpret_cast<char*>(newBlock));
    if (usableBytes < SlotSize_)
    {
        ::operator delete(newBlock);
        throw std::bad_alloc();
    }

    std::size_t slotCount = usableBytes / SlotSize_;
    if (slotCount == 0)
    {
        ::operator delete(newBlock);
        throw std::bad_alloc();
    }

    curSlot_ = reinterpret_cast<Slot*>(alignedBody);
    endSlot_ = curSlot_ + slotCount * slotAdvance_;
    freeList_ = nullptr;  // reset free list for the new block
}

size_t MemoryPool::padPointer(char* p, size_t align)
{
    // （8 - 4099 ） % 8 = 5
    return (align - reinterpret_cast<size_t>(p) % align) % align;
}

void HashBucket::initMemoryPool()
{
    for (int i = 0; i < MEMORY_POOL_NUM; ++i)
    {
        getMemoryPool(i).init(static_cast<std::size_t>((i + 1) * SLOT_BASE_SIZE));
    }
}

void HashBucket::ensureInitialized()
{
    std::call_once(g_memoryPoolInitFlag, []() {
        initMemoryPool();
    });
}

// single instance of memory pools
MemoryPool& HashBucket::getMemoryPool(int index)
{
    static MemoryPool memoryPools[MEMORY_POOL_NUM];  // static here means this array is created only once
    return memoryPools[index];
}

LockFreeMemoryPool::LockFreeMemoryPool(size_t BlockSize)
    : BlockSize_(BlockSize), SlotSize_(0), slotAdvance_(0), firstBlock_(nullptr),
      curSlot_(nullptr), freeList_(nullptr), endSlot_(nullptr)
{
}

LockFreeMemoryPool::~LockFreeMemoryPool()
{
    Slot* currentBlock = firstBlock_;
    while (currentBlock != nullptr)
    {
        Slot* nextBlock = currentBlock->next;
        ::operator delete(static_cast<void*>(currentBlock));
        currentBlock = nextBlock;
    }
}

void LockFreeMemoryPool::init(size_t slotSize)
{
    SlotSize_ = slotSize;
    if (SlotSize_ == 0)
    {
        SlotSize_ = sizeof(Slot);
    }

    if (SlotSize_ % sizeof(Slot) != 0)
    {
        SlotSize_ = ((SlotSize_ + sizeof(Slot) - 1) / sizeof(Slot)) * sizeof(Slot);
    }

    slotAdvance_ = SlotSize_ / sizeof(Slot);
    if (slotAdvance_ == 0)
    {
        slotAdvance_ = 1;
        SlotSize_ = sizeof(Slot);
    }

    firstBlock_ = nullptr;
    curSlot_ = nullptr;
    freeList_.store(nullptr, std::memory_order_relaxed);
    endSlot_ = nullptr;
}

void* LockFreeMemoryPool::allocate()
{
    if (Slot* slot = popFreeList())
    {
        return static_cast<void*>(slot);
    }

    void* temp;
    {
        std::lock_guard<std::mutex> lock(mutexForBlock_);
        if (curSlot_ == nullptr || endSlot_ == nullptr || curSlot_ == endSlot_)
        {
            allocateNewBlock();
        }

        temp = curSlot_;
        curSlot_ += slotAdvance_;
    }
    return temp;
}

void LockFreeMemoryPool::deallocate(void* p)
{
    if (p == nullptr)
    {
        return;
    }

    Slot* slot = static_cast<Slot*>(p);
    while (!pushFreeList(slot))
    {
        // retry until CAS succeeds
    }
}

void LockFreeMemoryPool::allocateNewBlock()
{
    void* newBlock = ::operator new(BlockSize_);
    Slot* newBlockSlot = static_cast<Slot*>(newBlock);
    newBlockSlot->next = firstBlock_;
    firstBlock_ = newBlockSlot;

    char* blockBody = reinterpret_cast<char*>(newBlock) + sizeof(Slot*);
    size_t bodyPadding = padPointer(blockBody, SlotSize_);
    char* alignedBody = blockBody + bodyPadding;
    std::size_t usableBytes = BlockSize_ - static_cast<std::size_t>(alignedBody - reinterpret_cast<char*>(newBlock));
    if (usableBytes < SlotSize_)
    {
        ::operator delete(newBlock);
        throw std::bad_alloc();
    }

    std::size_t slotCount = usableBytes / SlotSize_;
    if (slotCount == 0)
    {
        ::operator delete(newBlock);
        throw std::bad_alloc();
    }

    curSlot_ = reinterpret_cast<Slot*>(alignedBody);
    endSlot_ = curSlot_ + slotCount * slotAdvance_;
}

size_t LockFreeMemoryPool::padPointer(char* p, size_t align)
{
    return (align - reinterpret_cast<size_t>(p) % align) % align;
}

bool LockFreeMemoryPool::pushFreeList(Slot* slot)
{
    Slot* oldHead = freeList_.load(std::memory_order_acquire);
    do
    {
        slot->next = oldHead;
    }
    while (!freeList_.compare_exchange_weak(oldHead, slot,
                                            std::memory_order_release,
                                            std::memory_order_relaxed));
    return true;
}

Slot* LockFreeMemoryPool::popFreeList()
{
    Slot* oldHead = freeList_.load(std::memory_order_acquire);
    while (oldHead != nullptr)
    {
        Slot* newHead = oldHead->next;
        if (freeList_.compare_exchange_weak(oldHead, newHead,
                                            std::memory_order_acquire,
                                            std::memory_order_relaxed))
        {
            return oldHead;
        }
    }
    return nullptr;
}

void LockFreeHashBucket::initMemoryPool()
{
    for (int i = 0; i < MEMORY_POOL_NUM; ++i)
    {
        getMemoryPool(i).init(static_cast<std::size_t>((i + 1) * SLOT_BASE_SIZE));
    }
}

void LockFreeHashBucket::ensureInitialized()
{
    std::call_once(g_lockFreePoolInitFlag, []() {
        initMemoryPool();
    });
}

LockFreeMemoryPool& LockFreeHashBucket::getMemoryPool(int index)
{
    static LockFreeMemoryPool memoryPools[MEMORY_POOL_NUM];
    return memoryPools[index];
}

}  // namespace memorypool