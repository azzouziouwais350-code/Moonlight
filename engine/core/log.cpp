#include "core/log.hpp"

#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <string>
#include <utility>

#include "core/log_sinks.hpp"
#include "core/text_util.hpp"

namespace moonlight::core {

const char* logLevelName(LogLevel level) noexcept {
    switch (level) {
        case LogLevel::Trace:   return "trace";
        case LogLevel::Debug:   return "debug";
        case LogLevel::Info:    return "info";
        case LogLevel::Warning: return "warning";
        case LogLevel::Error:   return "error";
    }
    return "unknown";
}

bool parseLogLevel(std::string_view name, LogLevel& out) noexcept {
    for (const LogLevel candidate : {LogLevel::Trace, LogLevel::Debug, LogLevel::Info,
                                     LogLevel::Warning, LogLevel::Error}) {
        if (text::equalsIgnoreCase(name, logLevelName(candidate))) {
            out = candidate;
            return true;
        }
    }
    return false;
}

const char* toString(LogLevel level) noexcept {
    switch (level) {
        case LogLevel::Trace:   return "TRACE";
        case LogLevel::Debug:   return "DEBUG";
        case LogLevel::Info:    return "INFO";
        case LogLevel::Warning: return "WARN";
        case LogLevel::Error:   return "ERROR";
    }
    return "?";
}

Logger& Logger::instance() {
    static Logger logger;
    return logger;
}

Logger::Logger() : startTime_(std::chrono::steady_clock::now()) {}

double Logger::uptimeSeconds() const {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - startTime_).count();
}

void Logger::setSink(LogSink sink) {
    std::shared_ptr<const LogSink> newSink;
    if (sink) {
        newSink = std::make_shared<const LogSink>(std::move(sink));
    }
    std::lock_guard<std::mutex> lock(sinkMutex_);
    sink_ = std::move(newSink);
}

void Logger::log(LogLevel level, std::string_view category, const char* format, ...) {
    if (!isEnabled(level)) {
        return;
    }

    // Formatage dans un tampon de pile ; repli sur le tas si le message est plus long.
    char stackBuffer[1024];
    std::va_list args;
    va_start(args, format);
    std::va_list argsCopy;
    va_copy(argsCopy, args);
    const int needed = std::vsnprintf(stackBuffer, sizeof(stackBuffer), format, args);
    va_end(args);

    std::string heapBuffer;
    std::string_view message;
    if (needed < 0) {
        message = "<log formatting error>";
    } else if (static_cast<std::size_t>(needed) < sizeof(stackBuffer)) {
        message = std::string_view(stackBuffer, static_cast<std::size_t>(needed));
    } else {
        heapBuffer.resize(static_cast<std::size_t>(needed) + 1);
        std::vsnprintf(heapBuffer.data(), heapBuffer.size(), format, argsCopy);
        heapBuffer.resize(static_cast<std::size_t>(needed));
        message = heapBuffer;
    }
    va_end(argsCopy);

    LogRecord record{level, category, message};
    record.uptimeSeconds = uptimeSeconds();
    dispatch(record);
}

void Logger::logFields(LogLevel level, std::string_view category, std::string_view message,
                       std::initializer_list<LogField> fields) {
    if (!isEnabled(level)) {
        return;
    }
    LogRecord record{level, category, message};
    record.fields = std::span<const LogField>(fields.begin(), fields.size());
    record.uptimeSeconds = uptimeSeconds();
    dispatch(record);
}

void Logger::dispatch(const LogRecord& record) {
    // On copie le pointeur sous verrou puis on appelle le sink hors verrou :
    // un sink qui journalise lui-meme ne provoque donc pas d'interblocage.
    std::shared_ptr<const LogSink> sink;
    {
        std::lock_guard<std::mutex> lock(sinkMutex_);
        sink = sink_;
    }
    if (sink) {
        (*sink)(record);
    } else {
        writeToStderr(record);
    }
}

void Logger::writeToStderr(const LogRecord& record) const {
    // Meme format qu'en 0.1.0 pour les messages sans champs :
    // "[%9.3f] [%-5s] [categorie] message".
    std::string line = formatLogRecord(record, LogFormat::Text);
    line.push_back('\n');
    std::fwrite(line.data(), 1, line.size(), stderr);
}

}  // namespace moonlight::core
