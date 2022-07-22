# Thread Pool

A reusable thread pool implementation in C++17 using modern concurrency primitives.

## Features

- Configurable number of worker threads (defaults to hardware concurrency)
- Submit any callable and receive a `std::future` for the result
- Supports tasks returning any type (int, double, string, void, etc.)
- Graceful shutdown with proper thread joining
- Exception-safe task submission and execution
- Lock-based task queue with condition variable signaling

## Build

```bash
make
```

## Usage

```bash
./threadpool
```

The program demonstrates:
1. Parallel prime counting with speedup measurement vs sequential
2. Task chaining (step 1 result feeds into step 2, etc.)
3. Mixed return type tasks
4. Throughput benchmark (tasks/sec)

## Clean

```bash
make clean
```
