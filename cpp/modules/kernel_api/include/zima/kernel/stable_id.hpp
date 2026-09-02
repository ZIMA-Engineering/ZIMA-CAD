#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <random>
#include <string>

namespace zima::kernel {

// Process-independent, monotonically generated 128-bit UUIDv7 identity
// rendered as the existing
// compact 32 lowercase hexadecimal characters. Keeping the current width is
// important: stable IDs occur throughout reference keys and persisted graphs,
// so adding a MAC prefix would increase memory, comparisons and file size for
// no useful collision-safety gain. The timestamp plus 74 random process bits
// makes cross-machine/process collisions negligible; an atomic monotonic tail
// preserves creation order relied upon by existing ordered Sketch workflows.
// Ordinary ID creation therefore performs only one relaxed atomic increment.
[[nodiscard]] inline std::string make_stable_id() {
    struct State {
        State() {
            std::random_device source;
            std::array<std::uint32_t, 4> entropy{};
            for (auto& value : entropy) value = source();
            std::seed_seq seed(entropy.begin(), entropy.end());
            std::mt19937_64 generator(seed);
            const auto milliseconds = static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count());
            high = ((milliseconds & UINT64_C(0x0000ffffffffffff)) << 16) |
                UINT64_C(0x0000000000007000) | (generator() & UINT64_C(0x0fff));
            random_tail = generator() & UINT64_C(0x3fffffffffffffff);
        }

        std::uint64_t high{};
        std::uint64_t random_tail{};
        std::atomic<std::uint64_t> sequence{0};
    };
    static State state;

    const auto serial = state.sequence.fetch_add(1, std::memory_order_relaxed);
    const std::uint64_t high = state.high;
    const std::uint64_t low = UINT64_C(0x8000000000000000) |
        ((state.random_tail + serial) & UINT64_C(0x3fffffffffffffff));

    constexpr char digits[] = "0123456789abcdef";
    std::string result(32, '0');
    const auto write = [&](std::uint64_t value, std::size_t offset) {
        for (int nibble = 15; nibble >= 0; --nibble) {
            result[offset + static_cast<std::size_t>(15 - nibble)] =
                digits[(value >> (nibble * 4)) & 0x0f];
        }
    };
    write(high, 0);
    write(low, 16);
    return result;
}

}  // namespace zima::kernel
