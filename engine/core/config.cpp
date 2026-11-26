#include "core/config.hpp"

#include <charconv>
#include <cmath>
#include <set>
#include <system_error>
#include <utility>

#include "core/text_util.hpp"

namespace moonlight::core {

namespace {

constexpr std::string_view kUtf8Bom = "\xEF\xBB\xBF";

constexpr const char* kKeyLogLevel = "log.level";
constexpr const char* kKeyLogFormat = "log.format";
constexpr const char* kKeyLogFile = "log.file";
constexpr const char* kKeyGameDataDir = "paths.game_data_dir";
constexpr const char* kKeyModsDir = "paths.mods_dir";
constexpr const char* kKeySavesDir = "paths.saves_dir";
constexpr const char* kKeyMaxFrameDelta = "engine.max_frame_delta_seconds";

std::string lowercase(std::string_view text) {
    std::string result(text);
    for (char& c : result) {
        c = text::asciiLower(c);
    }
    return result;
}

Status invalid(std::string message) {
    return Status::error(ErrorCode::InvalidArgument, std::move(message));
}

// --- Validation d'une valeur (partagee entre validateConfig et l'analyseur) ---

Status checkPathValue(std::string_view key, std::string_view value, bool allowEmpty) {
    if (value.empty()) {
        return allowEmpty ? Status::ok() : invalid(std::string(key) + " must not be empty");
    }
    if (value.size() > kMaxConfigPathLength) {
        return invalid(std::string(key) + " is longer than " +
                       std::to_string(kMaxConfigPathLength) + " characters");
    }
    for (const char c : value) {
        if (text::isControl(c)) {
            return invalid(std::string(key) + " contains a control character");
        }
    }
    return Status::ok();
}

Status checkMaxFrameDelta(double value) {
    if (!std::isfinite(value) || value <= 0.0 || value > kMaxFrameDeltaUpperBound) {
        return invalid(std::string(kKeyMaxFrameDelta) + " must be a number in (0, " +
                       std::to_string(static_cast<int>(kMaxFrameDeltaUpperBound)) + "]");
    }
    return Status::ok();
}

Status checkLogLevel(LogLevel level) {
    switch (level) {
        case LogLevel::Trace:
        case LogLevel::Debug:
        case LogLevel::Info:
        case LogLevel::Warning:
        case LogLevel::Error:
            return Status::ok();
    }
    return invalid(std::string(kKeyLogLevel) + " is not a valid level");
}

Status checkLogFormat(LogFormat format) {
    switch (format) {
        case LogFormat::Text:
        case LogFormat::Json:
            return Status::ok();
    }
    return invalid(std::string(kKeyLogFormat) + " is not a valid format");
}

// --- Analyse ---

Status parseErrorAt(int line, const std::string& message) {
    return Status::error(ErrorCode::ParseError, "line " + std::to_string(line) + ": " + message);
}

/// Interprete la partie valeur d'une ligne (deja rognee) : texte brut, ou texte
/// entre guillemets avec echappements \" et \\.
Result<std::string> parseValueText(std::string_view raw) {
    if (raw.empty() || raw.front() != '"') {
        return std::string(raw);
    }
    if (raw.size() < 2 || raw.back() != '"') {
        return Status::error(ErrorCode::ParseError, "unterminated quoted value");
    }
    std::string result;
    const std::size_t last = raw.size() - 1;  // indice du guillemet fermant
    for (std::size_t i = 1; i < last; ++i) {
        const char c = raw[i];
        if (c == '\\') {
            if (i + 1 >= last) {
                return Status::error(ErrorCode::ParseError, "unterminated quoted value");
            }
            const char next = raw[i + 1];
            if (next != '\\' && next != '"') {
                return Status::error(ErrorCode::ParseError,
                                     std::string("unsupported escape sequence '\\") + next + "'");
            }
            result.push_back(next);
            ++i;
        } else if (c == '"') {
            return Status::error(ErrorCode::ParseError,
                                 "unescaped '\"' inside a quoted value");
        } else {
            result.push_back(c);
        }
    }
    return result;
}

bool parseDouble(std::string_view text, double& out) {
    if (text.empty()) {
        return false;
    }
    const char* begin = text.data();
    const char* end = begin + text.size();
    double value = 0.0;
    const std::from_chars_result result = std::from_chars(begin, end, value);
    if (result.ec != std::errc() || result.ptr != end) {
        return false;
    }
    out = value;
    return true;
}

/// Affecte `value` a la cle `section.key`. Les erreurs n'ont pas de numero de ligne
/// (l'appelant l'ajoute).
Status assignKey(Config& config, const std::string& section, const std::string& key,
                 const std::string& value) {
    if (section == "log") {
        if (key == "level") {
            if (!parseLogLevel(value, config.log.minLevel)) {
                return invalid(std::string(kKeyLogLevel) +
                               " must be one of: trace, debug, info, warning, error (got '" +
                               value + "')");
            }
            return Status::ok();
        }
        if (key == "format") {
            if (!parseLogFormat(value, config.log.format)) {
                return invalid(std::string(kKeyLogFormat) +
                               " must be one of: text, json (got '" + value + "')");
            }
            return Status::ok();
        }
        if (key == "file") {
            if (Status s = checkPathValue(kKeyLogFile, value, /*allowEmpty=*/true); !s.isOk()) {
                return s;
            }
            config.log.filePath = value;
            return Status::ok();
        }
    } else if (section == "paths") {
        if (key == "game_data_dir") {
            if (Status s = checkPathValue(kKeyGameDataDir, value, true); !s.isOk()) {
                return s;
            }
            config.paths.gameDataDir = value;
            return Status::ok();
        }
        if (key == "mods_dir") {
            if (Status s = checkPathValue(kKeyModsDir, value, false); !s.isOk()) {
                return s;
            }
            config.paths.modsDir = value;
            return Status::ok();
        }
        if (key == "saves_dir") {
            if (Status s = checkPathValue(kKeySavesDir, value, false); !s.isOk()) {
                return s;
            }
            config.paths.savesDir = value;
            return Status::ok();
        }
    } else if (section == "engine") {
        if (key == "max_frame_delta_seconds") {
            double number = 0.0;
            if (!parseDouble(value, number)) {
                return invalid(std::string(kKeyMaxFrameDelta) + " must be a number (got '" +
                               value + "')");
            }
            if (Status s = checkMaxFrameDelta(number); !s.isOk()) {
                return s;
            }
            config.engine.maxFrameDeltaSeconds = number;
            return Status::ok();
        }
    }
    return invalid("unknown key '" + key + "' in section [" + section + "]");
}

bool isKnownSection(const std::string& section) {
    return section == "log" || section == "paths" || section == "engine";
}

// --- Serialisation ---

void appendValueLine(std::string& out, std::string_view key, std::string_view value) {
    out.append(key);
    out.append(" =");
    if (value.empty()) {
        out.push_back('\n');
        return;
    }
    out.push_back(' ');
    const bool needsQuotes =
        value.front() == '"' || text::isBlank(value.front()) || text::isBlank(value.back());
    if (!needsQuotes) {
        out.append(value);
    } else {
        out.push_back('"');
        for (const char c : value) {
            if (c == '"' || c == '\\') {
                out.push_back('\\');
            }
            out.push_back(c);
        }
        out.push_back('"');
    }
    out.push_back('\n');
}

std::string formatDouble(double value) {
    char buffer[64];
    const std::to_chars_result result = std::to_chars(buffer, buffer + sizeof(buffer), value);
    return result.ec == std::errc() ? std::string(buffer, result.ptr) : std::string("0.25");
}

}  // namespace

Status validateConfig(const Config& config) {
    if (Status s = checkLogLevel(config.log.minLevel); !s.isOk()) {
        return s;
    }
    if (Status s = checkLogFormat(config.log.format); !s.isOk()) {
        return s;
    }
    if (Status s = checkPathValue(kKeyLogFile, config.log.filePath, true); !s.isOk()) {
        return s;
    }
    if (Status s = checkPathValue(kKeyGameDataDir, config.paths.gameDataDir, true); !s.isOk()) {
        return s;
    }
    if (Status s = checkPathValue(kKeyModsDir, config.paths.modsDir, false); !s.isOk()) {
        return s;
    }
    if (Status s = checkPathValue(kKeySavesDir, config.paths.savesDir, false); !s.isOk()) {
        return s;
    }
    return checkMaxFrameDelta(config.engine.maxFrameDeltaSeconds);
}

Result<Config> parseConfig(std::string_view input) {
    if (input.size() > kMaxConfigBytes) {
        return Status::error(ErrorCode::TooLarge,
                             "configuration is larger than " + std::to_string(kMaxConfigBytes) +
                                 " bytes");
    }
    if (input.find('\0') != std::string_view::npos) {
        return Status::error(ErrorCode::ParseError, "configuration contains a NUL byte");
    }
    if (input.substr(0, kUtf8Bom.size()) == kUtf8Bom) {
        input.remove_prefix(kUtf8Bom.size());
    }

    Config config;
    std::string section;  // vide tant qu'aucun en-tete [section] n'a ete lu
    std::set<std::string> seenSections;
    std::set<std::string> seenKeys;

    int lineNumber = 0;
    std::size_t position = 0;
    while (position <= input.size()) {
        const std::size_t newline = input.find('\n', position);
        std::string_view line = (newline == std::string_view::npos)
                                    ? input.substr(position)
                                    : input.substr(position, newline - position);
        position = (newline == std::string_view::npos) ? input.size() + 1 : newline + 1;
        ++lineNumber;

        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }
        line = text::trim(line);
        if (line.empty() || line.front() == '#' || line.front() == ';') {
            continue;
        }

        if (line.front() == '[') {
            if (line.back() != ']') {
                return parseErrorAt(lineNumber, "section header must end with ']'");
            }
            const std::string_view name = text::trim(line.substr(1, line.size() - 2));
            if (name.empty() || name.find_first_of("[]") != std::string_view::npos) {
                return parseErrorAt(lineNumber, "invalid section name");
            }
            section = lowercase(name);
            if (!isKnownSection(section)) {
                return parseErrorAt(lineNumber, "unknown section [" + section + "]");
            }
            if (!seenSections.insert(section).second) {
                return parseErrorAt(lineNumber, "duplicate section [" + section + "]");
            }
            continue;
        }

        const std::size_t equals = line.find('=');
        if (equals == std::string_view::npos) {
            return parseErrorAt(lineNumber, "expected 'key = value' or a [section] header");
        }
        const std::string_view rawKey = text::trim(line.substr(0, equals));
        if (rawKey.empty()) {
            return parseErrorAt(lineNumber, "missing key before '='");
        }
        if (section.empty()) {
            return parseErrorAt(lineNumber, "key '" + std::string(rawKey) +
                                                "' appears before any [section]");
        }
        const std::string key = lowercase(rawKey);
        if (!seenKeys.insert(section + "." + key).second) {
            return parseErrorAt(lineNumber, "duplicate key '" + key + "' in section [" +
                                                section + "]");
        }

        const Result<std::string> value = parseValueText(text::trim(line.substr(equals + 1)));
        if (!value.isOk()) {
            return parseErrorAt(lineNumber, value.status().message());
        }
        if (Status assigned = assignKey(config, section, key, value.value()); !assigned.isOk()) {
            return parseErrorAt(lineNumber, assigned.message());
        }
    }

    // Filet de securite : l'ensemble doit rester valide.
    if (Status s = validateConfig(config); !s.isOk()) {
        return s;
    }
    return config;
}

std::string serializeConfig(const Config& config) {
    std::string out;
    out.append("# MOONLIGHT configuration\n");
    out.append("# Lines starting with '#' or ';' are comments.\n");
    out.append("\n[log]\n");
    out.append("# level: trace | debug | info | warning | error\n");
    appendValueLine(out, "level", logLevelName(config.log.minLevel));
    out.append("# format: text | json\n");
    appendValueLine(out, "format", logFormatName(config.log.format));
    out.append("# file: path of the log file; empty = write to stderr\n");
    appendValueLine(out, "file", config.log.filePath);

    out.append("\n[paths]\n");
    out.append("# game_data_dir: folder with YOUR OWN game data (empty = not configured)\n");
    appendValueLine(out, "game_data_dir", config.paths.gameDataDir);
    appendValueLine(out, "mods_dir", config.paths.modsDir);
    appendValueLine(out, "saves_dir", config.paths.savesDir);

    out.append("\n[engine]\n");
    out.append("# max_frame_delta_seconds: largest time step accepted per update, in (0, 10]\n");
    appendValueLine(out, "max_frame_delta_seconds", formatDouble(config.engine.maxFrameDeltaSeconds));
    return out;
}

EngineConfig makeEngineConfig(const Config& config) {
    EngineConfig engineConfig;
    engineConfig.maxFrameDeltaSeconds = config.engine.maxFrameDeltaSeconds;
    return engineConfig;
}

Status applyLogConfig(const LogConfig& config, Logger& logger) {
    if (Status s = checkLogLevel(config.minLevel); !s.isOk()) {
        return s;
    }
    if (Status s = checkLogFormat(config.format); !s.isOk()) {
        return s;
    }
    if (Status s = checkPathValue(kKeyLogFile, config.filePath, true); !s.isOk()) {
        return s;
    }

    // Tout ou rien : on ouvre la destination AVANT de toucher au Logger.
    LogSink sink;
    if (config.filePath.empty()) {
        sink = makeStderrSink(config.format);
    } else {
        Result<LogSink> fileSink = makeFileSink(config.filePath, config.format);
        if (!fileSink.isOk()) {
            return fileSink.status();
        }
        sink = std::move(fileSink).value();
    }
    logger.setSink(std::move(sink));
    logger.setMinLevel(config.minLevel);
    return Status::ok();
}

}  // namespace moonlight::core
