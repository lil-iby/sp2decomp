// fs_tests.cpp — nn::fs reads from a host RomFS folder.
//
// Builds a tiny fake RomFS in a temporary folder (no game data involved), mounts it as
// "content" the way the game does, and reads it back through the SDK calls.
#include "nn/fs.hpp"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {
int g_failures = 0, g_checks = 0;

void Check(bool cond, const char *what) {
    ++g_checks;
    if (!cond) { ++g_failures; std::printf("  FAIL  %s\n", what); }
    else        std::printf("  ok    %s\n", what);
}

namespace stdfs = std::filesystem;

void WriteBytes(const stdfs::path &p, const std::string &bytes) {
    stdfs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary).write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

std::vector<char> g_cache(4 * 1024 * 1024);
} // namespace

int main() {
    std::printf("nn::fs RomFS tests\n");

    const stdfs::path root = stdfs::temp_directory_path() / "sp2_fs_tests_romfs";
    stdfs::remove_all(root);
    const std::string payload = "0123456789ABCDEF";
    WriteBytes(root / "Model" / "Test.bin", payload);
    WriteBytes(root / "Model" / "Other.bin", "xy");
    WriteBytes(root / "Pack" / "Empty.pack", "");
    stdfs::create_directories(root / "Model" / "Sub");

    nn::fs::host::SetRomRoot(root.string().c_str());
    std::uint64_t cacheSize = 0;
    nn::fs::QueryMountRomCacheSize(&cacheSize);
    Check(nn::fs::MountRom("content", g_cache.data(), cacheSize) == 0, "MountRom(\"content\") succeeds");

    std::printf("\n[1] files\n");
    nn::fs::FileHandle f{};
    Check(nn::fs::OpenFile(&f, "content:/Model/Test.bin", nn::fs::OpenMode_Read) == 0, "OpenFile");
    std::int64_t size = 0;
    Check(nn::fs::GetFileSize(&size, f) == 0 && size == 16, "GetFileSize == 16");

    char buf[32] = {};
    Check(nn::fs::ReadFile(f, 0, buf, 16) == 0 && std::memcmp(buf, payload.data(), 16) == 0,
          "ReadFile reads the whole file");
    Check(nn::fs::ReadFile(f, 10, buf, 4) == 0 && std::memcmp(buf, "ABCD", 4) == 0,
          "ReadFile at an offset");
    Check(nn::fs::ReadFile(f, 12, buf, 8) == nn::fs::ResultOutOfRange,
          "exact-size ReadFile past the end is OutOfRange");
    std::size_t got = 0;
    Check(nn::fs::ReadFile(&got, f, 12, buf, 8) == 0 && got == 4 && std::memcmp(buf, "CDEF", 4) == 0,
          "ReadFile with out-size returns the 4 remaining bytes");
    Check(nn::fs::ReadFile(&got, f, 16, buf, 8) == 0 && got == 0, "reading at the end gives 0 bytes");
    Check(nn::fs::ReadFile(&got, f, 17, buf, 1) == nn::fs::ResultOutOfRange, "past the end is OutOfRange");
    nn::fs::CloseFile(f);

    Check(nn::fs::OpenFile(&f, "content:Model//Test.bin", nn::fs::OpenMode_Read) == 0,
          "missing leading slash and doubled slashes are tolerated");
    nn::fs::CloseFile(f);

    std::printf("\n[2] rejected opens\n");
    Check(nn::fs::OpenFile(&f, "content:/Model/Missing.bin", nn::fs::OpenMode_Read) ==
              nn::fs::ResultPathNotFound, "a missing file is PathNotFound");
    Check(nn::fs::OpenFile(&f, "content:/Model", nn::fs::OpenMode_Read) == nn::fs::ResultPathNotFound,
          "a directory cannot be opened as a file");
    Check(nn::fs::OpenFile(&f, "content:/Model/Test.bin", nn::fs::OpenMode_Write) != 0,
          "write mode is refused (RomFS is read-only)");
    Check(nn::fs::OpenFile(&f, "content:/../sp2_fs_tests_romfs/Model/Test.bin",
                           nn::fs::OpenMode_Read) == nn::fs::ResultInvalidArgument,
          "'..' cannot escape the mount");
    Check(nn::fs::OpenFile(&f, "data:/Model/Test.bin", nn::fs::OpenMode_Read) != 0,
          "an unmounted name fails");

    std::printf("\n[3] entry types\n");
    nn::fs::DirectoryEntryType type{};
    Check(nn::fs::GetEntryType(&type, "content:/Model/Test.bin") == 0 &&
              type == nn::fs::DirectoryEntryType_File, "a file is a File");
    Check(nn::fs::GetEntryType(&type, "content:/Model") == 0 &&
              type == nn::fs::DirectoryEntryType_Directory, "a folder is a Directory");
    Check(nn::fs::GetEntryType(&type, "content:/") == 0 &&
              type == nn::fs::DirectoryEntryType_Directory, "the mount root is a Directory");
    Check(nn::fs::GetEntryType(&type, "content:/Nope") == nn::fs::ResultPathNotFound,
          "a missing entry is PathNotFound");

    std::printf("\n[4] directories\n");
    nn::fs::DirectoryHandle d{};
    Check(nn::fs::OpenDirectory(&d, "content:/Model", nn::fs::OpenDirectoryMode_All) == 0, "OpenDirectory");
    std::int64_t count = 0;
    Check(nn::fs::GetDirectoryEntryCount(&count, d) == 0 && count == 3, "3 entries in Model");
    nn::fs::DirectoryEntry entries[2] = {};
    std::int64_t read = 0;
    Check(nn::fs::ReadDirectory(&read, entries, d, 2) == 0 && read == 2, "first batch of 2");
    Check(std::strcmp(entries[0].name, "Other.bin") == 0 && entries[0].fileSize == 2 &&
              entries[0].type == nn::fs::DirectoryEntryType_File, "sorted: Other.bin, size 2");
    Check(std::strcmp(entries[1].name, "Sub") == 0 &&
              entries[1].type == nn::fs::DirectoryEntryType_Directory, "then the Sub folder");
    Check(nn::fs::ReadDirectory(&read, entries, d, 2) == 0 && read == 1 &&
              std::strcmp(entries[0].name, "Test.bin") == 0, "second batch continues with Test.bin");
    Check(nn::fs::ReadDirectory(&read, entries, d, 2) == 0 && read == 0, "then nothing is left");
    nn::fs::CloseDirectory(d);

    Check(nn::fs::OpenDirectory(&d, "content:/Model", nn::fs::OpenDirectoryMode_File) == 0 &&
              nn::fs::GetDirectoryEntryCount(&count, d) == 0 && count == 2,
          "File mode lists only the 2 files");
    nn::fs::CloseDirectory(d);

    std::printf("\n[5] unmount\n");
    Check(nn::fs::Unmount("content") == 0, "Unmount");
    Check(nn::fs::OpenFile(&f, "content:/Model/Test.bin", nn::fs::OpenMode_Read) != 0,
          "files are unreachable after Unmount");

    nn::fs::host::SetRomRoot(nullptr);
    stdfs::remove_all(root);
    std::printf("\n%d checks, %d failure(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
