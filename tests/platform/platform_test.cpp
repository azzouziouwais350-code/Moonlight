// Tests de la couche plateforme : IFileSystem (hote + memoire), IClock.
//
// Le meme "contrat" est execute sur HostFileSystem (vrai disque, dossier
// temporaire) et sur MemoryFileSystem : cela garantit que le double de test
// se comporte comme l'implementation reelle.

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string>

#include "check.hpp"
#include "manual_clock.hpp"
#include "memory_filesystem.hpp"
#include "platform/clock.hpp"
#include "platform/filesystem.hpp"
#include "platform/host_clock.hpp"
#include "platform/host_filesystem.hpp"
#include "temp_dir.hpp"

using namespace moonlight::core;
using namespace moonlight::platform;
using moonlight::testing::ManualClock;
using moonlight::testing::MemoryFileSystem;
using moonlight::testing::TempDir;

namespace {

namespace stdfs = std::filesystem;

std::string join(const std::string& base, const std::string& name) { return base + "/" + name; }

// ---------------------------------------------------------------------------
// Contrat commun a toute implementation de IFileSystem
// ---------------------------------------------------------------------------
void runFileSystemContract(IFileSystem& fs, const std::string& base) {
    // Un dossier existant / un element absent.
    CHECK_OK(fs.createDirectories(base));
    Result<FileType> type = fs.fileType(base);
    CHECK_OK(type);
    CHECK(type.isOk() && type.value() == FileType::Directory);
    type = fs.fileType(join(base, "absent"));
    CHECK_OK(type);  // "absent" est une valeur, pas une erreur
    CHECK(type.isOk() && type.value() == FileType::NotFound);

    // Aller-retour binaire (octets nuls et 0xFF compris).
    const Bytes binary = {std::byte{0x00}, std::byte{0xFF}, std::byte{0x10}, std::byte{0x00}};
    const std::string binaryPath = join(base, "binary.bin");
    CHECK_OK(fs.writeFile(binaryPath, binary));
    Result<Bytes> read = fs.readFile(binaryPath, 1024);
    CHECK_OK(read);
    CHECK(read.isOk() && read.value() == binary);
    type = fs.fileType(binaryPath);
    CHECK(type.isOk() && type.value() == FileType::File);

    // Fichier vide.
    const std::string emptyPath = join(base, "empty.bin");
    CHECK_OK(fs.writeFile(emptyPath, {}));
    read = fs.readFile(emptyPath, 0);
    CHECK_OK(read);
    CHECK(read.isOk() && read.value().empty());

    // Remplacement du contenu.
    CHECK_OK(writeTextFile(fs, binaryPath, "second"));
    const Result<std::string> text = readTextFile(fs, binaryPath, 1024);
    CHECK_OK(text);
    CHECK(text.isOk() && text.value() == "second");

    // Limite de taille : exactement la taille = ok, un octet de moins = TooLarge.
    CHECK_OK(fs.readFile(binaryPath, 6));
    CHECK_CODE(fs.readFile(binaryPath, 5), ErrorCode::TooLarge);

    // Erreurs de lecture.
    CHECK_CODE(fs.readFile(join(base, "absent"), 1024), ErrorCode::NotFound);
    CHECK_CODE(fs.readFile(base, 1024), ErrorCode::InvalidArgument);  // un dossier

    // Ecriture sans dossier parent.
    CHECK_CODE(fs.writeFile(join(base, "no-such-dir/file"), binary), ErrorCode::NotFound);

    // createDirectories : imbrique, idempotent, refuse un fichier.
    const std::string nested = join(base, "a/b/c");
    CHECK_OK(fs.createDirectories(nested));
    CHECK_OK(fs.createDirectories(nested));
    type = fs.fileType(nested);
    CHECK(type.isOk() && type.value() == FileType::Directory);
    CHECK_CODE(fs.createDirectories(binaryPath), ErrorCode::InvalidArgument);

    // Renommage : remplace une destination existante.
    CHECK_OK(writeTextFile(fs, join(base, "src.txt"), "new"));
    CHECK_OK(writeTextFile(fs, join(base, "dst.txt"), "old"));
    CHECK_OK(fs.renameEntry(join(base, "src.txt"), join(base, "dst.txt")));
    const Result<std::string> renamed = readTextFile(fs, join(base, "dst.txt"), 1024);
    CHECK(renamed.isOk() && renamed.value() == "new");
    type = fs.fileType(join(base, "src.txt"));
    CHECK(type.isOk() && type.value() == FileType::NotFound);
    CHECK_CODE(fs.renameEntry(join(base, "src.txt"), join(base, "other.txt")), ErrorCode::NotFound);

    // Suppression.
    CHECK_OK(fs.removeFile(join(base, "dst.txt")));
    type = fs.fileType(join(base, "dst.txt"));
    CHECK(type.isOk() && type.value() == FileType::NotFound);
    CHECK_CODE(fs.removeFile(join(base, "dst.txt")), ErrorCode::NotFound);
    CHECK_CODE(fs.removeFile(nested), ErrorCode::InvalidArgument);  // un dossier

    // Chemin canonique : "." et ".." disparaissent, l'element doit exister.
    const Result<std::string> direct = fs.canonicalPath(binaryPath);
    const Result<std::string> indirect = fs.canonicalPath(join(base, "a/b/../../binary.bin"));
    CHECK_OK(direct);
    CHECK_OK(indirect);
    CHECK(direct.isOk() && indirect.isOk() && direct.value() == indirect.value());
    CHECK(direct.isOk() && direct.value().find("..") == std::string::npos);
    CHECK_CODE(fs.canonicalPath(join(base, "absent")), ErrorCode::NotFound);

    // Chemins invalides : vides ou contenant NUL.
    const std::string withNul("bad\0path", 8);
    CHECK_CODE(fs.fileType(""), ErrorCode::InvalidArgument);
    CHECK_CODE(fs.fileType(withNul), ErrorCode::InvalidArgument);
    CHECK_CODE(fs.readFile("", 10), ErrorCode::InvalidArgument);
    CHECK_CODE(fs.readFile(withNul, 10), ErrorCode::InvalidArgument);
    CHECK_CODE(fs.writeFile("", binary), ErrorCode::InvalidArgument);
    CHECK_CODE(fs.writeFile(withNul, binary), ErrorCode::InvalidArgument);
    CHECK_CODE(fs.createDirectories(""), ErrorCode::InvalidArgument);
    CHECK_CODE(fs.renameEntry("", join(base, "x")), ErrorCode::InvalidArgument);
    CHECK_CODE(fs.removeFile(withNul), ErrorCode::InvalidArgument);
    CHECK_CODE(fs.canonicalPath(""), ErrorCode::InvalidArgument);
}

void testHostFileSystemContract() {
    TempDir dir;
    CHECK(dir.isValid());
    HostFileSystem fs;
    runFileSystemContract(fs, dir.file("contract"));
}

void testMemoryFileSystemContract() {
    MemoryFileSystem fs;
    runFileSystemContract(fs, "/sandbox");
}

// ---------------------------------------------------------------------------
// Cas propres a l'implementation hote (vrai disque)
// ---------------------------------------------------------------------------
void testHostSymlinks() {
    TempDir dir;
    HostFileSystem fs;
    CHECK_OK(fs.createDirectories(dir.file("real")));
    CHECK_OK(writeTextFile(fs, dir.file("real/data.txt"), "payload"));

    std::error_code ec;
    stdfs::create_directory_symlink(dir.file("real"), dir.file("dirlink"), ec);
    if (ec) {
        SKIP_TEST("symbolic links are not available here");
    }
    stdfs::create_symlink(dir.file("real/data.txt"), dir.file("filelink"), ec);
    stdfs::create_symlink(dir.file("does-not-exist"), dir.file("broken"), ec);

    // Les liens sont suivis par fileType/readFile.
    const Result<FileType> linkType = fs.fileType(dir.file("dirlink"));
    CHECK(linkType.isOk() && linkType.value() == FileType::Directory);
    const Result<std::string> viaLink = readTextFile(fs, dir.file("dirlink/data.txt"), 100);
    CHECK(viaLink.isOk() && viaLink.value() == "payload");

    // canonicalPath resout le lien vers la vraie cible.
    const Result<std::string> canonicalViaLink = fs.canonicalPath(dir.file("filelink"));
    const Result<std::string> canonicalReal = fs.canonicalPath(dir.file("real/data.txt"));
    CHECK_OK(canonicalViaLink);
    CHECK(canonicalViaLink.isOk() && canonicalReal.isOk() &&
          canonicalViaLink.value() == canonicalReal.value());

    // Un lien casse est "absent".
    const Result<FileType> broken = fs.fileType(dir.file("broken"));
    CHECK(broken.isOk() && broken.value() == FileType::NotFound);
    CHECK_CODE(fs.canonicalPath(dir.file("broken")), ErrorCode::NotFound);
}

void testHostSpecialFileIsNotReadable() {
    HostFileSystem fs;
    const Result<FileType> type = fs.fileType("/dev/null");
    if (!type.isOk() || type.value() != FileType::Other) {
        SKIP_TEST("/dev/null is not available as a special file here");
    }
    CHECK_CODE(fs.readFile("/dev/null", 1024), ErrorCode::InvalidArgument);
}

void testHostUnreadableFile() {
    TempDir dir;
    HostFileSystem fs;
    const std::string path = dir.file("secret.txt");
    CHECK_OK(writeTextFile(fs, path, "hidden"));

    std::error_code ec;
    stdfs::permissions(path, stdfs::perms::none, ec);
    if (ec) {
        SKIP_TEST("cannot change permissions here");
    }
    const Result<Bytes> attempt = fs.readFile(path, 100);
    if (attempt.isOk()) {
        // Execute en root : les permissions ne s'appliquent pas.
        SKIP_TEST("permissions are not enforced (running as root?)");
    }
    CHECK_CODE(attempt, ErrorCode::AccessDenied);
    stdfs::permissions(path, stdfs::perms::owner_read | stdfs::perms::owner_write, ec);
}

void testHostWriteIntoReadOnlyDirectory() {
    TempDir dir;
    HostFileSystem fs;
    CHECK_OK(fs.createDirectories(dir.file("ro")));
    std::error_code ec;
    stdfs::permissions(dir.file("ro"), stdfs::perms::owner_read | stdfs::perms::owner_exec, ec);
    if (ec) {
        SKIP_TEST("cannot change permissions here");
    }
    const Status attempt = writeTextFile(fs, dir.file("ro/file.txt"), "x");
    if (attempt.isOk()) {
        stdfs::permissions(dir.file("ro"), stdfs::perms::owner_all, ec);
        SKIP_TEST("permissions are not enforced (running as root?)");
    }
    CHECK_CODE(attempt, ErrorCode::AccessDenied);
    stdfs::permissions(dir.file("ro"), stdfs::perms::owner_all, ec);
}

// ---------------------------------------------------------------------------
// Cas propres au double de test en memoire
// ---------------------------------------------------------------------------
void testMemoryFileSystemSimulations() {
    MemoryFileSystem fs;
    fs.addFile("/data/a.txt", "A");
    fs.addFile("/outside/secret.txt", "S");
    fs.addSymlink("/data/link", "/outside");

    // ".." est resolu physiquement : apres le lien, on remonte depuis la cible.
    const Result<std::string> viaLink = fs.canonicalPath("/data/link/secret.txt");
    CHECK(viaLink.isOk() && viaLink.value() == "/outside/secret.txt");
    const Result<std::string> parentOfTarget = fs.canonicalPath("/data/link/../outside/secret.txt");
    CHECK(parentOfTarget.isOk() && parentOfTarget.value() == "/outside/secret.txt");

    // Boucle de liens.
    fs.addSymlink("/loop/a", "/loop/b");
    fs.addSymlink("/loop/b", "/loop/a");
    CHECK_CODE(fs.fileType("/loop/a"), ErrorCode::IoError);

    // Fichier illisible et pannes d'ecriture simulees.
    fs.setUnreadable("/data/a.txt");
    CHECK_CODE(fs.readFile("/data/a.txt", 10), ErrorCode::AccessDenied);
    fs.setFailWrites(true);
    CHECK_CODE(writeTextFile(fs, "/data/b.txt", "x"), ErrorCode::IoError);
    fs.setFailWrites(false);
    CHECK_OK(writeTextFile(fs, "/data/b.txt", "x"));
    CHECK(fs.contentOf("/data/b.txt").value_or("") == "x");

    // Un chemin relatif est interprete depuis la racine.
    CHECK(fs.hasFile("data/b.txt"));
}

// ---------------------------------------------------------------------------
// Horloges
// ---------------------------------------------------------------------------
void testHostClock() {
    HostClock clock;
    const std::chrono::nanoseconds first = clock.monotonicNow();
    std::chrono::nanoseconds previous = first;
    bool nonDecreasing = true;
    for (int i = 0; i < 1000; ++i) {
        const std::chrono::nanoseconds now = clock.monotonicNow();
        nonDecreasing = nonDecreasing && now >= previous;
        previous = now;
    }
    CHECK(nonDecreasing);

    const auto wall = clock.systemNow();
    const auto reference = std::chrono::system_clock::now();
    const auto difference = reference > wall ? reference - wall : wall - reference;
    CHECK(difference < std::chrono::seconds(5));
}

void testManualClock() {
    ManualClock clock;
    CHECK(clock.monotonicNow() == std::chrono::nanoseconds(0));
    clock.advance(std::chrono::milliseconds(1500));
    CHECK(clock.monotonicNow() == std::chrono::milliseconds(1500));
    clock.advance(std::chrono::milliseconds(500));
    CHECK(clock.monotonicNow() == std::chrono::seconds(2));

    const auto fixed = std::chrono::system_clock::time_point(std::chrono::seconds(1000));
    clock.setSystemTime(fixed);
    CHECK(clock.systemNow() == fixed);

    // Utilisable a travers l'interface.
    const IClock& asInterface = clock;
    CHECK(asInterface.monotonicNow() == std::chrono::seconds(2));
}

void testTextHelpers() {
    const Bytes bytes = toBytes("a\0b");  // s'arrete au premier NUL (const char*)
    CHECK(bytes.size() == 1);
    const std::string_view withNul("a\0b", 3);
    CHECK(toBytes(withNul).size() == 3);

    MemoryFileSystem fs;
    CHECK_OK(fs.createDirectories("/t"));
    CHECK_OK(writeTextFile(fs, "/t/bin", withNul));
    const Result<std::string> back = readTextFile(fs, "/t/bin", 10);
    CHECK(back.isOk() && back.value() == std::string(withNul));
    CHECK_CODE(readTextFile(fs, "/t/missing", 10), ErrorCode::NotFound);
}

}  // namespace

int main() {
    std::printf("moonlight_platform_test\n");
    RUN_TEST(testHostFileSystemContract);
    RUN_TEST(testMemoryFileSystemContract);
    RUN_TEST(testHostSymlinks);
    RUN_TEST(testHostSpecialFileIsNotReadable);
    RUN_TEST(testHostUnreadableFile);
    RUN_TEST(testHostWriteIntoReadOnlyDirectory);
    RUN_TEST(testMemoryFileSystemSimulations);
    RUN_TEST(testHostClock);
    RUN_TEST(testManualClock);
    RUN_TEST(testTextHelpers);
    return moonlight::testing::finish("moonlight_platform_test");
}
