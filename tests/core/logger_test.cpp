// Tests du Logger : niveaux, messages structures, formats texte/JSON, sinks.

#include <atomic>
#include <cstdio>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include "check.hpp"
#include "core/log.hpp"
#include "core/log_sinks.hpp"
#include "log_capture.hpp"
#include "temp_dir.hpp"

using namespace moonlight::core;
using moonlight::testing::LogCapture;
using moonlight::testing::TempDir;

namespace {

std::vector<std::string> readLines(const std::string& path) {
    std::vector<std::string> lines;
    std::ifstream stream(path, std::ios::binary);
    std::string line;
    while (std::getline(stream, line)) {
        lines.push_back(line);
    }
    return lines;
}

void testLevelNamesAndParsing() {
    CHECK(std::string(logLevelName(LogLevel::Trace)) == "trace");
    CHECK(std::string(logLevelName(LogLevel::Debug)) == "debug");
    CHECK(std::string(logLevelName(LogLevel::Info)) == "info");
    CHECK(std::string(logLevelName(LogLevel::Warning)) == "warning");
    CHECK(std::string(logLevelName(LogLevel::Error)) == "error");

    LogLevel level = LogLevel::Info;
    CHECK(parseLogLevel("debug", level) && level == LogLevel::Debug);
    CHECK(parseLogLevel("ERROR", level) && level == LogLevel::Error);
    CHECK(parseLogLevel("Warning", level) && level == LogLevel::Warning);
    level = LogLevel::Info;
    CHECK(!parseLogLevel("warn", level));      // alias non supporte : explicite
    CHECK(!parseLogLevel("", level));
    CHECK(!parseLogLevel("debugg", level));
    CHECK(level == LogLevel::Info);            // inchange en cas d'echec

    LogFormat format = LogFormat::Text;
    CHECK(parseLogFormat("JSON", format) && format == LogFormat::Json);
    CHECK(parseLogFormat("text", format) && format == LogFormat::Text);
    CHECK(!parseLogFormat("xml", format));
    CHECK(std::string(logFormatName(LogFormat::Json)) == "json");
}

void testLevelFiltering() {
    LogCapture capture(LogLevel::Warning);
    MOONLIGHT_LOG_DEBUG("t", "debug hidden");
    MOONLIGHT_LOG_INFO("t", "info hidden");
    MOONLIGHT_LOG_WARN("t", "warning shown");
    MOONLIGHT_LOG_ERROR("t", "error shown");
    CHECK(capture.count() == 2);
    CHECK(capture.contains(LogLevel::Warning, "warning shown"));
    CHECK(capture.contains(LogLevel::Error, "error shown"));

    Logger::instance().setMinLevel(LogLevel::Debug);
    MOONLIGHT_LOG_DEBUG("t", "debug now shown");
    CHECK(capture.contains(LogLevel::Debug, "debug now shown"));
}

void testStructuredFieldsReachTheSink() {
    LogCapture capture(LogLevel::Info);
    const std::string size = std::to_string(42);
    MOONLIGHT_LOG_FIELDS(LogLevel::Info, "assets", "resource loaded",
                         {"path", "textures/a.png"}, {"bytes", size});
    const auto records = capture.records();
    CHECK(records.size() == 1);
    if (records.size() == 1) {
        CHECK(records[0].category == "assets");
        CHECK(records[0].message == "resource loaded");
        CHECK(records[0].fields.size() == 2);
        if (records[0].fields.size() == 2) {
            CHECK(records[0].fields[0].first == "path");
            CHECK(records[0].fields[0].second == "textures/a.png");
            CHECK(records[0].fields[1].first == "bytes");
            CHECK(records[0].fields[1].second == "42");
        }
    }

    // Messages classiques : aucun champ.
    capture.clear();
    MOONLIGHT_LOG_INFO("t", "plain %d", 1);
    CHECK(capture.count() == 1);
    CHECK(capture.records()[0].fields.empty());
}

void testFieldsAreNotEvaluatedWhenLevelDisabled() {
    LogCapture capture(LogLevel::Error);
    int evaluations = 0;
    auto expensive = [&evaluations]() {
        ++evaluations;
        return std::string("value");
    };
    MOONLIGHT_LOG_FIELDS(LogLevel::Debug, "t", "skipped", {"k", expensive()});
    CHECK(evaluations == 0);
    CHECK(capture.count() == 0);
    MOONLIGHT_LOG_FIELDS(LogLevel::Error, "t", "emitted", {"k", expensive()});
    CHECK(evaluations == 1);
    CHECK(capture.count() == 1);
}

void testRecordIsBackwardCompatible() {
    // Le code 0.1.0 construisait LogRecord avec trois valeurs.
    const LogRecord record{LogLevel::Info, "cat", "msg"};
    CHECK(record.fields.empty());
    CHECK(record.uptimeSeconds == 0.0);
}

void testTextFormat() {
    LogRecord record{LogLevel::Info, "engine", "hello"};
    record.uptimeSeconds = 1.5;
    CHECK(formatLogRecord(record, LogFormat::Text) == "[    1.500] [INFO ] [engine] hello");

    const LogField fields[] = {{"plain", "abc"},
                               {"spaced", "a b"},
                               {"empty", ""},
                               {"quote", "say \"hi\""},
                               {"equals", "a=b"},
                               {"newline", "l1\nl2"},
                               {"control", "\x01"}};
    record.level = LogLevel::Warning;
    record.fields = fields;
    CHECK(formatLogRecord(record, LogFormat::Text) ==
          "[    1.500] [WARN ] [engine] hello plain=abc spaced=\"a b\" empty=\"\" "
          "quote=\"say \\\"hi\\\"\" equals=\"a=b\" newline=\"l1\\nl2\" control=\"\\x01\"");
}

void testTextFormatMatchesLegacyDefaultFormat() {
    // En 0.1.0 le sink par defaut utilisait printf("[%9.3f] [%-5s] [%.*s] %.*s").
    // Le nouveau formateur doit produire exactement la meme ligne.
    const double uptimes[] = {0.0, 0.0004, 0.0005, 1.0, 12.3456, 999.9994, 12345.678, 1234567.891};
    const LogLevel levels[] = {LogLevel::Trace, LogLevel::Debug, LogLevel::Info,
                               LogLevel::Warning, LogLevel::Error};
    for (const double uptime : uptimes) {
        for (const LogLevel level : levels) {
            LogRecord record{level, "engine", "some message"};
            record.uptimeSeconds = uptime;
            char expected[256];
            std::snprintf(expected, sizeof(expected), "[%9.3f] [%-5s] [%s] %s", uptime,
                          toString(level), "engine", "some message");
            CHECK(formatLogRecord(record, LogFormat::Text) == expected);
        }
    }
}

void testJsonFormat() {
    LogRecord record{LogLevel::Error, "assets", "disk \"full\""};
    record.uptimeSeconds = 2.25;
    CHECK(formatLogRecord(record, LogFormat::Json) ==
          "{\"uptime\":2.250,\"level\":\"error\",\"category\":\"assets\","
          "\"message\":\"disk \\\"full\\\"\",\"fields\":{}}");

    const LogField fields[] = {{"path", "a\\b"}, {"note", "tab\there\nline\x01"}, {"utf8", "caf\xC3\xA9"}};
    record.fields = fields;
    record.level = LogLevel::Info;
    CHECK(formatLogRecord(record, LogFormat::Json) ==
          "{\"uptime\":2.250,\"level\":\"info\",\"category\":\"assets\","
          "\"message\":\"disk \\\"full\\\"\",\"fields\":{\"path\":\"a\\\\b\","
          "\"note\":\"tab\\there\\nline\\u0001\",\"utf8\":\"caf\xC3\xA9\"}}");
}

void testCustomSinkMayLogWithoutDeadlock() {
    // Le sink est appele hors verrou : il peut lui-meme journaliser.
    Logger& logger = Logger::instance();
    logger.setMinLevel(LogLevel::Info);
    std::atomic<int> calls{0};
    logger.setSink([&calls](const LogRecord& record) {
        if (calls.fetch_add(1) == 0 && record.message == "outer") {
            MOONLIGHT_LOG_INFO("t", "inner");
        }
    });
    MOONLIGHT_LOG_INFO("t", "outer");
    logger.resetSink();
    CHECK(calls.load() == 2);
}

void testFileSinkWritesJsonLines() {
    TempDir dir;
    CHECK(dir.isValid());
    const std::string path = dir.file("log.jsonl");

    Result<LogSink> sink = makeFileSink(path, LogFormat::Json);
    CHECK_OK(sink);
    if (!sink.isOk()) {
        return;
    }
    Logger& logger = Logger::instance();
    logger.setMinLevel(LogLevel::Info);
    logger.setSink(sink.value());
    MOONLIGHT_LOG_INFO("t", "first");
    MOONLIGHT_LOG_FIELDS(LogLevel::Warning, "t", "second", {"k", "v"});
    logger.resetSink();

    const std::vector<std::string> lines = readLines(path);
    CHECK(lines.size() == 2);
    if (lines.size() == 2) {
        CHECK(lines[0].find("\"message\":\"first\"") != std::string::npos);
        CHECK(lines[0].find("\"level\":\"info\"") != std::string::npos);
        CHECK(lines[1].find("\"message\":\"second\"") != std::string::npos);
        CHECK(lines[1].find("\"fields\":{\"k\":\"v\"}") != std::string::npos);
        CHECK(lines[1].front() == '{' && lines[1].back() == '}');
    }
}

void testFileSinkAppends() {
    TempDir dir;
    CHECK(dir.isValid());
    const std::string path = dir.file("append.log");
    LogRecord record{LogLevel::Info, "t", "line"};

    for (int i = 0; i < 2; ++i) {
        Result<LogSink> sink = makeFileSink(path, LogFormat::Text);
        CHECK_OK(sink);
        if (sink.isOk()) {
            sink.value()(record);
        }
    }  // le fichier est ferme a la destruction du sink
    CHECK(readLines(path).size() == 2);
}

void testFileSinkErrors() {
    TempDir dir;
    CHECK_CODE(makeFileSink("", LogFormat::Text), ErrorCode::InvalidArgument);
    CHECK_CODE(makeFileSink(std::string_view("a\0b", 3), LogFormat::Text),
               ErrorCode::InvalidArgument);
    CHECK_CODE(makeFileSink(dir.file("missing-dir/log.txt"), LogFormat::Text),
               ErrorCode::NotFound);
    // Ouvrir un dossier en ecriture echoue (selon le systeme : IoError ou AccessDenied).
    const Result<LogSink> onDirectory = makeFileSink(dir.path(), LogFormat::Text);
    CHECK(!onDirectory.isOk());
}

void testFileSinkKeepsLinesIntactAcrossThreads() {
    TempDir dir;
    const std::string path = dir.file("threads.log");
    Result<LogSink> sink = makeFileSink(path, LogFormat::Text);
    CHECK_OK(sink);
    if (!sink.isOk()) {
        return;
    }
    const LogSink shared = sink.value();

    constexpr int kThreads = 8;
    constexpr int kLinesPerThread = 200;
    const std::string payload(300, 'x');  // assez long pour reveler un entremelement
    std::vector<std::thread> threads;
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&shared, &payload, t]() {
            for (int i = 0; i < kLinesPerThread; ++i) {
                const std::string message = "thread" + std::to_string(t) + " " + payload;
                shared(LogRecord{LogLevel::Info, "mt", message});
            }
        });
    }
    for (std::thread& thread : threads) {
        thread.join();
    }

    const std::vector<std::string> lines = readLines(path);
    CHECK(lines.size() == static_cast<std::size_t>(kThreads * kLinesPerThread));
    bool allIntact = true;
    for (const std::string& line : lines) {
        allIntact = allIntact && line.find("] [mt] thread") != std::string::npos &&
                    line.size() >= payload.size() && line.compare(line.size() - payload.size(),
                                                                  payload.size(), payload) == 0;
    }
    CHECK(allIntact);
}

}  // namespace

int main() {
    std::printf("moonlight_logger_test\n");
    RUN_TEST(testLevelNamesAndParsing);
    RUN_TEST(testLevelFiltering);
    RUN_TEST(testStructuredFieldsReachTheSink);
    RUN_TEST(testFieldsAreNotEvaluatedWhenLevelDisabled);
    RUN_TEST(testRecordIsBackwardCompatible);
    RUN_TEST(testTextFormat);
    RUN_TEST(testTextFormatMatchesLegacyDefaultFormat);
    RUN_TEST(testJsonFormat);
    RUN_TEST(testCustomSinkMayLogWithoutDeadlock);
    RUN_TEST(testFileSinkWritesJsonLines);
    RUN_TEST(testFileSinkAppends);
    RUN_TEST(testFileSinkErrors);
    RUN_TEST(testFileSinkKeepsLinesIntactAcrossThreads);
    return moonlight::testing::finish("moonlight_logger_test");
}
