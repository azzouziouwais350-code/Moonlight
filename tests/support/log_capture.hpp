// Capture des logs du Logger global pendant la duree de vie de l'objet.
// Restaure le sink par defaut et le niveau minimal a la destruction.
#pragma once

#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/log.hpp"

namespace moonlight::testing {

struct CapturedRecord {
    moonlight::core::LogLevel level;
    std::string category;
    std::string message;
    std::vector<std::pair<std::string, std::string>> fields;
};

class LogCapture {
public:
    explicit LogCapture(moonlight::core::LogLevel minLevel = moonlight::core::LogLevel::Trace)
        : previousMinLevel_(moonlight::core::Logger::instance().minLevel()) {
        moonlight::core::Logger& logger = moonlight::core::Logger::instance();
        logger.setMinLevel(minLevel);
        logger.setSink([this](const moonlight::core::LogRecord& record) {
            CapturedRecord copy{record.level, std::string(record.category),
                                std::string(record.message), {}};
            for (const moonlight::core::LogField& field : record.fields) {
                copy.fields.emplace_back(std::string(field.key), std::string(field.value));
            }
            const std::lock_guard<std::mutex> lock(mutex_);
            records_.push_back(std::move(copy));
        });
    }

    ~LogCapture() {
        moonlight::core::Logger& logger = moonlight::core::Logger::instance();
        logger.resetSink();
        logger.setMinLevel(previousMinLevel_);
    }

    LogCapture(const LogCapture&) = delete;
    LogCapture& operator=(const LogCapture&) = delete;

    std::vector<CapturedRecord> records() const {
        const std::lock_guard<std::mutex> lock(mutex_);
        return records_;
    }

    std::size_t count() const {
        const std::lock_guard<std::mutex> lock(mutex_);
        return records_.size();
    }

    /// Vrai si un message de ce niveau contient `text`.
    bool contains(moonlight::core::LogLevel level, std::string_view text) const {
        const std::lock_guard<std::mutex> lock(mutex_);
        for (const CapturedRecord& record : records_) {
            if (record.level == level && record.message.find(text) != std::string::npos) {
                return true;
            }
        }
        return false;
    }

    void clear() {
        const std::lock_guard<std::mutex> lock(mutex_);
        records_.clear();
    }

private:
    mutable std::mutex mutex_;
    std::vector<CapturedRecord> records_;
    moonlight::core::LogLevel previousMinLevel_;
};

}  // namespace moonlight::testing
