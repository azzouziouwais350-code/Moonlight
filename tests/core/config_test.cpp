// Tests de la configuration : valeurs par defaut, analyse, erreurs, validation,
// serialisation, application au Logger, chargement/sauvegarde via IFileSystem.

#include <cmath>
#include <cstdio>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>

#include "check.hpp"
#include "core/config.hpp"
#include "core/engine.hpp"
#include "log_capture.hpp"
#include "memory_filesystem.hpp"
#include "platform/config_io.hpp"
#include "platform/host_filesystem.hpp"
#include "temp_dir.hpp"

using namespace moonlight::core;
using namespace moonlight::platform;
using moonlight::testing::LogCapture;
using moonlight::testing::MemoryFileSystem;
using moonlight::testing::TempDir;

namespace {

bool contains(const std::string& text, const std::string& part) {
    return text.find(part) != std::string::npos;
}

/// Verifie qu'un texte est rejete avec ParseError et que le message contient `expected`.
void expectParseError(const std::string& text, const std::string& expected) {
    const Result<Config> result = parseConfig(text);
    CHECK_CODE(result, ErrorCode::ParseError);
    if (result.status().code() == ErrorCode::ParseError) {
        const bool found = contains(result.status().message(), expected);
        if (!found) {
            std::fprintf(stderr, "  message was: %s\n  expected to contain: %s\n",
                         result.status().message().c_str(), expected.c_str());
        }
        CHECK(found);
    }
}

// ---------------------------------------------------------------------------
// Valeurs par defaut
// ---------------------------------------------------------------------------
void testDefaults() {
    const Config config;
    CHECK(config.log.minLevel == LogLevel::Info);
    CHECK(config.log.format == LogFormat::Text);
    CHECK(config.log.filePath.empty());
    CHECK(config.paths.gameDataDir.empty());  // aucune donnee de jeu par defaut
    CHECK(config.paths.modsDir == "mods");
    CHECK(config.paths.savesDir == "saves");
    CHECK(std::fabs(config.engine.maxFrameDeltaSeconds - 0.25) < 1e-12);
    CHECK_OK(validateConfig(config));

    // Le moteur recoit les memes valeurs par defaut que EngineConfig.
    const EngineConfig engineConfig = makeEngineConfig(config);
    CHECK(std::fabs(engineConfig.maxFrameDeltaSeconds - EngineConfig{}.maxFrameDeltaSeconds) < 1e-12);

    Config changed;
    changed.engine.maxFrameDeltaSeconds = 0.5;
    CHECK(std::fabs(makeEngineConfig(changed).maxFrameDeltaSeconds - 0.5) < 1e-12);
}

// ---------------------------------------------------------------------------
// Analyse : cas valides
// ---------------------------------------------------------------------------
void testParseEmptyGivesDefaults() {
    const Result<Config> empty = parseConfig("");
    CHECK_OK(empty);
    CHECK(empty.isOk() && empty.value() == Config{});

    const Result<Config> onlyComments = parseConfig("# rien\n; ici\n\n   \n");
    CHECK_OK(onlyComments);
    CHECK(onlyComments.isOk() && onlyComments.value() == Config{});
}

void testParseFullFile() {
    const Result<Config> result = parseConfig(
        "# configuration\n"
        "[log]\n"
        "level = debug\n"
        "format = json\n"
        "file = logs/moonlight.jsonl\n"
        "\n"
        "[paths]\n"
        "game_data_dir = /home/user/my-game-data\n"
        "mods_dir = my mods\n"
        "saves_dir = saves2\n"
        "[engine]\n"
        "max_frame_delta_seconds = 0.1\n");
    CHECK_OK(result);
    if (!result.isOk()) {
        return;
    }
    const Config& c = result.value();
    CHECK(c.log.minLevel == LogLevel::Debug);
    CHECK(c.log.format == LogFormat::Json);
    CHECK(c.log.filePath == "logs/moonlight.jsonl");
    CHECK(c.paths.gameDataDir == "/home/user/my-game-data");
    CHECK(c.paths.modsDir == "my mods");  // espaces internes conserves
    CHECK(c.paths.savesDir == "saves2");
    CHECK(std::fabs(c.engine.maxFrameDeltaSeconds - 0.1) < 1e-12);
}

void testParsePartialKeepsDefaults() {
    const Result<Config> result = parseConfig("[engine]\nmax_frame_delta_seconds = 1\n");
    CHECK_OK(result);
    if (result.isOk()) {
        Config expected;
        expected.engine.maxFrameDeltaSeconds = 1.0;
        CHECK(result.value() == expected);
    }
}

void testParseToleratesFormattingVariants() {
    // CRLF, BOM UTF-8, casse, espaces autour de '=', commentaires ';'.
    const std::string text =
        "\xEF\xBB\xBF; fichier ecrit sous Windows\r\n"
        "[ LOG ]\r\n"
        "  LeVeL   =    ERROR   \r\n"
        "Format=Json\r\n"
        "[Engine]\r\n"
        "MAX_FRAME_DELTA_SECONDS=2.5\r\n";
    const Result<Config> result = parseConfig(text);
    CHECK_OK(result);
    if (result.isOk()) {
        CHECK(result.value().log.minLevel == LogLevel::Error);
        CHECK(result.value().log.format == LogFormat::Json);
        CHECK(std::fabs(result.value().engine.maxFrameDeltaSeconds - 2.5) < 1e-12);
    }
    // Derniere ligne sans retour a la ligne final.
    CHECK_OK(parseConfig("[log]\nlevel = info"));
}

void testParseQuotedValues() {
    const Result<Config> result = parseConfig(
        "[paths]\n"
        "mods_dir = \"  spaced  \"\n"
        "saves_dir = \"say \\\"hi\\\" \\\\ ok\"\n"
        "game_data_dir = \"\"\n");
    CHECK_OK(result);
    if (result.isOk()) {
        CHECK(result.value().paths.modsDir == "  spaced  ");
        CHECK(result.value().paths.savesDir == "say \"hi\" \\ ok");
        CHECK(result.value().paths.gameDataDir.empty());
    }
}

// ---------------------------------------------------------------------------
// Analyse : erreurs (jamais de configuration partielle)
// ---------------------------------------------------------------------------
void testParseSyntaxErrors() {
    expectParseError("[log\nlevel = info\n", "line 1");                 // ']' manquant
    expectParseError("[]\n", "invalid section name");
    expectParseError("[a[b]\n", "invalid section name");
    expectParseError("[log] # note\n", "must end with ']'");            // pas de commentaire en fin de ligne
    expectParseError("level = info\n", "before any [section]");
    expectParseError("[log]\njust a line\n", "expected 'key = value'");
    expectParseError("[log]\n = value\n", "missing key");
    expectParseError(std::string("[log]\nlevel = in") + '\0' + "fo\n", "NUL");
}

void testParseUnknownAndDuplicates() {
    expectParseError("[nothing]\n", "unknown section [nothing]");
    expectParseError("[log]\nlvl = info\n", "unknown key 'lvl' in section [log]");
    expectParseError("[log]\nlevel = info\nlevel = debug\n", "duplicate key 'level'");
    expectParseError("[log]\nlevel = info\n[log]\n", "duplicate section [log]");
    // La casse ne contourne pas la detection des doublons.
    expectParseError("[log]\nlevel = info\nLEVEL = debug\n", "duplicate key");
}

void testParseValueErrors() {
    expectParseError("[log]\nlevel = verbose\n", "log.level must be one of");
    expectParseError("[log]\nlevel = warn\n", "log.level must be one of");
    expectParseError("[log]\nformat = xml\n", "log.format must be one of");
    expectParseError("[engine]\nmax_frame_delta_seconds = fast\n", "must be a number");
    expectParseError("[engine]\nmax_frame_delta_seconds = 0.5x\n", "must be a number");
    expectParseError("[engine]\nmax_frame_delta_seconds =\n", "must be a number");
    expectParseError("[engine]\nmax_frame_delta_seconds = 0\n", "must be a number in (0, 10]");
    expectParseError("[engine]\nmax_frame_delta_seconds = -1\n", "must be a number in (0, 10]");
    expectParseError("[engine]\nmax_frame_delta_seconds = 10.5\n", "must be a number in (0, 10]");
    expectParseError("[engine]\nmax_frame_delta_seconds = nan\n", "must be a number in (0, 10]");
    expectParseError("[engine]\nmax_frame_delta_seconds = inf\n", "must be a number in (0, 10]");
    expectParseError("[paths]\nmods_dir =\n", "paths.mods_dir must not be empty");
    expectParseError("[paths]\nsaves_dir = \"\"\n", "paths.saves_dir must not be empty");
    expectParseError("[paths]\nmods_dir = a\tb\n", "control character");
    expectParseError("[paths]\nmods_dir = " + std::string(kMaxConfigPathLength + 1, 'x') + "\n",
                     "longer than");
}

void testParseQuotingErrors() {
    expectParseError("[paths]\nmods_dir = \"abc\n", "unterminated quoted value");
    expectParseError("[paths]\nmods_dir = \"abc\\\"\n", "unterminated quoted value");
    expectParseError("[paths]\nmods_dir = \"a\\nb\"\n", "unsupported escape");
    expectParseError("[paths]\nmods_dir = \"a\"b\"\n", "unescaped");
}

void testParseReportsTheRightLine() {
    expectParseError("# 1\n# 2\n[log]\n\nlevel = nope\n", "line 5:");
    expectParseError("[log]\r\nlevel = info\r\nbogus\r\n", "line 3:");
}

void testParseTooLarge() {
    const std::string huge(kMaxConfigBytes + 1, '#');
    CHECK_CODE(parseConfig(huge), ErrorCode::TooLarge);
    // A la limite exacte : accepte (que des commentaires).
    CHECK_OK(parseConfig(std::string(kMaxConfigBytes, '#')));
}

void testParseFailureReturnsNoPartialConfig() {
    // La premiere ligne est valide, la seconde non : aucun Config n'est produit.
    const Result<Config> result = parseConfig("[engine]\nmax_frame_delta_seconds = 1\n[log]\nlevel = bad\n");
    CHECK(!result.isOk());
    CHECK(result.valueOr(Config{}) == Config{});
}

// ---------------------------------------------------------------------------
// Validation directe
// ---------------------------------------------------------------------------
void testValidateConfig() {
    Config c;
    CHECK_OK(validateConfig(c));

    c.engine.maxFrameDeltaSeconds = std::numeric_limits<double>::quiet_NaN();
    CHECK_CODE(validateConfig(c), ErrorCode::InvalidArgument);
    c.engine.maxFrameDeltaSeconds = std::numeric_limits<double>::infinity();
    CHECK_CODE(validateConfig(c), ErrorCode::InvalidArgument);
    c.engine.maxFrameDeltaSeconds = 0.0;
    CHECK_CODE(validateConfig(c), ErrorCode::InvalidArgument);
    c.engine.maxFrameDeltaSeconds = kMaxFrameDeltaUpperBound;  // borne incluse
    CHECK_OK(validateConfig(c));
    c.engine.maxFrameDeltaSeconds = 0.25;

    c.paths.modsDir.clear();
    CHECK_CODE(validateConfig(c), ErrorCode::InvalidArgument);
    c.paths.modsDir = "mods";
    c.paths.savesDir = "bad\nname";
    CHECK_CODE(validateConfig(c), ErrorCode::InvalidArgument);
    c.paths.savesDir = "saves";
    c.log.filePath = std::string("a\0b", 3);
    CHECK_CODE(validateConfig(c), ErrorCode::InvalidArgument);
    c.log.filePath.clear();

    // Enumerations hors plage (valeur forgee par un appelant).
    c.log.minLevel = static_cast<LogLevel>(99);
    CHECK_CODE(validateConfig(c), ErrorCode::InvalidArgument);
    c.log.minLevel = LogLevel::Info;
    c.log.format = static_cast<LogFormat>(99);
    CHECK_CODE(validateConfig(c), ErrorCode::InvalidArgument);
}

// ---------------------------------------------------------------------------
// Serialisation
// ---------------------------------------------------------------------------
void testSerializeRoundTrip() {
    Config original;
    original.log.minLevel = LogLevel::Warning;
    original.log.format = LogFormat::Json;
    original.log.filePath = "logs/out.jsonl";
    original.paths.gameDataDir = "/data/mine";
    original.paths.modsDir = "my mods";
    original.paths.savesDir = "saves";
    original.engine.maxFrameDeltaSeconds = 0.0333333333333333;

    const Result<Config> back = parseConfig(serializeConfig(original));
    CHECK_OK(back);
    CHECK(back.isOk() && back.value() == original);  // egalite exacte, y compris le double

    // Les valeurs par defaut aussi.
    const Result<Config> defaults = parseConfig(serializeConfig(Config{}));
    CHECK(defaults.isOk() && defaults.value() == Config{});
}

void testSerializeQuotesWhenNeeded() {
    Config original;
    original.paths.modsDir = "  leading and trailing  ";
    original.paths.savesDir = "\"starts with a quote\" and a \\ backslash";
    original.paths.gameDataDir = "path # not a comment ; really";
    original.log.filePath = "tab-free value";

    const std::string text = serializeConfig(original);
    CHECK(contains(text, "mods_dir = \"  leading and trailing  \""));

    const Result<Config> back = parseConfig(text);
    CHECK_OK(back);
    CHECK(back.isOk() && back.value() == original);
}

void testSerializeIsStableAndDocumented() {
    const std::string first = serializeConfig(Config{});
    CHECK(first == serializeConfig(Config{}));
    CHECK(contains(first, "[log]"));
    CHECK(contains(first, "[paths]"));
    CHECK(contains(first, "[engine]"));
    CHECK(contains(first, "level = info"));
    CHECK(contains(first, "max_frame_delta_seconds = 0.25"));
    CHECK(contains(first, "YOUR OWN game data"));
}

// ---------------------------------------------------------------------------
// Application au Logger
// ---------------------------------------------------------------------------
void testApplyLogConfigToFile() {
    TempDir dir;
    CHECK(dir.isValid());
    LogConfig config;
    config.minLevel = LogLevel::Debug;
    config.format = LogFormat::Json;
    config.filePath = dir.file("applied.jsonl");

    Logger& logger = Logger::instance();
    const LogLevel previous = logger.minLevel();
    CHECK_OK(applyLogConfig(config, logger));
    CHECK(logger.minLevel() == LogLevel::Debug);
    MOONLIGHT_LOG_DEBUG("cfg", "debug line");
    logger.resetSink();
    logger.setMinLevel(previous);

    std::ifstream stream(config.filePath, std::ios::binary);
    std::stringstream content;
    content << stream.rdbuf();
    CHECK(contains(content.str(), "\"message\":\"debug line\""));
    CHECK(contains(content.str(), "\"level\":\"debug\""));
}

void testApplyLogConfigIsAllOrNothing() {
    LogCapture capture(LogLevel::Info);
    Logger& logger = Logger::instance();

    LogConfig config;
    config.minLevel = LogLevel::Error;
    config.filePath = "/this-directory-does-not-exist-moonlight/log.txt";
    CHECK_CODE(applyLogConfig(config, logger), ErrorCode::NotFound);

    // Le Logger est inchange : meme niveau, et le sink de capture est toujours actif.
    CHECK(logger.minLevel() == LogLevel::Info);
    MOONLIGHT_LOG_INFO("cfg", "still captured");
    CHECK(capture.contains(LogLevel::Info, "still captured"));

    LogConfig invalid;
    invalid.filePath = "bad\npath";
    CHECK_CODE(applyLogConfig(invalid, logger), ErrorCode::InvalidArgument);
    CHECK(logger.minLevel() == LogLevel::Info);
}

// ---------------------------------------------------------------------------
// Chargement / sauvegarde via IFileSystem
// ---------------------------------------------------------------------------
void runConfigIoRoundTrip(IFileSystem& fs, const std::string& base) {
    const std::string path = base + "/settings/moonlight.ini";  // "settings" n'existe pas encore

    Config config;
    config.log.minLevel = LogLevel::Debug;
    config.paths.gameDataDir = "/some/user/folder";
    config.engine.maxFrameDeltaSeconds = 0.5;

    CHECK_OK(saveConfig(fs, path, config));  // cree le dossier parent
    const Result<Config> loaded = loadConfig(fs, path);
    CHECK_OK(loaded);
    CHECK(loaded.isOk() && loaded.value() == config);

    // Pas de fichier temporaire residuel.
    const Result<FileType> leftover = fs.fileType(path + ".tmp");
    CHECK(leftover.isOk() && leftover.value() == FileType::NotFound);

    // Ecraser : le nouveau contenu remplace l'ancien.
    config.log.minLevel = LogLevel::Error;
    CHECK_OK(saveConfig(fs, path, config));
    const Result<Config> reloaded = loadConfig(fs, path);
    CHECK(reloaded.isOk() && reloaded.value().log.minLevel == LogLevel::Error);
}

void testConfigIoOnMemoryFileSystem() {
    MemoryFileSystem fs;
    runConfigIoRoundTrip(fs, "/home/user");
}

void testConfigIoOnHostFileSystem() {
    TempDir dir;
    CHECK(dir.isValid());
    HostFileSystem fs;
    runConfigIoRoundTrip(fs, dir.file("io"));
}

void testLoadMissingAndDefaults() {
    MemoryFileSystem fs;
    fs.addDirectory("/cfg");

    const Result<Config> missing = loadConfig(fs, "/cfg/none.ini");
    CHECK_CODE(missing, ErrorCode::NotFound);
    CHECK(contains(missing.status().message(), "/cfg/none.ini"));  // le chemin est dans l'erreur

    LogCapture capture(LogLevel::Info);
    const Result<Config> fallback = loadConfigOrDefault(fs, "/cfg/none.ini");
    CHECK_OK(fallback);
    CHECK(fallback.isOk() && fallback.value() == Config{});
    CHECK(capture.contains(LogLevel::Info, "using defaults"));
}

void testLoadInvalidIsNeverReplacedByDefaults() {
    MemoryFileSystem fs;
    fs.addFile("/cfg/broken.ini", "[log]\nlevel = nonsense\n");

    const Result<Config> strict = loadConfig(fs, "/cfg/broken.ini");
    CHECK_CODE(strict, ErrorCode::ParseError);
    CHECK(contains(strict.status().message(), "/cfg/broken.ini"));
    CHECK(contains(strict.status().message(), "line 2"));

    // loadConfigOrDefault ne masque PAS une configuration cassee.
    CHECK_CODE(loadConfigOrDefault(fs, "/cfg/broken.ini"), ErrorCode::ParseError);

    // Ni un fichier illisible, ni un dossier a la place du fichier.
    fs.addFile("/cfg/locked.ini", "[log]\nlevel = info\n");
    fs.setUnreadable("/cfg/locked.ini");
    CHECK_CODE(loadConfigOrDefault(fs, "/cfg/locked.ini"), ErrorCode::AccessDenied);
    fs.addDirectory("/cfg/isdir.ini");
    CHECK_CODE(loadConfigOrDefault(fs, "/cfg/isdir.ini"), ErrorCode::InvalidArgument);

    // Fichier trop gros.
    fs.addFile("/cfg/huge.ini", std::string(kMaxConfigBytes + 1, '#'));
    CHECK_CODE(loadConfig(fs, "/cfg/huge.ini"), ErrorCode::TooLarge);
}

void testSaveRejectsInvalidConfigWithoutTouchingDisk() {
    MemoryFileSystem fs;
    Config bad;
    bad.engine.maxFrameDeltaSeconds = -3.0;
    CHECK_CODE(saveConfig(fs, "/cfg/new.ini", bad), ErrorCode::InvalidArgument);
    CHECK(!fs.hasFile("/cfg/new.ini"));
    CHECK(!fs.hasFile("/cfg/new.ini.tmp"));

    CHECK_CODE(saveConfig(fs, "", Config{}), ErrorCode::InvalidArgument);
}

void testSaveFailureKeepsPreviousFile() {
    MemoryFileSystem fs;
    Config original;
    original.log.minLevel = LogLevel::Warning;
    CHECK_OK(saveConfig(fs, "/cfg/keep.ini", original));
    const std::string before = fs.contentOf("/cfg/keep.ini").value_or("");
    CHECK(!before.empty());

    Config changed;
    changed.log.minLevel = LogLevel::Error;
    fs.setFailWrites(true);
    CHECK_CODE(saveConfig(fs, "/cfg/keep.ini", changed), ErrorCode::IoError);
    fs.setFailWrites(false);

    CHECK(fs.contentOf("/cfg/keep.ini").value_or("") == before);  // ancien fichier intact
    CHECK(!fs.hasFile("/cfg/keep.ini.tmp"));                      // pas de temporaire residuel
    const Result<Config> still = loadConfig(fs, "/cfg/keep.ini");
    CHECK(still.isOk() && still.value().log.minLevel == LogLevel::Warning);
}

}  // namespace

int main() {
    std::printf("moonlight_config_test\n");
    RUN_TEST(testDefaults);
    RUN_TEST(testParseEmptyGivesDefaults);
    RUN_TEST(testParseFullFile);
    RUN_TEST(testParsePartialKeepsDefaults);
    RUN_TEST(testParseToleratesFormattingVariants);
    RUN_TEST(testParseQuotedValues);
    RUN_TEST(testParseSyntaxErrors);
    RUN_TEST(testParseUnknownAndDuplicates);
    RUN_TEST(testParseValueErrors);
    RUN_TEST(testParseQuotingErrors);
    RUN_TEST(testParseReportsTheRightLine);
    RUN_TEST(testParseTooLarge);
    RUN_TEST(testParseFailureReturnsNoPartialConfig);
    RUN_TEST(testValidateConfig);
    RUN_TEST(testSerializeRoundTrip);
    RUN_TEST(testSerializeQuotesWhenNeeded);
    RUN_TEST(testSerializeIsStableAndDocumented);
    RUN_TEST(testApplyLogConfigToFile);
    RUN_TEST(testApplyLogConfigIsAllOrNothing);
    RUN_TEST(testConfigIoOnMemoryFileSystem);
    RUN_TEST(testConfigIoOnHostFileSystem);
    RUN_TEST(testLoadMissingAndDefaults);
    RUN_TEST(testLoadInvalidIsNeverReplacedByDefaults);
    RUN_TEST(testSaveRejectsInvalidConfigWithoutTouchingDisk);
    RUN_TEST(testSaveFailureKeepsPreviousFile);
    return moonlight::testing::finish("moonlight_config_test");
}
