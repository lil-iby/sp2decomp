// nn::fs over a host folder — see port/include/nn/fs.hpp.
#include "nn/fs.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace nn::fs {

namespace {
namespace stdfs = std::filesystem;

// The cache the caller supplies is host memory the port does not need; report a size the
// game's heap sizing treats as plausible.
constexpr std::uint64_t kPlausibleRomCacheSize = 4ull * 1024 * 1024;
constexpr int kMaxMounts = 8;

struct Mount {
    bool used = false;
    std::string name;
    stdfs::path root;
};

std::mutex g_lock;
Mount g_mounts[kMaxMounts];
bool g_hasRootOverride = false;
stdfs::path g_rootOverride;

struct HostFile {
    std::mutex m; // ReadFile is seek + read on one stream
    std::ifstream in;
    std::int64_t size = 0;
};

struct HostDirectory {
    std::vector<DirectoryEntry> entries;
    std::size_t next = 0;
};

stdfs::path EnvRomRoot() {
#if defined(_WIN32)
    wchar_t *value = nullptr;
    std::size_t len = 0;
    if (_wdupenv_s(&value, &len, L"SP2_ROMFS") != 0 || value == nullptr) return {};
    stdfs::path p(value);
    std::free(value);
    return p;
#else
    const char *value = std::getenv("SP2_ROMFS");
    return value != nullptr ? stdfs::path(value) : stdfs::path();
#endif
}

stdfs::path CurrentRomRoot() {
    if (g_hasRootOverride) return g_rootOverride;
    stdfs::path env = EnvRomRoot();
    if (!env.empty()) return env;
    return "romfs";
}

std::string Printable(const stdfs::path &p) {
    const auto u8 = p.u8string();
    return std::string(reinterpret_cast<const char *>(u8.c_str()), u8.size());
}

// "content:/Model/Foo.szs" -> <root of "content">/Model/Foo.szs. Components "..", or
// containing a backslash or colon, are refused so a path can never leave the mount.
int Resolve(const char *path, stdfs::path *out) {
    if (path == nullptr || out == nullptr) return ResultInvalidArgument;
    const char *colon = std::strchr(path, ':');
    if (colon == nullptr) return ResultInvalidArgument;
    const std::string name(path, colon);

    stdfs::path root;
    {
        std::lock_guard<std::mutex> g(g_lock);
        for (const Mount &m : g_mounts) {
            if (m.used && m.name == name) root = m.root;
        }
    }
    if (root.empty()) return ResultInvalidArgument; // not mounted

    stdfs::path result = root;
    const char *p = colon + 1;
    while (*p != '\0') {
        while (*p == '/') ++p;
        const char *end = p;
        while (*end != '\0' && *end != '/') ++end;
        const std::string component(p, end);
        p = end;
        if (component.empty() || component == ".") continue;
        if (component == ".." || component.find_first_of("\\:") != std::string::npos) {
            return ResultInvalidArgument;
        }
        result /= stdfs::path(component);
    }
    *out = result;
    return 0;
}

HostFile *AsFile(FileHandle h) { return static_cast<HostFile *>(h.handle); }
HostDirectory *AsDirectory(DirectoryHandle h) { return static_cast<HostDirectory *>(h.handle); }

} // namespace

int QueryMountRomCacheSize(std::uint64_t *outSize) {
    if (outSize == nullptr) return ResultInvalidArgument;
    *outSize = kPlausibleRomCacheSize;
    return 0;
}

int MountRom(const char *name, void *cache, std::uint64_t cacheSize) {
    if (name == nullptr || cache == nullptr || cacheSize == 0) return ResultInvalidArgument;
    stdfs::path root;
    {
        std::lock_guard<std::mutex> g(g_lock);
        // Mounting one name twice would give two handles for one filesystem.
        for (const Mount &m : g_mounts) {
            if (m.used && m.name == name) return ResultInvalidArgument;
        }
        Mount *slot = nullptr;
        for (Mount &m : g_mounts) {
            if (!m.used) { slot = &m; break; }
        }
        if (slot == nullptr) return ResultInvalidArgument; // mount table full
        root = CurrentRomRoot();
        slot->used = true;
        slot->name = name;
        slot->root = root;
    }
    // The mount itself succeeds either way so boot can proceed; say why reads will fail.
    std::error_code ec;
    if (!stdfs::is_directory(root, ec)) {
        std::fprintf(stderr,
                     "nn::fs::MountRom(\"%s\"): no RomFS folder at '%s' -- set SP2_ROMFS to the "
                     "extracted romfs of your game dump; file reads will fail\n",
                     name, Printable(root).c_str());
    }
    return 0;
}

int Unmount(const char *name) {
    if (name == nullptr) return ResultInvalidArgument;
    std::lock_guard<std::mutex> g(g_lock);
    for (Mount &m : g_mounts) {
        if (m.used && m.name == name) {
            m = Mount{};
            return 0;
        }
    }
    return ResultInvalidArgument;
}

int OpenFile(FileHandle *outHandle, const char *path, int mode) {
    if (outHandle == nullptr) return ResultInvalidArgument;
    outHandle->handle = nullptr;
    if ((mode & OpenMode_Read) == 0 || (mode & (OpenMode_Write | OpenMode_AllowAppend)) != 0) {
        return ResultInvalidArgument; // RomFS is read-only
    }
    stdfs::path hostPath;
    if (const int rc = Resolve(path, &hostPath); rc != 0) return rc;

    std::error_code ec;
    if (!stdfs::is_regular_file(hostPath, ec)) return ResultPathNotFound;
    const auto size = stdfs::file_size(hostPath, ec);
    if (ec) return ResultPathNotFound;

    auto file = std::make_unique<HostFile>();
    file->in.open(hostPath, std::ios::binary);
    if (!file->in.is_open()) return ResultPathNotFound;
    file->size = static_cast<std::int64_t>(size);
    outHandle->handle = file.release();
    return 0;
}

int ReadFile(std::size_t *outReadSize, FileHandle handle, std::int64_t offset, void *buffer,
             std::size_t size) {
    HostFile *f = AsFile(handle);
    if (outReadSize == nullptr || f == nullptr || (buffer == nullptr && size != 0)) {
        return ResultInvalidArgument;
    }
    *outReadSize = 0;
    if (offset < 0 || offset > f->size) return ResultOutOfRange;
    const auto available = static_cast<std::uint64_t>(f->size - offset);
    const std::size_t toRead =
        static_cast<std::size_t>(std::min<std::uint64_t>(available, size));
    if (toRead == 0) return 0;

    std::lock_guard<std::mutex> g(f->m);
    f->in.clear();
    f->in.seekg(offset);
    f->in.read(static_cast<char *>(buffer), static_cast<std::streamsize>(toRead));
    *outReadSize = static_cast<std::size_t>(f->in.gcount());
    return 0;
}

int ReadFile(FileHandle handle, std::int64_t offset, void *buffer, std::size_t size) {
    HostFile *f = AsFile(handle);
    if (f == nullptr) return ResultInvalidArgument;
    if (offset < 0 || static_cast<std::uint64_t>(offset) + size >
                          static_cast<std::uint64_t>(f->size)) {
        return ResultOutOfRange;
    }
    std::size_t read = 0;
    if (const int rc = ReadFile(&read, handle, offset, buffer, size); rc != 0) return rc;
    return read == size ? 0 : ResultOutOfRange;
}

int GetFileSize(std::int64_t *outSize, FileHandle handle) {
    HostFile *f = AsFile(handle);
    if (outSize == nullptr || f == nullptr) return ResultInvalidArgument;
    *outSize = f->size;
    return 0;
}

void CloseFile(FileHandle handle) { delete AsFile(handle); }

int GetEntryType(DirectoryEntryType *outType, const char *path) {
    if (outType == nullptr) return ResultInvalidArgument;
    stdfs::path hostPath;
    if (const int rc = Resolve(path, &hostPath); rc != 0) return rc;
    std::error_code ec;
    const auto status = stdfs::status(hostPath, ec);
    if (stdfs::is_directory(status)) {
        *outType = DirectoryEntryType_Directory;
        return 0;
    }
    if (stdfs::is_regular_file(status)) {
        *outType = DirectoryEntryType_File;
        return 0;
    }
    return ResultPathNotFound;
}

int OpenDirectory(DirectoryHandle *outHandle, const char *path, int mode) {
    if (outHandle == nullptr) return ResultInvalidArgument;
    outHandle->handle = nullptr;
    if ((mode & OpenDirectoryMode_All) == 0) return ResultInvalidArgument;
    stdfs::path hostPath;
    if (const int rc = Resolve(path, &hostPath); rc != 0) return rc;

    std::error_code ec;
    if (!stdfs::is_directory(hostPath, ec)) return ResultPathNotFound;

    auto dir = std::make_unique<HostDirectory>();
    for (stdfs::directory_iterator it(hostPath, ec), end; !ec && it != end; it.increment(ec)) {
        const bool isDir = it->is_directory(ec);
        const bool isFile = !isDir && it->is_regular_file(ec);
        if (isDir && (mode & OpenDirectoryMode_Directory) == 0) continue;
        if (isFile && (mode & OpenDirectoryMode_File) == 0) continue;
        if (!isDir && !isFile) continue;

        const auto u8 = it->path().filename().u8string();
        if (u8.size() >= sizeof(DirectoryEntry::name)) continue; // cannot be represented
        DirectoryEntry entry{};
        std::memcpy(entry.name, u8.data(), u8.size());
        entry.type = isDir ? DirectoryEntryType_Directory : DirectoryEntryType_File;
        entry.fileSize = isFile ? static_cast<std::int64_t>(it->file_size(ec)) : 0;
        dir->entries.push_back(entry);
    }
    if (ec) return ResultPathNotFound;
    // Host iteration order is unspecified; sort so the game sees a stable order.
    std::sort(dir->entries.begin(), dir->entries.end(),
              [](const DirectoryEntry &a, const DirectoryEntry &b) {
                  return std::strcmp(a.name, b.name) < 0;
              });
    outHandle->handle = dir.release();
    return 0;
}

int ReadDirectory(std::int64_t *outCount, DirectoryEntry *entries, DirectoryHandle handle,
                  std::int64_t maxEntries) {
    HostDirectory *d = AsDirectory(handle);
    if (outCount == nullptr || d == nullptr || maxEntries < 0 ||
        (entries == nullptr && maxEntries != 0)) {
        return ResultInvalidArgument;
    }
    const std::size_t remaining = d->entries.size() - d->next;
    const std::size_t n = std::min<std::size_t>(remaining, static_cast<std::size_t>(maxEntries));
    for (std::size_t i = 0; i < n; ++i) entries[i] = d->entries[d->next + i];
    d->next += n;
    *outCount = static_cast<std::int64_t>(n);
    return 0;
}

int GetDirectoryEntryCount(std::int64_t *outCount, DirectoryHandle handle) {
    HostDirectory *d = AsDirectory(handle);
    if (outCount == nullptr || d == nullptr) return ResultInvalidArgument;
    *outCount = static_cast<std::int64_t>(d->entries.size());
    return 0;
}

void CloseDirectory(DirectoryHandle handle) { delete AsDirectory(handle); }

namespace host {
void SetRomRoot(const char *path) {
    std::lock_guard<std::mutex> g(g_lock);
    g_hasRootOverride = path != nullptr;
    g_rootOverride = path != nullptr ? stdfs::path(path) : stdfs::path();
}
} // namespace host

} // namespace nn::fs
