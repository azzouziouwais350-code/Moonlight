#pragma once

#include "platform/clock.hpp"

namespace moonlight::platform {

/// Horloge reelle de la machine hote (std::chrono::steady_clock et system_clock).
class HostClock final : public IClock {
public:
    HostClock() = default;

    std::chrono::nanoseconds monotonicNow() const override;
    std::chrono::system_clock::time_point systemNow() const override;
};

}  // namespace moonlight::platform
