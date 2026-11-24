// Test de fumee de MOONLIGHT 0.1.0.
//
// Verifie reellement le cycle de vie du moteur (initialize/update/shutdown),
// la gestion d'erreurs et le systeme de logs. Retourne 0 si tout passe, 1 sinon.
// Il ne teste AUCUN element du jeu : ils n'existent pas encore.

#include <cmath>
#include <cstdio>
#include <limits>
#include <string>
#include <vector>

#include "core/engine.hpp"
#include "core/error.hpp"
#include "core/log.hpp"
#include "core/version.hpp"

using namespace moonlight::core;

namespace {

int g_checks = 0;
int g_failures = 0;

#define CHECK(condition)                                                          \
    do {                                                                          \
        ++g_checks;                                                               \
        if (!(condition)) {                                                       \
            ++g_failures;                                                         \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        }                                                                         \
    } while (false)

struct CapturedLog {
    LogLevel level;
    std::string category;
    std::string message;
};

std::vector<CapturedLog> g_logs;

void startCapturingLogs() {
    g_logs.clear();
    Logger::instance().setSink([](const LogRecord& record) {
        g_logs.push_back({record.level, std::string(record.category), std::string(record.message)});
    });
}

bool capturedLogContains(LogLevel level, const std::string& text) {
    for (const CapturedLog& entry : g_logs) {
        if (entry.level == level && entry.message.find(text) != std::string::npos) {
            return true;
        }
    }
    return false;
}

bool nearlyEqual(double a, double b) { return std::fabs(a - b) < 1e-9; }

void testNormalLifecycle() {
    Engine engine;
    CHECK(engine.state() == Engine::State::Created);

    Status status = engine.initialize();
    CHECK(status.isOk());
    CHECK(engine.state() == Engine::State::Initialized);
    CHECK(capturedLogContains(LogLevel::Info, "initialized"));

    for (int i = 0; i < 3; ++i) {
        status = engine.update(0.016);
        CHECK(status.isOk());
    }
    CHECK(engine.frameCount() == 3);
    CHECK(nearlyEqual(engine.elapsedSeconds(), 0.048));

    engine.shutdown();
    CHECK(engine.state() == Engine::State::ShutDown);
    CHECK(capturedLogContains(LogLevel::Info, "Shutting down after 3 frame(s)"));
}

void testInvalidStateTransitions() {
    Engine engine;

    Status status = engine.update(0.016);  // avant initialize()
    CHECK(!status.isOk());
    CHECK(status.code() == ErrorCode::InvalidState);
    CHECK(engine.frameCount() == 0);

    engine.shutdown();  // sans effet : jamais initialise
    CHECK(engine.state() == Engine::State::Created);

    CHECK(engine.initialize().isOk());
    status = engine.initialize();  // double initialisation
    CHECK(status.code() == ErrorCode::InvalidState);
    CHECK(engine.state() == Engine::State::Initialized);

    engine.shutdown();
    engine.shutdown();  // idempotent
    CHECK(engine.state() == Engine::State::ShutDown);

    status = engine.update(0.016);  // apres shutdown()
    CHECK(status.code() == ErrorCode::InvalidState);
}

void testInvalidDeltaAndClamp() {
    EngineConfig config;
    config.maxFrameDeltaSeconds = 0.1;
    Engine engine(config);
    CHECK(engine.initialize().isOk());

    CHECK(engine.update(-1.0).code() == ErrorCode::InvalidArgument);
    CHECK(engine.update(std::numeric_limits<double>::quiet_NaN()).code() == ErrorCode::InvalidArgument);
    CHECK(engine.update(std::numeric_limits<double>::infinity()).code() == ErrorCode::InvalidArgument);
    CHECK(engine.frameCount() == 0);  // les appels invalides ne comptent pas

    CHECK(engine.update(10.0).isOk());  // ramene a 0.1 s
    CHECK(engine.frameCount() == 1);
    CHECK(nearlyEqual(engine.elapsedSeconds(), 0.1));

    engine.shutdown();
}

void testInvalidConfig() {
    EngineConfig config;
    config.maxFrameDeltaSeconds = 0.0;
    Engine engine(config);

    Status status = engine.initialize();
    CHECK(status.code() == ErrorCode::InvalidArgument);
    CHECK(engine.state() == Engine::State::Created);
}

void testReinitialization() {
    Engine engine;
    CHECK(engine.initialize().isOk());
    CHECK(engine.update(0.01).isOk());
    engine.shutdown();

    CHECK(engine.initialize().isOk());  // reinitialisation permise
    CHECK(engine.frameCount() == 0);    // les compteurs repartent de zero
    CHECK(nearlyEqual(engine.elapsedSeconds(), 0.0));
    engine.shutdown();
}

void testDestructorShutsDownAndWarns() {
    g_logs.clear();
    {
        Engine engine;
        CHECK(engine.initialize().isOk());
    }  // destructeur sans shutdown()
    CHECK(capturedLogContains(LogLevel::Warning, "without shutdown()"));
    CHECK(capturedLogContains(LogLevel::Info, "Shutting down"));
}

void testLogging() {
    Logger& logger = Logger::instance();
    g_logs.clear();

    logger.setMinLevel(LogLevel::Info);
    MOONLIGHT_LOG_DEBUG("test", "hidden message");
    MOONLIGHT_LOG_INFO("test", "value=%d text=%s", 42, "ok");
    CHECK(!capturedLogContains(LogLevel::Debug, "hidden"));
    CHECK(capturedLogContains(LogLevel::Info, "value=42 text=ok"));

    logger.setMinLevel(LogLevel::Debug);
    MOONLIGHT_LOG_DEBUG("test", "now visible");
    CHECK(capturedLogContains(LogLevel::Debug, "now visible"));
    logger.setMinLevel(LogLevel::Info);

    // Message plus long que le tampon de pile (1024 octets) : il doit rester complet.
    const std::string longText(3000, 'x');
    MOONLIGHT_LOG_INFO("test", "%s", longText.c_str());
    bool foundLong = false;
    for (const CapturedLog& entry : g_logs) {
        foundLong = foundLong || entry.message == longText;
    }
    CHECK(foundLong);
}

}  // namespace

int main() {
    std::printf("MOONLIGHT %s - smoke test\n", MOONLIGHT_VERSION_STRING);

    Logger::instance().setMinLevel(LogLevel::Info);
    startCapturingLogs();

    testNormalLifecycle();
    testInvalidStateTransitions();
    testInvalidDeltaAndClamp();
    testInvalidConfig();
    testReinitialization();
    testDestructorShutsDownAndWarns();
    testLogging();

    Logger::instance().resetSink();

    // Demonstration visible du sink par defaut (stderr), hors verifications.
    MOONLIGHT_LOG_INFO("smoke", "Default stderr sink is working");

    std::printf("%d check(s), %d failure(s)\n", g_checks, g_failures);
    if (g_failures == 0) {
        std::printf("RESULT: PASS\n");
        return 0;
    }
    std::printf("RESULT: FAIL\n");
    return 1;
}
