// Horloge controlee par le test : le temps n'avance que si on le demande.
#pragma once

#include <chrono>

#include "platform/clock.hpp"

namespace moonlight::testing {

class ManualClock final : public moonlight::platform::IClock {
public:
    std::chrono::nanoseconds monotonicNow() const override { return monotonic_; }
    std::chrono::system_clock::time_point systemNow() const override { return system_; }

    void advance(std::chrono::nanoseconds delta) {
        monotonic_ += delta;
        system_ += std::chrono::duration_cast<std::chrono::system_clock::duration>(delta);
    }
    void setSystemTime(std::chrono::system_clock::time_point time) { system_ = time; }

private:
    std::chrono::nanoseconds monotonic_{0};
    std::chrono::system_clock::time_point system_{};
};

}  // namespace moonlight::testing
