// nn::fs — RomFS mounting and read-only file/directory access.
//
// The game reaches its data through sead's file devices, which call these SDK functions
// with paths like "content:/Model/Foo.szs" (MountRom's mount name is the literal "content"
// at both call sites). On PC a mount resolves to a folder on the host: the RomFS the user
// extracted from their own copy of the game. Nothing from the game ships with the port.
//
// Host folder for a mount: nn::fs::host::SetRomRoot() if called, else the SP2_ROMFS
// environment variable, else "romfs" in the working directory.
//
// Signatures follow the SDK (results are int; 0 is success).
#pragma once

#include <cstddef>
#include <cstdint>

namespace nn::fs {

// SDK result values ((description << 9) | module 2).
constexpr int ResultPathNotFound = 0x202;
constexpr int ResultOutOfRange = (3001 << 9) | 2;
// Port-only: malformed argument (null pointer, bad path, unknown mount).
constexpr int ResultInvalidArgument = -1;

// `nn::fs::QueryMountRomCacheSize(&local_28);` — out parameter, result code returned.
int QueryMountRomCacheSize(std::uint64_t *outSize);

// `nn::fs::MountRom("content", pvVar3, local_28);` — 2 sites, and the mount name is the
// literal "content" at both. Shape: (const char *name, void *cache, std::uint64_t size).
int MountRom(const char *name, void *cache, std::uint64_t cacheSize);

// `nn::fs::Unmount("content")` is the complement used by teardown paths.
int Unmount(const char *name);

// --- files ------------------------------------------------------------------------------
enum OpenMode : int { OpenMode_Read = 1, OpenMode_Write = 2, OpenMode_AllowAppend = 4 };

struct FileHandle {
    void *handle;
};

// Only OpenMode_Read is supported: a RomFS is read-only.
int OpenFile(FileHandle *outHandle, const char *path, int mode);
// Reads exactly `size` bytes at `offset`; fails with ResultOutOfRange if the file is shorter.
int ReadFile(FileHandle handle, std::int64_t offset, void *buffer, std::size_t size);
// Reads up to `size` bytes at `offset` and reports how many were read.
int ReadFile(std::size_t *outReadSize, FileHandle handle, std::int64_t offset, void *buffer,
             std::size_t size);
int GetFileSize(std::int64_t *outSize, FileHandle handle);
void CloseFile(FileHandle handle);

// --- entries and directories ------------------------------------------------------------
enum DirectoryEntryType : std::uint8_t { DirectoryEntryType_Directory = 0, DirectoryEntryType_File = 1 };

int GetEntryType(DirectoryEntryType *outType, const char *path);

enum OpenDirectoryMode : int {
    OpenDirectoryMode_Directory = 1,
    OpenDirectoryMode_File = 2,
    OpenDirectoryMode_All = 3,
};

struct DirectoryHandle {
    void *handle;
};

// SDK layout: 0x301-byte name, type at +0x304, size at +0x308, 0x310 in all.
struct DirectoryEntry {
    char name[0x301];
    char reserved0[3];
    std::uint8_t type; // DirectoryEntryType
    char reserved1[3];
    std::int64_t fileSize;
};
static_assert(sizeof(DirectoryEntry) == 0x310, "nn::fs::DirectoryEntry must be 0x310 bytes");

int OpenDirectory(DirectoryHandle *outHandle, const char *path, int mode);
// Fills up to `maxEntries` entries, continuing where the previous call stopped.
int ReadDirectory(std::int64_t *outCount, DirectoryEntry *entries, DirectoryHandle handle,
                  std::int64_t maxEntries);
int GetDirectoryEntryCount(std::int64_t *outCount, DirectoryHandle handle);
void CloseDirectory(DirectoryHandle handle);

namespace host {
// Port-only: the host folder MountRom maps the next mount to. Null restores the default.
void SetRomRoot(const char *path);
} // namespace host

} // namespace nn::fs
