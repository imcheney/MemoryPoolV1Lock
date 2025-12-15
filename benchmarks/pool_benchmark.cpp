#include "MemoryPool.h"

#include <chrono>
#include <cstddef>
#include <iostream>
#include <string>
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
    HashBucket::ensureInitialized();  // 确保内存池准备就绪，避免把初始化成本算进测试

    constexpr std::size_t iterations = 500000;
    std::vector<BenchPayload*> cache;
    cache.reserve(iterations);

    runBenchmark("memory pool", [&]() {
        for (std::size_t i = 0; i < iterations; ++i)
        {
            cache.push_back(newElement<BenchPayload>());
        }
        for (BenchPayload* ptr : cache)
        {
            deleteElement(ptr);
        }
        cache.clear();
    });

    runBenchmark("operator new/delete", [&]() {
        for (std::size_t i = 0; i < iterations; ++i)
        {
            cache.push_back(new BenchPayload());
        }
        for (BenchPayload* ptr : cache)
        {
            delete ptr;
        }
        cache.clear();
    });

    return 0;
}
