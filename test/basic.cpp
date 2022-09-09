/*
 * Copyright (C) 2019 Ricardo Leite. All rights reserved.
 * Licenced under the MIT licence. See COPYING file in the project root for
 * details.
 */

#include <cstdio>
#include <cstdlib>

#include <algorithm>
#include <array>
#include <random>
#include <thread>
#include <vector>

int main()
{
    // super trivial test:
    // - create N threads
    // - each thread does X allocs
    // - each thread shuffles their allocs
    // - each thread frees their allocs

    printf("Basic tests\n");

    // parameters
    constexpr size_t numThreads = 8;
    constexpr size_t numAllocs = 10000;
    constexpr size_t minAllocSize = 1;
    constexpr size_t maxAllocSize = 65536;

    printf("Parameters: %zu threads x %zu allocs between [%zu,%zu] bytes\n",
        numThreads, numAllocs, minAllocSize, maxAllocSize);

    std::random_device dev;
    std::uniform_int_distribution<std::mt19937::result_type> dist(minAllocSize, maxAllocSize);

    std::array<std::mt19937, numThreads> rngs;
    std::array<std::thread, numThreads> threads;

    for (size_t t = 0; t < numThreads; ++t) {
        rngs[t] = std::mt19937(dev());

        threads[t] = std::thread([t, &rngs, &dist]() {
            auto rng = rngs[t];
            std::vector<std::tuple<uint8_t* /*alloc*/, size_t /*size*/, uint8_t /*pattern*/>> allocs(numAllocs);

            printf("Thread %zu doing %zu allocations\n",
                t, numAllocs);

            // fill allocs
            for (auto& alloc : allocs) {
                size_t size = dist(rng);
                uint8_t pattern = static_cast<uint8_t>(dist(rng));

                // allocate
                uint8_t* buffer = static_cast<uint8_t*>(malloc(size));

                // write with random pattern
                for (size_t k = 0; k < size; ++k) {
                    buffer[k] = pattern;
                }

                // add to allocs
                alloc = std::make_tuple(buffer, size, pattern);
            }

            printf("Thread %zu shuffling %zu allocations\n",
                t, numAllocs);

            // shuffle 'em
            std::shuffle(allocs.begin(), allocs.end(), rng);

            printf("Thread %zu freeing %zu allocations\n",
                t, numAllocs);

            // verify and free them
            for (auto& alloc : allocs) {
                uint8_t* buffer = std::get<uint8_t*>(alloc);
                size_t size = std::get<size_t>(alloc);
                uint8_t pattern = std::get<uint8_t>(alloc);

                // check random pattern
                for (size_t k = 0; k < size; ++k) {
                    if (buffer[k] != pattern) {
                        printf("Thread %zu: alloc %p of size %zu is corrupted, expected pattern 0x%2X but got 0x%2X\n",
                            t, buffer, size, pattern, buffer[k]);

                        ::exit(1);
                    }
                }

                free(buffer);
            }
        });
    }

    for (size_t t = 0; t < numThreads; ++t) {
        threads[t].join();
    }

    return 0;
}
