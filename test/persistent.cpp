/*
 * Copyright (C) 2019 Ricardo Leite. All rights reserved.
 * Licenced under the MIT licence. See COPYING file in the project root for
 * details.
 */

#include <cstdio>
#include <cstdlib>

#include <algorithm>
#include <random>
#include <thread>
#include <vector>

#include "../lrmalloc.h"

int main()
{
    // check whether "persistent" allocations never cause SIGSEGVs
    // - do N allocations, where 20% are persistent
    // - keep track of the persistent allocations
    // - free all allocations
    // - keep dereferencing persistent allocations every now and then

    printf("Persistent alloc tests\n");

    // parameters
    constexpr size_t numIterations = 10;
    constexpr size_t numAllocs = 1000000;
    constexpr size_t minAllocSize = sizeof(uint8_t);
    constexpr size_t maxAllocSize = 14335; // must be smaller than 14336, that is the maximum size class

    printf("Parameters: %zu iterations x %zu allocs between [%zu,%zu] bytes\n",
        numIterations, numAllocs, minAllocSize, maxAllocSize);

    std::random_device dev;
    std::mt19937 rng(dev());
    std::uniform_int_distribution<std::mt19937::result_type> dist(minAllocSize, maxAllocSize);

    std::vector<uint8_t*> persistent;

    for (size_t i = 0; i < numIterations; ++i) {

        printf("Iteration %zu, doing %zu allocations\n",
            i, numAllocs);

        std::vector<uint8_t*> allocs(numAllocs);

        // fill allocs
        for (auto& alloc : allocs) {
            size_t size = dist(rng);

            // allocate
            bool isPersistent = ((rng() % 10) < 2);
            alloc = static_cast<uint8_t*>(
                isPersistent ? lf_palloc(size) : malloc(size)
            );

            if (isPersistent) {
                persistent.push_back(alloc);
            }
        }

        printf("Iteration %zu, shuffling %zu allocations (have %zu persistent in total)\n",
            i, numAllocs, persistent.size());

        // shuffle 'em
        std::shuffle(allocs.begin(), allocs.end(), rng);

        printf("Iteration %zu, freeing %zu allocations\n",
            i, numAllocs);

        // free 'em
        for (auto& alloc : allocs) {
            free(alloc);
        }

        // dereference all of the persistent allocations, which should still be mapped
        printf("Iteration %zu, reading %zu persistent allocations\n",
            i, persistent.size());

        uint8_t sum = 0x00;
        for (auto& alloc : persistent) {
            sum += alloc[0];
        }

        printf("Iteration %zu, persistent allocation checksum is 0x%2X\n",
            i, sum);
    }

    return 0;
}
