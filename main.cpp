#include <iostream>
#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <future>
#include <functional>
#include <chrono>
#include <cmath>
#include <numeric>
#include <iomanip>
#include <atomic>
#include <sstream>

class ThreadPool {
public:
    explicit ThreadPool(size_t num_threads = std::thread::hardware_concurrency()) {
        for (size_t i = 0; i < num_threads; ++i) {
            workers_.emplace_back([this] { worker_loop(); });
        }
    }

    ~ThreadPool() {
        shutdown();
    }

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    template <typename F, typename... Args>
    auto submit(F&& f, Args&&... args) -> std::future<std::invoke_result_t<F, Args...>> {
        using ReturnType = std::invoke_result_t<F, Args...>;

        auto task = std::make_shared<std::packaged_task<ReturnType()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );

        std::future<ReturnType> result = task->get_future();

        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stopped_) {
                throw std::runtime_error("submit called on stopped ThreadPool");
            }
            tasks_.emplace([task]() { (*task)(); });
        }
        cv_.notify_one();
        return result;
    }

    void shutdown() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stopped_) return;
            stopped_ = true;
        }
        cv_.notify_all();
        for (auto& w : workers_) {
            if (w.joinable()) w.join();
        }
    }

    size_t thread_count() const { return workers_.size(); }

private:
    void worker_loop() {
        while (true) {
            std::function<void()> task;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                cv_.wait(lock, [this] { return stopped_ || !tasks_.empty(); });
                if (stopped_ && tasks_.empty()) return;
                task = std::move(tasks_.front());
                tasks_.pop();
            }
            task();
        }
    }

    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> tasks_;
    std::mutex mutex_;
    std::condition_variable cv_;
    bool stopped_ = false;
};

bool is_prime(uint64_t n) {
    if (n < 2) return false;
    if (n < 4) return true;
    if (n % 2 == 0 || n % 3 == 0) return false;
    for (uint64_t i = 5; i * i <= n; i += 6) {
        if (n % i == 0 || n % (i + 2) == 0) return false;
    }
    return true;
}

size_t count_primes_in_range(uint64_t start, uint64_t end) {
    size_t count = 0;
    for (uint64_t i = start; i < end; ++i) {
        if (is_prime(i)) ++count;
    }
    return count;
}

void demo_parallel_primes(ThreadPool& pool) {
    std::cout << "=== Parallel Prime Counting ===\n\n";

    constexpr uint64_t LIMIT = 2'000'000;
    size_t num_threads = pool.thread_count();
    uint64_t chunk = LIMIT / num_threads;

    auto par_start = std::chrono::high_resolution_clock::now();
    std::vector<std::future<size_t>> futures;
    for (size_t i = 0; i < num_threads; ++i) {
        uint64_t lo = i * chunk;
        uint64_t hi = (i == num_threads - 1) ? LIMIT : (i + 1) * chunk;
        futures.push_back(pool.submit(count_primes_in_range, lo, hi));
    }

    size_t par_total = 0;
    for (auto& f : futures) {
        par_total += f.get();
    }
    auto par_time = std::chrono::high_resolution_clock::now() - par_start;

    auto seq_start = std::chrono::high_resolution_clock::now();
    size_t seq_total = count_primes_in_range(0, LIMIT);
    auto seq_time = std::chrono::high_resolution_clock::now() - seq_start;

    auto par_ms = std::chrono::duration<double, std::milli>(par_time).count();
    auto seq_ms = std::chrono::duration<double, std::milli>(seq_time).count();

    std::cout << "Counting primes below " << LIMIT << ":\n";
    std::cout << "  Result: " << par_total << " primes (verified: " << seq_total << ")\n\n";
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "  Sequential: " << seq_ms << " ms\n";
    std::cout << "  Parallel (" << num_threads << " threads): " << par_ms << " ms\n";
    std::cout << "  Speedup: " << (seq_ms / par_ms) << "x\n";
}

void demo_task_chaining(ThreadPool& pool) {
    std::cout << "\n=== Task Chaining ===\n\n";

    auto step1 = pool.submit([]() -> std::vector<int> {
        std::cout << "  Step 1: Generating data...\n";
        std::vector<int> data(100);
        std::iota(data.begin(), data.end(), 1);
        return data;
    });

    std::vector<int> data = step1.get();

    auto step2 = pool.submit([data = std::move(data)]() -> double {
        std::cout << "  Step 2: Computing sum...\n";
        return std::accumulate(data.begin(), data.end(), 0.0);
    });

    double sum = step2.get();

    auto step3 = pool.submit([sum]() -> std::string {
        std::cout << "  Step 3: Formatting result...\n";
        std::ostringstream oss;
        oss << "Sum of 1..100 = " << std::fixed << std::setprecision(0) << sum;
        return oss.str();
    });

    std::cout << "  Result: " << step3.get() << "\n";
}

void demo_mixed_tasks(ThreadPool& pool) {
    std::cout << "\n=== Mixed Task Types ===\n\n";

    auto int_task = pool.submit([]() -> int { return 42; });
    auto double_task = pool.submit([]() -> double { return std::sqrt(2.0); });
    auto string_task = pool.submit([]() -> std::string { return "hello from thread pool"; });
    auto void_task = pool.submit([]() { /* fire and forget */ });

    std::cout << "  int result:    " << int_task.get() << "\n";
    std::cout << std::fixed << std::setprecision(6);
    std::cout << "  double result: " << double_task.get() << "\n";
    std::cout << "  string result: " << string_task.get() << "\n";
    void_task.get();
    std::cout << "  void task:     completed\n";
}

void benchmark_throughput(ThreadPool& pool) {
    std::cout << "\n=== Throughput Benchmark ===\n\n";

    constexpr int NUM_TASKS = 10000;
    std::atomic<int> counter{0};

    auto start = std::chrono::high_resolution_clock::now();
    std::vector<std::future<void>> futures;
    futures.reserve(NUM_TASKS);

    for (int i = 0; i < NUM_TASKS; ++i) {
        futures.push_back(pool.submit([&counter]() {
            counter.fetch_add(1, std::memory_order_relaxed);
        }));
    }

    for (auto& f : futures) f.get();
    auto elapsed = std::chrono::high_resolution_clock::now() - start;

    auto ms = std::chrono::duration<double, std::milli>(elapsed).count();
    std::cout << "  Dispatched and completed " << NUM_TASKS << " tasks in "
              << std::fixed << std::setprecision(2) << ms << " ms\n";
    std::cout << "  Throughput: " << std::setprecision(0)
              << (NUM_TASKS / (ms / 1000.0)) << " tasks/sec\n";
    std::cout << "  Counter value: " << counter.load() << " (expected " << NUM_TASKS << ")\n";
}

int main() {
    size_t hw_threads = std::thread::hardware_concurrency();
    size_t num_threads = hw_threads > 0 ? hw_threads : 4;

    std::cout << "Thread Pool Demo\n";
    std::cout << "Hardware concurrency: " << hw_threads << "\n";
    std::cout << "Using " << num_threads << " worker threads\n\n";

    ThreadPool pool(num_threads);

    demo_parallel_primes(pool);
    demo_task_chaining(pool);
    demo_mixed_tasks(pool);
    benchmark_throughput(pool);

    std::cout << "\nShutting down thread pool...\n";
    pool.shutdown();
    std::cout << "Done.\n";

    return 0;
}
