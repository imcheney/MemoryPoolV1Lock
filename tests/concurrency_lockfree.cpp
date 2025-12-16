#include "MemoryPool.h"

#include <atomic>
#include <cassert>
#include <cstddef>
#include <iostream>
#include <thread>
#include <vector>

using namespace memorypool;

namespace
{
// PayloadLF 用于验证 lock-free 版本在并发场景下的正确性
class PayloadLF
{
public:
    explicit PayloadLF(int v) : value(v)
    {
        for (std::size_t i = 0; i < sizeof(padding); ++i)
        {
            padding[i] = static_cast<unsigned char>((value + static_cast<int>(i)) & 0xFF);
        }
    }

    ~PayloadLF()
    {
        for (std::size_t i = 0; i < sizeof(padding); ++i)
        {
            assert(padding[i] == static_cast<unsigned char>((value + static_cast<int>(i)) & 0xFF));
        }
    }

    int value;
    unsigned char padding[56];
};
}  // namespace

int main()
{
    constexpr std::size_t threadCount = 8;
    constexpr std::size_t iterationsPerThread = 25000;

    std::atomic<std::size_t> totalAllocated{0};

    // worker 函数：持续分配与释放，触发 lock-free 内存池的主路径
    auto worker = [&](std::size_t threadIndex) {
        std::vector<PayloadLF*> nodes;
        nodes.reserve(iterationsPerThread);

        for (std::size_t i = 0; i < iterationsPerThread; ++i)
        {
            const int value = static_cast<int>(threadIndex * iterationsPerThread + i);
            PayloadLF* node = newElementLockFree<PayloadLF>(value);
            assert(node != nullptr);
            assert(node->value == value);
            nodes.push_back(node);
        }

        totalAllocated.fetch_add(nodes.size(), std::memory_order_relaxed);

        for (PayloadLF* node : nodes)
        {
            deleteElementLockFree(node);
        }
    };

    std::vector<std::thread> threads;
    threads.reserve(threadCount);
    for (std::size_t i = 0; i < threadCount; ++i)
    {
        threads.emplace_back(worker, i);
    }

    for (auto& t : threads)
    {
        t.join();
    }

    std::cout << "Allocated and freed " << totalAllocated.load(std::memory_order_relaxed)
              << " payloads via lock-free pool across " << threadCount << " threads\n";

    return 0;
}
