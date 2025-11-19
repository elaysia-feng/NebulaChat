#include "utils/Random.h"

namespace utils {

int RandInt(int min, int max) {
    thread_local std::mt19937 rng{ std::random_device{}() };
    std::uniform_int_distribution<int> dist(min, max);
    return dist(rng);
}

} // namespace utils
