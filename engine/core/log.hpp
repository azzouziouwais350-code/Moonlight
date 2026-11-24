#pragma once

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <string_view>

// Permet au compilateur de verifier les chaines de format printf des appels de log.
#if defined(__GNUC__) || defined(__clang__)
#define MOONLIGHT_PRINTF_LIKE(formatIndex, firstArgIndex) \
    __attribute__((format(printf, formatIndex, firstArgIndex)))
#else
#define MOONLIGHT_PRINTF_LIKE(formatIndex, firstArgIndex)
#endif

namespace moonlight::core {

enum class LogLevel { Trace, Debug, Info, Warning, Error };

/// Message de log transmis a un "sink". Les string_view ne sont valides que
/// pendant l'appel du sink : copiez-les si vous devez les conserver.
struct LogRecord {
    LogLevel level;
    std::string_view category;
    std::string_view message;
};

using LogSink = std::function<void(const LogRecord&)>;

/// Journal du moteur (singleton, thread-safe).
///
/// Par defaut les messages sont ecrits sur stderr. Les plateformes pourront
/// installer leur propre sink (par exemple logcat sur Android) via setSink().
class Logger {
public:
    static Logger& instance();

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    void setMinLevel(LogLevel level) noexcept { minLevel_.store(level, std::memory_order_relaxed); }
    LogLevel minLevel() const noexcept { return minLevel_.load(std::memory_order_relaxed); }
    bool isEnabled(LogLevel level) const noexcept { return level >= minLevel(); }

    /// Remplace la destination des messages. Un sink vide restaure le sink par defaut.
    void setSink(LogSink sink);
    void resetSink() { setSink(nullptr); }

    /// Formate (style printf) puis transmet un message si son niveau est active.
    void log(LogLevel level, std::string_view category, const char* format, ...)
        MOONLIGHT_PRINTF_LIKE(4, 5);

private:
    Logger();

    void dispatch(const LogRecord& record);
    void writeToStderr(const LogRecord& record) const;

    std::atomic<LogLevel> minLevel_{LogLevel::Info};
    std::mutex sinkMutex_;
    std::shared_ptr<const LogSink> sink_;  // null => sink par defaut (stderr)
    std::chrono::steady_clock::time_point startTime_;
};

const char* toString(LogLevel level) noexcept;

}  // namespace moonlight::core

#define MOONLIGHT_LOG(level, category, ...) \
    ::moonlight::core::Logger::instance().log((level), (category), __VA_ARGS__)

#define MOONLIGHT_LOG_TRACE(category, ...) \
    MOONLIGHT_LOG(::moonlight::core::LogLevel::Trace, category, __VA_ARGS__)
#define MOONLIGHT_LOG_DEBUG(category, ...) \
    MOONLIGHT_LOG(::moonlight::core::LogLevel::Debug, category, __VA_ARGS__)
#define MOONLIGHT_LOG_INFO(category, ...) \
    MOONLIGHT_LOG(::moonlight::core::LogLevel::Info, category, __VA_ARGS__)
#define MOONLIGHT_LOG_WARN(category, ...) \
    MOONLIGHT_LOG(::moonlight::core::LogLevel::Warning, category, __VA_ARGS__)
#define MOONLIGHT_LOG_ERROR(category, ...) \
    MOONLIGHT_LOG(::moonlight::core::LogLevel::Error, category, __VA_ARGS__)
