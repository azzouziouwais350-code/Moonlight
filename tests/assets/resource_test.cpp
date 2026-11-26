// Tests du ResourceManager : chemins virtuels, montages, priorites, erreurs,
// et securite (traversee de chemins, liens symboliques).
//
// La logique est testee sur MemoryFileSystem (ou l'on peut forger des liens
// symboliques et des fichiers illisibles meme en root) ET sur le vrai disque
// pour les cas de securite.

#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "assets/resource_manager.hpp"
#include "check.hpp"
#include "log_capture.hpp"
#include "memory_filesystem.hpp"
#include "platform/host_filesystem.hpp"
#include "temp_dir.hpp"

using namespace moonlight::core;
using namespace moonlight::platform;
using namespace moonlight::assets;
using moonlight::testing::LogCapture;
using moonlight::testing::MemoryFileSystem;
using moonlight::testing::TempDir;

namespace {

namespace stdfs = std::filesystem;

std::string asText(const Bytes& bytes) {
    std::string text;
    for (const std::byte b : bytes) {
        text.push_back(static_cast<char>(std::to_integer<unsigned char>(b)));
    }
    return text;
}

bool contains(const std::string& text, const std::string& part) {
    return text.find(part) != std::string::npos;
}

/// Contenu type : un jeu de base et un dossier de mods qui masque un fichier.
void populate(MemoryFileSystem& fs) {
    fs.addFile("/game/textures/a.png", "BASE-A");
    fs.addFile("/game/only-base.txt", "BASE");
    fs.addDirectory("/game/empty-dir");
    fs.addFile("/mods/textures/a.png", "MOD-A");
    fs.addFile("/outside/secret.txt", "SECRET");
}

// ---------------------------------------------------------------------------
// Syntaxe des chemins virtuels
// ---------------------------------------------------------------------------
void testVirtualPathValidation() {
    const std::vector<std::string> valid = {
        "a", "file.txt", "textures/hd/menu.png", "dir/file.name.ext", "a/b/c/d/e",
        "name with spaces.txt", "caf\xC3\xA9/menu.png", ".hidden", "a/.hidden", "..dots", "a..b",
        "-dash_underscore"};
    for (const std::string& path : valid) {
        const Status status = ResourceManager::validateVirtualPath(path);
        if (!status.isOk()) {
            std::fprintf(stderr, "  rejected unexpectedly: '%s'\n", path.c_str());
        }
        CHECK_OK(status);
    }

    const std::vector<std::string> invalid = {
        "",              // vide
        "/etc/passwd",   // absolu
        "/",
        "a/",            // '/' final
        "a//b",          // composante vide
        "//a",
        ".",             // composantes speciales
        "..",
        "./a",
        "a/./b",
        "../a",
        "a/..",
        "a/../b",
        "a/b/../../..",
        "a\\b",          // antislash
        "..\\a",
        "C:/windows",    // lettre de lecteur / ':'
        "a:b",
        "tab\there",     // controles
        "line\nbreak",
        "bell\x07",
        std::string("nul\0byte", 8),
        "del\x7F",
        std::string(1025, 'x'),  // trop long
    };
    for (const std::string& path : invalid) {
        const Status status = ResourceManager::validateVirtualPath(path);
        if (status.code() != ErrorCode::InvalidArgument) {
            std::fprintf(stderr, "  accepted unexpectedly: '%s'\n", path.substr(0, 40).c_str());
        }
        CHECK_CODE(status, ErrorCode::InvalidArgument);
    }
    CHECK_OK(ResourceManager::validateVirtualPath(std::string(1024, 'x')));  // limite incluse
}

// ---------------------------------------------------------------------------
// Montages
// ---------------------------------------------------------------------------
void testMountErrors() {
    MemoryFileSystem fs;
    populate(fs);
    ResourceManager manager(fs);

    CHECK_CODE(manager.mountDirectory("", "/game"), ErrorCode::InvalidArgument);
    CHECK_CODE(manager.mountDirectory("bad name", "/game"), ErrorCode::InvalidArgument);
    CHECK_CODE(manager.mountDirectory("bad/name", "/game"), ErrorCode::InvalidArgument);
    CHECK_CODE(manager.mountDirectory(std::string(65, 'a'), "/game"), ErrorCode::InvalidArgument);
    CHECK_CODE(manager.mountDirectory("missing", "/no-such-dir"), ErrorCode::NotFound);
    CHECK_CODE(manager.mountDirectory("file", "/game/only-base.txt"), ErrorCode::InvalidArgument);
    CHECK_CODE(manager.mountDirectory("empty", ""), ErrorCode::InvalidArgument);
    CHECK(manager.mounts().empty());  // aucun echec ne laisse de montage

    CHECK_OK(manager.mountDirectory("game", "/game"));
    CHECK_CODE(manager.mountDirectory("game", "/mods"), ErrorCode::InvalidArgument);  // nom deja pris
    CHECK(manager.mounts().size() == 1);

    CHECK_CODE(manager.unmount("nope"), ErrorCode::NotFound);
    CHECK_OK(manager.unmount("game"));
    CHECK(manager.mounts().empty());
    CHECK_OK(manager.mountDirectory("game", "/game"));  // le nom est reutilisable
}

void testMountOrderFollowsPriority() {
    MemoryFileSystem fs;
    populate(fs);
    fs.addDirectory("/extra1");
    fs.addDirectory("/extra2");
    ResourceManager manager(fs);

    CHECK_OK(manager.mountDirectory("base", "/game", 0));
    CHECK_OK(manager.mountDirectory("mods", "/mods", 10));
    CHECK_OK(manager.mountDirectory("low", "/extra1", -5));
    CHECK_OK(manager.mountDirectory("base2", "/extra2", 0));  // meme priorite : apres "base"

    const std::vector<MountInfo>& mounts = manager.mounts();
    CHECK(mounts.size() == 4);
    if (mounts.size() == 4) {
        CHECK(mounts[0].name == "mods");
        CHECK(mounts[1].name == "base");
        CHECK(mounts[2].name == "base2");
        CHECK(mounts[3].name == "low");
        CHECK(mounts[0].priority == 10);
        CHECK(mounts[0].rootPath == "/mods");
    }
}

void testMountStoresCanonicalRoot() {
    MemoryFileSystem fs;
    populate(fs);
    fs.addSymlink("/linked-root", "/game");
    ResourceManager manager(fs);
    CHECK_OK(manager.mountDirectory("lr", "/linked-root/../game/./"));
    CHECK(manager.mounts().size() == 1 && manager.mounts()[0].rootPath == "/game");
    const Result<std::string> content = [&]() -> Result<std::string> {
        const Result<Bytes> bytes = manager.readAll("only-base.txt");
        if (!bytes.isOk()) {
            return bytes.status();
        }
        return asText(bytes.value());
    }();
    CHECK(content.isOk() && content.value() == "BASE");
}

// ---------------------------------------------------------------------------
// Resolution et lecture
// ---------------------------------------------------------------------------
void testResolveAndReadWithPriorities() {
    MemoryFileSystem fs;
    populate(fs);
    ResourceManager manager(fs);
    CHECK_OK(manager.mountDirectory("game", "/game", 0));
    CHECK_OK(manager.mountDirectory("mods", "/mods", 10));

    // Le mod (priorite plus haute) masque le fichier de base.
    const Result<ResolvedResource> overridden = manager.resolve("textures/a.png");
    CHECK_OK(overridden);
    if (overridden.isOk()) {
        CHECK(overridden.value().mountName == "mods");
        CHECK(overridden.value().hostPath == "/mods/textures/a.png");
        CHECK(overridden.value().virtualPath == "textures/a.png");
    }
    const Result<Bytes> overriddenBytes = manager.readAll("textures/a.png");
    CHECK_OK(overriddenBytes);
    CHECK(overriddenBytes.isOk() && asText(overriddenBytes.value()) == "MOD-A");

    // Absent du mod : repli normal sur le montage suivant.
    const Result<ResolvedResource> fromBase = manager.resolve("only-base.txt");
    CHECK(fromBase.isOk() && fromBase.value().mountName == "game");
    const Result<Bytes> baseBytes = manager.readAll("only-base.txt");
    CHECK(baseBytes.isOk() && asText(baseBytes.value()) == "BASE");

    // Demonter le mod fait reapparaitre le fichier de base.
    CHECK_OK(manager.unmount("mods"));
    const Result<Bytes> afterUnmount = manager.readAll("textures/a.png");
    CHECK(afterUnmount.isOk() && asText(afterUnmount.value()) == "BASE-A");
}

void testNotFoundAndExists() {
    MemoryFileSystem fs;
    populate(fs);
    ResourceManager manager(fs);

    // Aucun montage.
    CHECK_CODE(manager.resolve("a.txt"), ErrorCode::NotFound);
    CHECK_CODE(manager.readAll("a.txt"), ErrorCode::NotFound);

    CHECK_OK(manager.mountDirectory("game", "/game"));
    CHECK_OK(manager.mountDirectory("mods", "/mods", 5));

    const Result<ResolvedResource> missing = manager.resolve("nothing/here.bin");
    CHECK_CODE(missing, ErrorCode::NotFound);
    CHECK(contains(missing.status().message(), "nothing/here.bin"));
    CHECK(contains(missing.status().message(), "2 mount(s)"));

    const Result<bool> present = manager.exists("only-base.txt");
    CHECK_OK(present);
    CHECK(present.isOk() && present.value());
    const Result<bool> absent = manager.exists("nothing.bin");
    CHECK_OK(absent);  // absent n'est pas une erreur
    CHECK(absent.isOk() && !absent.value());

    // Un chemin invalide est une erreur, pas "faux".
    CHECK_CODE(manager.exists("../outside/secret.txt"), ErrorCode::InvalidArgument);

    // Un dossier n'est pas une ressource.
    CHECK_CODE(manager.resolve("textures"), ErrorCode::InvalidArgument);
    CHECK_CODE(manager.readAll("empty-dir"), ErrorCode::InvalidArgument);
}

void testReadSizeLimit() {
    MemoryFileSystem fs;
    fs.addFile("/game/four.bin", "1234");
    fs.addFile("/game/five.bin", "12345");
    ResourceManagerOptions options;
    options.maxResourceBytes = 4;
    ResourceManager manager(fs, options);
    CHECK_OK(manager.mountDirectory("game", "/game"));

    CHECK_OK(manager.readAll("four.bin"));  // exactement la limite
    const Result<Bytes> tooBig = manager.readAll("five.bin");
    CHECK_CODE(tooBig, ErrorCode::TooLarge);
    CHECK(contains(tooBig.status().message(), "five.bin"));
}

void testFailsClosedOnErrors() {
    MemoryFileSystem fs;
    populate(fs);
    ResourceManager manager(fs);
    CHECK_OK(manager.mountDirectory("mods", "/mods", 10));
    CHECK_OK(manager.mountDirectory("game", "/game", 0));

    // Fichier illisible dans le mod : on NE retombe PAS sur la version de base.
    fs.setUnreadable("/mods/textures/a.png");
    const Result<Bytes> denied = manager.readAll("textures/a.png");
    CHECK_CODE(denied, ErrorCode::AccessDenied);
    CHECK(contains(denied.status().message(), "textures/a.png"));

    // Erreur du systeme de fichiers pendant la recherche (boucle de liens) :
    // elle est retournee, pas contournee par le montage suivant.
    fs.addSymlink("/mods/l1", "/mods/l2");
    fs.addSymlink("/mods/l2", "/mods/l1");
    fs.addFile("/game/l1", "decoy");
    CHECK_CODE(manager.resolve("l1"), ErrorCode::IoError);
    const Result<bool> existsOnError = manager.exists("l1");
    CHECK(!existsOnError.isOk());  // exists() ne transforme pas une erreur en "faux"
}

void testLogsAreStructured() {
    MemoryFileSystem fs;
    fs.addFile("/game/x.bin", "abc");
    ResourceManager manager(fs);
    LogCapture capture(LogLevel::Debug);
    CHECK_OK(manager.mountDirectory("game", "/game", 3));
    CHECK_OK(manager.readAll("x.bin"));

    bool mounted = false;
    bool read = false;
    for (const auto& record : capture.records()) {
        if (record.message == "directory mounted") {
            mounted = record.category == "assets" && record.fields.size() == 3 &&
                      record.fields[0].first == "mount" && record.fields[0].second == "game" &&
                      record.fields[2].first == "priority" && record.fields[2].second == "3";
        }
        if (record.message == "resource read") {
            read = record.fields.size() == 3 && record.fields[2].first == "bytes" &&
                   record.fields[2].second == "3";
        }
    }
    CHECK(mounted);
    CHECK(read);
}

// ---------------------------------------------------------------------------
// Securite : traversee de chemins et liens symboliques (double memoire)
// ---------------------------------------------------------------------------
void testPathTraversalIsRejectedBeforeTouchingTheFilesystem() {
    MemoryFileSystem fs;
    populate(fs);
    ResourceManager manager(fs);
    CHECK_OK(manager.mountDirectory("game", "/game"));

    const std::vector<std::string> attacks = {
        "../outside/secret.txt", "/outside/secret.txt", "textures/../../outside/secret.txt",
        "..\\outside\\secret.txt", "textures/..", "%2e%2e/outside"};
    for (const std::string& attack : attacks) {
        const Result<Bytes> bytes = manager.readAll(attack);
        if (bytes.isOk()) {
            std::fprintf(stderr, "  traversal succeeded: '%s'\n", attack.c_str());
        }
        CHECK(!bytes.isOk());
        if (attack != "%2e%2e/outside") {  // celui-ci est un nom valide mais inexistant
            CHECK_CODE(bytes, ErrorCode::InvalidArgument);
        } else {
            CHECK_CODE(bytes, ErrorCode::NotFound);
        }
    }
}

void testSymlinkEscapesAreDenied() {
    MemoryFileSystem fs;
    populate(fs);
    fs.addSymlink("/game/leak.txt", "/outside/secret.txt");  // lien fichier vers l'exterieur
    fs.addSymlink("/game/linkdir", "/outside");              // lien dossier vers l'exterieur
    ResourceManager manager(fs);
    CHECK_OK(manager.mountDirectory("game", "/game"));

    LogCapture capture(LogLevel::Warning);
    const Result<Bytes> viaFileLink = manager.readAll("leak.txt");
    CHECK_CODE(viaFileLink, ErrorCode::AccessDenied);
    CHECK(contains(viaFileLink.status().message(), "outside"));
    CHECK_CODE(manager.resolve("linkdir/secret.txt"), ErrorCode::AccessDenied);
    CHECK_CODE(manager.exists("leak.txt"), ErrorCode::AccessDenied);  // erreur, pas "faux"
    CHECK(capture.contains(LogLevel::Warning, "outside its mount"));

    // Aucune fuite du contenu secret, par aucune API.
    CHECK(!manager.readAll("linkdir/secret.txt").isOk());
}

void testSiblingDirectoryWithSamePrefixIsOutside() {
    // "/data/game-evil" commence par "/data/game" sans en faire partie.
    MemoryFileSystem fs;
    fs.addFile("/data/game/ok.txt", "OK");
    fs.addFile("/data/game-evil/x.txt", "EVIL");
    fs.addSymlink("/data/game/l", "/data/game-evil");
    ResourceManager manager(fs);
    CHECK_OK(manager.mountDirectory("game", "/data/game"));

    CHECK_OK(manager.readAll("ok.txt"));
    CHECK_CODE(manager.readAll("l/x.txt"), ErrorCode::AccessDenied);
}

void testInternalSymlinkIsAllowed() {
    MemoryFileSystem fs;
    fs.addFile("/game/real/data.txt", "REAL");
    fs.addSymlink("/game/alias", "/game/real");
    ResourceManager manager(fs);
    CHECK_OK(manager.mountDirectory("game", "/game"));

    const Result<ResolvedResource> resolved = manager.resolve("alias/data.txt");
    CHECK_OK(resolved);
    CHECK(resolved.isOk() && resolved.value().hostPath == "/game/real/data.txt");
    const Result<Bytes> bytes = manager.readAll("alias/data.txt");
    CHECK(bytes.isOk() && asText(bytes.value()) == "REAL");
}

void testHigherPriorityEscapeDoesNotFallThrough() {
    MemoryFileSystem fs;
    populate(fs);
    fs.addSymlink("/mods/leak.txt", "/outside/secret.txt");
    fs.addFile("/game/leak.txt", "legit base file");
    ResourceManager manager(fs);
    CHECK_OK(manager.mountDirectory("mods", "/mods", 10));
    CHECK_OK(manager.mountDirectory("game", "/game", 0));

    // L'evasion est detectee et signalee ; elle n'est pas "corrigee" en servant l'autre fichier.
    CHECK_CODE(manager.readAll("leak.txt"), ErrorCode::AccessDenied);
}

void testBrokenSymlinkCountsAsAbsent() {
    MemoryFileSystem fs;
    fs.addFile("/game/fallback.txt", "FB");
    fs.addFile("/mods/other.txt", "O");
    fs.addSymlink("/mods/fallback.txt", "/does/not/exist");
    ResourceManager manager(fs);
    CHECK_OK(manager.mountDirectory("mods", "/mods", 10));
    CHECK_OK(manager.mountDirectory("game", "/game", 0));
    const Result<Bytes> bytes = manager.readAll("fallback.txt");
    CHECK(bytes.isOk() && asText(bytes.value()) == "FB");
}

// ---------------------------------------------------------------------------
// Securite sur le vrai disque
// ---------------------------------------------------------------------------
void testRealDiskIntegrationAndSymlinkEscape() {
    TempDir dir;
    CHECK(dir.isValid());
    HostFileSystem fs;

    CHECK_OK(fs.createDirectories(dir.file("game/textures")));
    CHECK_OK(fs.createDirectories(dir.file("outside")));
    CHECK_OK(writeTextFile(fs, dir.file("game/textures/a.png"), "REAL-A"));
    CHECK_OK(writeTextFile(fs, dir.file("outside/secret.txt"), "SECRET"));

    std::error_code ec;
    stdfs::create_symlink(dir.file("outside/secret.txt"), dir.file("game/leak.txt"), ec);
    if (ec) {
        SKIP_TEST("symbolic links are not available here");
    }
    stdfs::create_directory_symlink(dir.file("outside"), dir.file("game/linkdir"), ec);
    stdfs::create_directory_symlink(dir.file("game/textures"), dir.file("game/alias"), ec);

    ResourceManager manager(fs);
    CHECK_OK(manager.mountDirectory("game", dir.file("game")));
    CHECK_CODE(manager.mountDirectory("nope", dir.file("absent")), ErrorCode::NotFound);

    // Lecture normale.
    const Result<Bytes> normal = manager.readAll("textures/a.png");
    CHECK(normal.isOk() && asText(normal.value()) == "REAL-A");

    // Lien interne : autorise ; lien sortant : refuse.
    const Result<Bytes> internal = manager.readAll("alias/a.png");
    CHECK(internal.isOk() && asText(internal.value()) == "REAL-A");
    CHECK_CODE(manager.readAll("leak.txt"), ErrorCode::AccessDenied);
    CHECK_CODE(manager.readAll("linkdir/secret.txt"), ErrorCode::AccessDenied);

    // Traversee classique refusee avant tout acces.
    CHECK_CODE(manager.readAll("../outside/secret.txt"), ErrorCode::InvalidArgument);
    CHECK_CODE(manager.readAll("textures/../../outside/secret.txt"), ErrorCode::InvalidArgument);

    // La racine memorisee est le chemin canonique reel.
    const Result<std::string> canonicalRoot = fs.canonicalPath(dir.file("game"));
    CHECK(canonicalRoot.isOk() && manager.mounts().size() == 1 &&
          manager.mounts()[0].rootPath == canonicalRoot.value());
}

void testRealDiskUnreadableFile() {
    TempDir dir;
    HostFileSystem fs;
    CHECK_OK(fs.createDirectories(dir.file("game")));
    CHECK_OK(writeTextFile(fs, dir.file("game/locked.bin"), "x"));
    std::error_code ec;
    stdfs::permissions(dir.file("game/locked.bin"), stdfs::perms::none, ec);
    if (ec) {
        SKIP_TEST("cannot change permissions here");
    }
    ResourceManager manager(fs);
    CHECK_OK(manager.mountDirectory("game", dir.file("game")));
    const Result<Bytes> attempt = manager.readAll("locked.bin");
    if (attempt.isOk()) {
        SKIP_TEST("permissions are not enforced (running as root?)");
    }
    CHECK_CODE(attempt, ErrorCode::AccessDenied);
    stdfs::permissions(dir.file("game/locked.bin"),
                       stdfs::perms::owner_read | stdfs::perms::owner_write, ec);
}

}  // namespace

int main() {
    std::printf("moonlight_resource_test\n");
    RUN_TEST(testVirtualPathValidation);
    RUN_TEST(testMountErrors);
    RUN_TEST(testMountOrderFollowsPriority);
    RUN_TEST(testMountStoresCanonicalRoot);
    RUN_TEST(testResolveAndReadWithPriorities);
    RUN_TEST(testNotFoundAndExists);
    RUN_TEST(testReadSizeLimit);
    RUN_TEST(testFailsClosedOnErrors);
    RUN_TEST(testLogsAreStructured);
    RUN_TEST(testPathTraversalIsRejectedBeforeTouchingTheFilesystem);
    RUN_TEST(testSymlinkEscapesAreDenied);
    RUN_TEST(testSiblingDirectoryWithSamePrefixIsOutside);
    RUN_TEST(testInternalSymlinkIsAllowed);
    RUN_TEST(testHigherPriorityEscapeDoesNotFallThrough);
    RUN_TEST(testBrokenSymlinkCountsAsAbsent);
    RUN_TEST(testRealDiskIntegrationAndSymlinkEscape);
    RUN_TEST(testRealDiskUnreadableFile);
    return moonlight::testing::finish("moonlight_resource_test");
}
