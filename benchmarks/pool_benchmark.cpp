#include "MemoryPool.h"

#include <chrono>
#include <cstddef>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

using namespace memorypool;

namespace
{
// BenchPayload 模拟固定大小的分配对象
struct BenchPayload
{
    unsigned char data[32];
};

using Clock = std::chrono::steady_clock;

// runBenchmark 记录 func 的执行耗时并输出结果
template<typename Func>
void runBenchmark(const std::string& name, Func&& func)
{
    const auto start = Clock::now();
    func();
    const auto end = Clock::now();
    const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    std::cout << name << ": " << elapsed.count() / 1000.0 << " ms" << std::endl;
}
}  // namespace

int main()
{
    HashBucket::ensureInitialized();  // 确保锁版本的内存池准备就绪
    LockFreeHashBucket::ensureInitialized();  // 同样准备好无锁版本

    constexpr std::size_t sequentialIterations = 1'000'000;
    constexpr std::size_t threadCount = 8;
    constexpr std::size_t iterationsPerThread = 200'000;

    std::cout << "Sequential benchmarks (" << sequentialIterations << " operations)" << std::endl;

    runBenchmark("memory pool (sequential)", [&]() {
        std::vector<BenchPayload*> cache;
        cache.reserve(sequentialIterations);
        for (std::size_t i = 0; i < sequentialIterations; ++i)
        {
            cache.push_back(newElement<BenchPayload>());
        }
        for (BenchPayload* ptr : cache)
        {
            deleteElement(ptr);
        }
    });

    runBenchmark("lock-free memory pool (sequential)", [&]() {
        std::vector<BenchPayload*> cache;
        cache.reserve(sequentialIterations);
        for (std::size_t i = 0; i < sequentialIterations; ++i)
        {
            cache.push_back(newElementLockFree<BenchPayload>());
        }
        for (BenchPayload* ptr : cache)
        {
            deleteElementLockFree(ptr);
        }
    });

    runBenchmark("operator new/delete (sequential)", [&]() {
        std::vector<BenchPayload*> cache;
        cache.reserve(sequentialIterations);
        for (std::size_t i = 0; i < sequentialIterations; ++i)
        {
            cache.push_back(new BenchPayload());
        }
        for (BenchPayload* ptr : cache)
        {
            delete ptr;
        }
    });

    std::cout << "\nConcurrent benchmarks (" << threadCount << " threads x "
              << iterationsPerThread << " operations)" << std::endl;

    runBenchmark("memory pool (concurrent)", [&]() {
        std::vector<std::thread> threads;
        threads.reserve(threadCount);
        for (std::size_t t = 0; t < threadCount; ++t)
        {
            threads.emplace_back([&]() {
                std::vector<BenchPayload*> local;
                local.reserve(iterationsPerThread);
                for (std::size_t i = 0; i < iterationsPerThread; ++i)
                {
                    local.push_back(newElement<BenchPayload>());
                }
                for (BenchPayload* ptr : local)
                {
                    deleteElement(ptr);
                }
            });
        }
        for (auto& th : threads)
        {
            th.join();
        }
    });

    runBenchmark("lock-free memory pool (concurrent)", [&]() {
        std::vector<std::thread> threads;
        threads.reserve(threadCount);
        for (std::size_t t = 0; t < threadCount; ++t)
        {
            threads.emplace_back([&]() {
                std::vector<BenchPayload*> local;
                local.reserve(iterationsPerThread);
                for (std::size_t i = 0; i < iterationsPerThread; ++i)
                {
                    local.push_back(newElementLockFree<BenchPayload>());
                }
                for (BenchPayload* ptr : local)
                {
                    deleteElementLockFree(ptr);
                }
            });
        }
        for (auto& th : threads)
        {
            th.join();
        }
    });

    runBenchmark("operator new/delete (concurrent)", [&]() {
        std::vector<std::thread> threads;
        threads.reserve(threadCount);
        for (std::size_t t = 0; t < threadCount; ++t)
        {
            threads.emplace_back([&]() {
                std::vector<BenchPayload*> local;
                local.reserve(iterationsPerThread);
                for (std::size_t i = 0; i < iterationsPerThread; ++i)
                {
                    local.push_back(new BenchPayload());
                }
                for (BenchPayload* ptr : local)
                {
                    delete ptr;
                }
            });
        }
        for (auto& th : threads)
        {
            th.join();
        }
    });

    return 0;
}
