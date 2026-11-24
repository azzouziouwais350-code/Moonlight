#include "core/log.hpp"

#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <string>
#include <utility>

namespace moonlight::core {

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

    dispatch(LogRecord{level, category, message});
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
    const std::chrono::duration<double> elapsed = std::chrono::steady_clock::now() - startTime_;
    std::fprintf(stderr, "[%9.3f] [%-5s] [%.*s] %.*s\n",
                 elapsed.count(),
                 toString(record.level),
                 static_cast<int>(record.category.size()), record.category.data(),
                 static_cast<int>(record.message.size()), record.message.data());
}

}  // namespace moonlight::core
