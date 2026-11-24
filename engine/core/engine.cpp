#include "core/engine.hpp"

#include <cmath>
#include <utility>

#include "core/log.hpp"
#include "core/version.hpp"

namespace moonlight::core {

namespace {
constexpr const char* kLogCategory = "engine";
}  // namespace

const char* toString(Engine::State state) noexcept {
    switch (state) {
        case Engine::State::Created:     return "Created";
        case Engine::State::Initialized: return "Initialized";
        case Engine::State::ShutDown:    return "ShutDown";
    }
    return "?";
}

Engine::Engine(EngineConfig config) : config_(std::move(config)) {}

Engine::~Engine() {
    if (state_ == State::Initialized) {
        MOONLIGHT_LOG_WARN(kLogCategory, "Engine destroyed without shutdown(); shutting down now");
        shutdown();
    }
}

Status Engine::initialize() {
    if (state_ == State::Initialized) {
        MOONLIGHT_LOG_ERROR(kLogCategory, "initialize() called on an already initialized engine");
        return Status::error(ErrorCode::InvalidState,
                             "Engine::initialize() called on an already initialized engine");
    }
    if (!std::isfinite(config_.maxFrameDeltaSeconds) || config_.maxFrameDeltaSeconds <= 0.0) {
        MOONLIGHT_LOG_ERROR(kLogCategory, "Invalid config: maxFrameDeltaSeconds must be finite and > 0");
        return Status::error(ErrorCode::InvalidArgument,
                             "EngineConfig::maxFrameDeltaSeconds must be finite and > 0");
    }

    frameCount_ = 0;
    elapsedSeconds_ = 0.0;
    state_ = State::Initialized;

    MOONLIGHT_LOG_INFO(kLogCategory,
                       "%s %s initialized (core only: no graphics, audio, input or game subsystem yet)",
                       config_.applicationName.c_str(), MOONLIGHT_VERSION_STRING);
    return Status::ok();
}

Status Engine::update(double deltaSeconds) {
    if (state_ != State::Initialized) {
        MOONLIGHT_LOG_ERROR(kLogCategory, "update() called while engine state is %s", toString(state_));
        return Status::error(ErrorCode::InvalidState,
                             "Engine::update() requires an initialized engine");
    }
    if (!std::isfinite(deltaSeconds) || deltaSeconds < 0.0) {
        MOONLIGHT_LOG_ERROR(kLogCategory, "update() called with invalid delta %f", deltaSeconds);
        return Status::error(ErrorCode::InvalidArgument,
                             "Engine::update() delta must be finite and >= 0");
    }
    if (deltaSeconds > config_.maxFrameDeltaSeconds) {
        MOONLIGHT_LOG_DEBUG(kLogCategory, "Frame delta %f s clamped to %f s",
                            deltaSeconds, config_.maxFrameDeltaSeconds);
        deltaSeconds = config_.maxFrameDeltaSeconds;
    }

    ++frameCount_;
    elapsedSeconds_ += deltaSeconds;
    return Status::ok();
}

void Engine::shutdown() {
    if (state_ != State::Initialized) {
        MOONLIGHT_LOG_DEBUG(kLogCategory, "shutdown() ignored: engine state is %s", toString(state_));
        return;
    }
    MOONLIGHT_LOG_INFO(kLogCategory, "Shutting down after %llu frame(s), %.3f s simulated",
                       static_cast<unsigned long long>(frameCount_), elapsedSeconds_);
    state_ = State::ShutDown;
}

}  // namespace moonlight::core
