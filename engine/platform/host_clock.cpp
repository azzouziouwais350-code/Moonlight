#include "platform/host_clock.hpp"

namespace moonlight::platform {

std::chrono::nanoseconds HostClock::monotonicNow() const {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch());
}

std::chrono::system_clock::time_point HostClock::systemNow() const {
    return std::chrono::system_clock::now();
}

}  // namespace moonlight::platform
