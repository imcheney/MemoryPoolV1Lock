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
// Payload 用来模拟固定大小对象，构造/析构内含校验，帮助检测写越界
class Payload
{
public:
    explicit Payload(int v) : value(v)
    {
        for (std::size_t i = 0; i < sizeof(padding); ++i)
        {
            padding[i] = static_cast<unsigned char>((value + static_cast<int>(i)) & 0xFF);
        }
    }

    ~Payload()
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

    // worker 在每个线程里持续分配和释放对象，验证并发路径
    auto worker = [&](std::size_t threadIndex) {
        std::vector<Payload*> nodes;
        nodes.reserve(iterationsPerThread);

        for (std::size_t i = 0; i < iterationsPerThread; ++i)
        {
            const int value = static_cast<int>(threadIndex * iterationsPerThread + i);
            Payload* node = newElement<Payload>(value);
            assert(node != nullptr);
            assert(node->value == value);
            nodes.push_back(node);
        }

        totalAllocated.fetch_add(nodes.size(), std::memory_order_relaxed);

        for (Payload* node : nodes)
        {
            deleteElement(node);
        }
    };

    // 启动多个线程并等待全部结束
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
              << " payloads across " << threadCount << " threads\n";

    return 0;
}
