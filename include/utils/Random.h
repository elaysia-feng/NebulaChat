#pragma once
#include <random>

namespace utils {

    inline int RandInt(int min, int max) {
    thread_local std::mt19937 rng{ std::random_device{}() };
    std::uniform_int_distribution<int> dist(min, max);
    return dist(rng);
    }

    inline int MakeTtlWithJitter (int baseSeconds, int jitterSeconds) {
        if (jitterSeconds <= 0) return baseSeconds;
        int delta = RandInt(0, jitterSeconds);
        return delta + baseSeconds;
    }

} 
