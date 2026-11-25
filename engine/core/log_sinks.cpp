#include "core/log_sinks.hpp"

#include <cerrno>
#include <charconv>
#include <cstddef>
#include <cstdio>
#include <memory>
#include <mutex>
#include <system_error>
#include <utility>

#include "core/text_util.hpp"

namespace moonlight::core {

namespace {

constexpr std::size_t kUptimeColumnWidth = 9;
constexpr std::size_t kLevelColumnWidth = 5;

/// Ecrit `value` avec 3 decimales (independant de la locale).
std::string fixed3(double value) {
    char buffer[64];
    const std::to_chars_result result =
        std::to_chars(buffer, buffer + sizeof(buffer), value, std::chars_format::fixed, 3);
    if (result.ec != std::errc()) {
        return "nan";
    }
    return std::string(buffer, static_cast<std::size_t>(result.ptr - buffer));
}

bool textValueNeedsQuotes(std::string_view value) {
    if (value.empty()) {
        return true;
    }
    for (const char c : value) {
        if (c == ' ' || c == '"' || c == '=' || c == '\\' || text::isControl(c)) {
            return true;
        }
    }
    return false;
}

constexpr char kHexDigits[] = "0123456789abcdef";

void appendHexByte(std::string& out, char c) {
    const auto byte = static_cast<unsigned char>(c);
    out.push_back(kHexDigits[byte >> 4]);
    out.push_back(kHexDigits[byte & 0x0F]);
}

void appendTextValue(std::string& out, std::string_view value) {
    if (!textValueNeedsQuotes(value)) {
        out.append(value);
        return;
    }
    out.push_back('"');
    for (const char c : value) {
        switch (c) {
            case '"':  out.append("\\\""); break;
            case '\\': out.append("\\\\"); break;
            case '\n': out.append("\\n"); break;
            case '\r': out.append("\\r"); break;
            case '\t': out.append("\\t"); break;
            default:
                if (text::isControl(c)) {
                    out.append("\\x");
                    appendHexByte(out, c);
                } else {
                    out.push_back(c);
                }
        }
    }
    out.push_back('"');
}

void appendJsonString(std::string& out, std::string_view value) {
    out.push_back('"');
    for (const char c : value) {
        switch (c) {
            case '"':  out.append("\\\""); break;
            case '\\': out.append("\\\\"); break;
            case '\b': out.append("\\b"); break;
            case '\f': out.append("\\f"); break;
            case '\n': out.append("\\n"); break;
            case '\r': out.append("\\r"); break;
            case '\t': out.append("\\t"); break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    out.append("\\u00");
                    appendHexByte(out, c);
                } else {
                    out.push_back(c);  // y compris 0x7F et les octets UTF-8
                }
        }
    }
    out.push_back('"');
}

std::string formatText(const LogRecord& record) {
    std::string out;
    out.reserve(64 + record.message.size());

    const std::string uptime = fixed3(record.uptimeSeconds);
    out.push_back('[');
    if (uptime.size() < kUptimeColumnWidth) {
        out.append(kUptimeColumnWidth - uptime.size(), ' ');
    }
    out.append(uptime);

    const std::string_view level = toString(record.level);
    out.append("] [");
    out.append(level);
    if (level.size() < kLevelColumnWidth) {
        out.append(kLevelColumnWidth - level.size(), ' ');
    }
    out.append("] [");
    out.append(record.category);
    out.append("] ");
    out.append(record.message);

    for (const LogField& field : record.fields) {
        out.push_back(' ');
        out.append(field.key);
        out.push_back('=');
        appendTextValue(out, field.value);
    }
    return out;
}

std::string formatJson(const LogRecord& record) {
    std::string out;
    out.reserve(96 + record.message.size());

    out.append("{\"uptime\":");
    out.append(fixed3(record.uptimeSeconds));
    out.append(",\"level\":");
    appendJsonString(out, logLevelName(record.level));
    out.append(",\"category\":");
    appendJsonString(out, record.category);
    out.append(",\"message\":");
    appendJsonString(out, record.message);
    out.append(",\"fields\":{");
    bool first = true;
    for (const LogField& field : record.fields) {
        if (!first) {
            out.push_back(',');
        }
        first = false;
        appendJsonString(out, field.key);
        out.push_back(':');
        appendJsonString(out, field.value);
    }
    out.append("}}");
    return out;
}

/// Fichier ouvert + verrou, partage entre les copies du sink.
struct FileState {
    std::FILE* file = nullptr;
    std::mutex mutex;

    FileState() = default;
    FileState(const FileState&) = delete;
    FileState& operator=(const FileState&) = delete;
    ~FileState() {
        if (file != nullptr) {
            std::fclose(file);
        }
    }
};

}  // namespace

const char* logFormatName(LogFormat format) noexcept {
    switch (format) {
        case LogFormat::Text: return "text";
        case LogFormat::Json: return "json";
    }
    return "unknown";
}

bool parseLogFormat(std::string_view name, LogFormat& out) noexcept {
    for (const LogFormat candidate : {LogFormat::Text, LogFormat::Json}) {
        if (text::equalsIgnoreCase(name, logFormatName(candidate))) {
            out = candidate;
            return true;
        }
    }
    return false;
}

std::string formatLogRecord(const LogRecord& record, LogFormat format) {
    return format == LogFormat::Json ? formatJson(record) : formatText(record);
}

LogSink makeStderrSink(LogFormat format) {
    return [format](const LogRecord& record) {
        std::string line = formatLogRecord(record, format);
        line.push_back('\n');
        std::fwrite(line.data(), 1, line.size(), stderr);  // un seul appel : pas d'entremelement
    };
}

Result<LogSink> makeFileSink(std::string_view path, LogFormat format) {
    if (path.empty() || path.find('\0') != std::string_view::npos) {
        return Status::error(ErrorCode::InvalidArgument,
                             "log file path must be non-empty and contain no NUL byte");
    }

    const std::string pathString(path);
    auto state = std::make_shared<FileState>();
    state->file = std::fopen(pathString.c_str(), "ab");
    if (state->file == nullptr) {
        const int savedErrno = errno;
        const std::error_code ec(savedErrno, std::generic_category());
        ErrorCode code = ErrorCode::IoError;
        if (ec == std::errc::no_such_file_or_directory) {
            code = ErrorCode::NotFound;
        } else if (ec == std::errc::permission_denied) {
            code = ErrorCode::AccessDenied;
        }
        return Status::error(code, "cannot open log file '" + pathString + "': " + ec.message());
    }

    return LogSink([state, format](const LogRecord& record) {
        std::string line = formatLogRecord(record, format);
        line.push_back('\n');
        const std::lock_guard<std::mutex> lock(state->mutex);
        std::fwrite(line.data(), 1, line.size(), state->file);
        std::fflush(state->file);
    });
}

}  // namespace moonlight::core
