// sead file devices — see port/include/sead/file_device.hpp.
#include "sead/file_device.hpp"

#include "sead/heap.hpp"
#include "sead/heap_mgr.hpp"

#include <cstdio>
#include <cstring>
#include <new>

namespace sead {

namespace {
void CopyName(char *dst, std::size_t cap, const char *src) {
    std::snprintf(dst, cap, "%s", src != nullptr ? src : "");
}

// The handle-buffer state of NinFileDeviceBase.
struct NinFileInner {
    nn::fs::FileHandle handle;
    std::int64_t offset;
};
struct NinDirectoryInner {
    nn::fs::DirectoryHandle handle;
};
} // namespace

// ============================================================================================
// FileDevice
// ============================================================================================

FileDevice::FileDevice(const char *driveName) : IDisposer() {
    setDriveName(driveName);
    mListNode.device = this;
}

FileDevice::~FileDevice() {
    if (mListNode.isLinked()) mListNode.erase_(); // still mounted: leave the manager's list
}

void FileDevice::setDriveName(const char *name) { CopyName(mDriveName, sizeof(mDriveName), name); }

std::uint8_t *FileDevice::tryLoad(LoadArg &arg) {
    if (!isAvailable()) return nullptr;
    return doLoad_(arg);
}

std::uint8_t *FileDevice::doLoad_(LoadArg &arg) {
    if (arg.buffer != nullptr && arg.buffer_size == 0) return nullptr;

    FileHandle handle;
    if (tryOpen(&handle, arg.path, cFileOpenFlag_ReadOnly, arg.div_size) == nullptr) return nullptr;

    std::uint32_t fileSize = 0;
    if (!tryGetFileSize(&fileSize, &handle) || fileSize == 0) return nullptr;

    const std::uint32_t sizeAlign = static_cast<std::uint32_t>(
        arg.buffer_size_alignment > 0 ? arg.buffer_size_alignment : cBufferMinAlignment);
    const std::uint32_t bufferSize = (fileSize + sizeAlign - 1) / sizeAlign * sizeAlign;

    std::uint8_t *buffer = arg.buffer;
    Heap *heap = nullptr;
    if (buffer == nullptr) {
        heap = arg.heap != nullptr ? arg.heap : HeapMgr::instance()->getCurrentHeap();
        if (heap == nullptr) return nullptr;
        // A negative alignment asks sead for a tail allocation; the host heap has no tail.
        std::int32_t align = arg.alignment < 0 ? -arg.alignment : arg.alignment;
        if (align < cBufferMinAlignment) align = cBufferMinAlignment;
        buffer = static_cast<std::uint8_t *>(heap->tryAlloc(bufferSize, align));
        if (buffer == nullptr) {
            if (arg.assert_on_alloc_fail) {
                std::fprintf(stderr, "sead::FileDevice: cannot allocate %u bytes for '%s'\n",
                             bufferSize, arg.path != nullptr ? arg.path : "");
            }
            return nullptr;
        }
    } else if (arg.buffer_size < fileSize) {
        return nullptr;
    }

    std::uint32_t total = 0;
    while (total < fileSize) {
        std::uint32_t chunk = fileSize - total;
        if (arg.div_size != 0 && chunk > arg.div_size) chunk = arg.div_size;
        std::uint32_t got = 0;
        if (!tryRead(&got, &handle, buffer + total, chunk) || got == 0) break;
        total += got;
    }
    if (arg.check_read_entire_file && total != fileSize) {
        if (heap != nullptr) heap->free(buffer);
        return nullptr;
    }

    arg.read_size = total;
    arg.roundup_size = bufferSize;
    arg.need_unload = heap != nullptr;
    return buffer;
}

FileDevice *FileDevice::tryOpen(FileHandle *handle, const char *path, FileOpenFlag flag,
                                std::uint32_t divSize) {
    if (handle == nullptr || path == nullptr || !isAvailable()) return nullptr;
    handle->mDivSize = divSize;
    FileDevice *device = doOpen_(handle, path, flag);
    if (device != nullptr) {
        handle->mDevice = device;
        handle->mOriginalDevice = this;
    }
    return device;
}

bool FileDevice::tryClose(FileHandle *handle) {
    if (handle == nullptr || !isMatchDevice_(handle)) return false;
    const bool ok = doClose_(handle);
    handle->mDevice = nullptr;
    handle->mOriginalDevice = nullptr;
    return ok;
}

bool FileDevice::tryRead(std::uint32_t *bytesRead, FileHandle *handle, std::uint8_t *out,
                         std::uint32_t bytesToRead) {
    if (handle == nullptr || out == nullptr || !isMatchDevice_(handle)) return false;
    return doRead_(bytesRead, handle, out, bytesToRead);
}

bool FileDevice::trySeek(FileHandle *handle, std::int32_t offset, SeekOrigin origin) {
    if (handle == nullptr || !isMatchDevice_(handle)) return false;
    return doSeek_(handle, offset, origin);
}

bool FileDevice::tryGetCurrentSeekPos(std::uint32_t *pos, FileHandle *handle) {
    if (pos == nullptr || handle == nullptr || !isMatchDevice_(handle)) return false;
    return doGetCurrentSeekPos_(pos, handle);
}

bool FileDevice::tryGetFileSize(std::uint32_t *size, const char *path) {
    if (size == nullptr || path == nullptr || !isAvailable()) return false;
    return doGetFileSize_(size, path);
}

bool FileDevice::tryGetFileSize(std::uint32_t *size, FileHandle *handle) {
    if (size == nullptr || handle == nullptr || !isMatchDevice_(handle)) return false;
    return doGetFileSize_(size, handle);
}

bool FileDevice::tryIsExistFile(bool *exists, const char *path) {
    if (exists == nullptr || path == nullptr || !isAvailable()) return false;
    return doIsExistFile_(exists, path);
}

bool FileDevice::tryIsExistDirectory(bool *exists, const char *path) {
    if (exists == nullptr || path == nullptr || !isAvailable()) return false;
    return doIsExistDirectory_(exists, path);
}

FileDevice *FileDevice::tryOpenDirectory(DirectoryHandle *handle, const char *path) {
    if (handle == nullptr || path == nullptr || !isAvailable()) return nullptr;
    FileDevice *device = doOpenDirectory_(handle, path);
    if (device != nullptr) {
        handle->mDevice = device;
        handle->mOriginalDevice = this;
    }
    return device;
}

bool FileDevice::tryCloseDirectory(DirectoryHandle *handle) {
    if (handle == nullptr || !isMatchDevice_(handle)) return false;
    const bool ok = doCloseDirectory_(handle);
    handle->mDevice = nullptr;
    handle->mOriginalDevice = nullptr;
    return ok;
}

bool FileDevice::tryReadDirectory(std::uint32_t *entriesRead, DirectoryHandle *handle,
                                  DirectoryEntry *entries, std::uint32_t count) {
    if (handle == nullptr || (entries == nullptr && count != 0) || !isMatchDevice_(handle)) return false;
    return doReadDirectory_(entriesRead, handle, entries, count);
}

// ============================================================================================
// Handles
// ============================================================================================

FileHandle::~FileHandle() {
    if (mOriginalDevice != nullptr) mOriginalDevice->tryClose(this);
}

bool FileHandle::tryClose() { return mOriginalDevice != nullptr && mOriginalDevice->tryClose(this); }

bool FileHandle::tryRead(std::uint32_t *bytesRead, std::uint8_t *out, std::uint32_t size) {
    return mOriginalDevice != nullptr && mOriginalDevice->tryRead(bytesRead, this, out, size);
}

bool FileHandle::trySeek(std::int32_t offset, FileDevice::SeekOrigin origin) {
    return mOriginalDevice != nullptr && mOriginalDevice->trySeek(this, offset, origin);
}

bool FileHandle::tryGetCurrentSeekPos(std::uint32_t *pos) {
    return mOriginalDevice != nullptr && mOriginalDevice->tryGetCurrentSeekPos(pos, this);
}

bool FileHandle::tryGetFileSize(std::uint32_t *size) {
    return mOriginalDevice != nullptr && mOriginalDevice->tryGetFileSize(size, this);
}

DirectoryHandle::~DirectoryHandle() {
    if (mOriginalDevice != nullptr) mOriginalDevice->tryCloseDirectory(this);
}

bool DirectoryHandle::tryClose() {
    return mOriginalDevice != nullptr && mOriginalDevice->tryCloseDirectory(this);
}

bool DirectoryHandle::tryRead(std::uint32_t *entriesRead, DirectoryEntry *entries, std::uint32_t count) {
    return mOriginalDevice != nullptr && mOriginalDevice->tryReadDirectory(entriesRead, this, entries, count);
}

// ============================================================================================
// NinFileDeviceBase — nn::fs
// ============================================================================================

static_assert(sizeof(NinFileInner) <= 32 && sizeof(NinDirectoryInner) <= 32,
              "nn::fs handle state must fit the 32-byte handle buffer");

NinFileDeviceBase::NinFileDeviceBase(const char *driveName, const char *mountPoint)
    : FileDevice(driveName) {
    CopyName(mMountPoint, sizeof(mMountPoint), mountPoint);
}

bool NinFileDeviceBase::formatPathForFS_(char *out, std::size_t cap, const char *path) const {
    const int n = std::snprintf(out, cap, "%s:/%s", mMountPoint, path != nullptr ? path : "");
    if (n < 0 || static_cast<std::size_t>(n) >= cap) return false;
    for (char *c = out; *c != '\0'; ++c) {
        if (*c == '\\') *c = '/';
    }
    return true;
}

FileDevice *NinFileDeviceBase::doOpen_(FileHandle *handle, const char *path, FileOpenFlag flag) {
    if (flag != cFileOpenFlag_ReadOnly) {
        mLastError = nn::fs::ResultInvalidArgument; // writing is not ported (read-only RomFS)
        return nullptr;
    }
    char fsPath[512];
    if (!formatPathForFS_(fsPath, sizeof(fsPath), path)) {
        mLastError = nn::fs::ResultInvalidArgument;
        return nullptr;
    }
    auto *inner = new (handleBuffer_(handle)) NinFileInner{};
    mLastError = nn::fs::OpenFile(&inner->handle, fsPath, nn::fs::OpenMode_Read);
    return mLastError == 0 ? this : nullptr;
}

bool NinFileDeviceBase::doClose_(FileHandle *handle) {
    auto *inner = reinterpret_cast<NinFileInner *>(handleBuffer_(handle));
    nn::fs::CloseFile(inner->handle);
    inner->handle.handle = nullptr;
    mLastError = 0;
    return true;
}

bool NinFileDeviceBase::doRead_(std::uint32_t *bytesRead, FileHandle *handle, std::uint8_t *out,
                                std::uint32_t bytesToRead) {
    auto *inner = reinterpret_cast<NinFileInner *>(handleBuffer_(handle));
    std::size_t got = 0;
    mLastError = nn::fs::ReadFile(&got, inner->handle, inner->offset, out, bytesToRead);
    if (mLastError != 0) return false;
    inner->offset += static_cast<std::int64_t>(got);
    if (bytesRead != nullptr) *bytesRead = static_cast<std::uint32_t>(got);
    return true;
}

bool NinFileDeviceBase::doSeek_(FileHandle *handle, std::int32_t offset, SeekOrigin origin) {
    auto *inner = reinterpret_cast<NinFileInner *>(handleBuffer_(handle));
    switch (origin) {
    case cSeekOrigin_Begin:
        inner->offset = offset;
        return true;
    case cSeekOrigin_Current:
        inner->offset += offset;
        return true;
    case cSeekOrigin_End: {
        std::uint32_t size = 0;
        if (!doGetFileSize_(&size, handle)) return false;
        inner->offset = static_cast<std::int64_t>(size) + offset;
        return true;
    }
    }
    return false;
}

bool NinFileDeviceBase::doGetCurrentSeekPos_(std::uint32_t *pos, FileHandle *handle) {
    *pos = static_cast<std::uint32_t>(reinterpret_cast<NinFileInner *>(handleBuffer_(handle))->offset);
    return true;
}

bool NinFileDeviceBase::doGetFileSize_(std::uint32_t *size, const char *path) {
    FileHandle handle;
    if (doOpen_(&handle, path, cFileOpenFlag_ReadOnly) == nullptr) return false;
    const bool ok = doGetFileSize_(size, &handle);
    doClose_(&handle); // opened directly, so the handle carries no device to close it
    return ok;
}

bool NinFileDeviceBase::doGetFileSize_(std::uint32_t *size, FileHandle *handle) {
    std::int64_t s = 0;
    mLastError = nn::fs::GetFileSize(&s, reinterpret_cast<NinFileInner *>(handleBuffer_(handle))->handle);
    if (mLastError != 0) return false;
    *size = static_cast<std::uint32_t>(s);
    return true;
}

bool NinFileDeviceBase::doIsExistFile_(bool *exists, const char *path) {
    char fsPath[512];
    if (!formatPathForFS_(fsPath, sizeof(fsPath), path)) return false;
    nn::fs::DirectoryEntryType type{};
    mLastError = nn::fs::GetEntryType(&type, fsPath);
    if (mLastError == 0) { *exists = type == nn::fs::DirectoryEntryType_File; return true; }
    if (mLastError == nn::fs::ResultPathNotFound) { *exists = false; return true; }
    return false;
}

bool NinFileDeviceBase::doIsExistDirectory_(bool *exists, const char *path) {
    char fsPath[512];
    if (!formatPathForFS_(fsPath, sizeof(fsPath), path)) return false;
    nn::fs::DirectoryEntryType type{};
    mLastError = nn::fs::GetEntryType(&type, fsPath);
    if (mLastError == 0) { *exists = type == nn::fs::DirectoryEntryType_Directory; return true; }
    if (mLastError == nn::fs::ResultPathNotFound) { *exists = false; return true; }
    return false;
}

FileDevice *NinFileDeviceBase::doOpenDirectory_(DirectoryHandle *handle, const char *path) {
    char fsPath[512];
    if (!formatPathForFS_(fsPath, sizeof(fsPath), path)) return nullptr;
    auto *inner = new (handleBuffer_(handle)) NinDirectoryInner{};
    mLastError = nn::fs::OpenDirectory(&inner->handle, fsPath, nn::fs::OpenDirectoryMode_All);
    return mLastError == 0 ? this : nullptr;
}

bool NinFileDeviceBase::doCloseDirectory_(DirectoryHandle *handle) {
    auto *inner = reinterpret_cast<NinDirectoryInner *>(handleBuffer_(handle));
    nn::fs::CloseDirectory(inner->handle);
    inner->handle.handle = nullptr;
    return true;
}

bool NinFileDeviceBase::doReadDirectory_(std::uint32_t *entriesRead, DirectoryHandle *handle,
                                         DirectoryEntry *entries, std::uint32_t count) {
    auto *inner = reinterpret_cast<NinDirectoryInner *>(handleBuffer_(handle));
    for (std::uint32_t i = 0; i < count; ++i) {
        nn::fs::DirectoryEntry entry{};
        std::int64_t n = 0;
        mLastError = nn::fs::ReadDirectory(&n, &entry, inner->handle, 1);
        if (mLastError != 0) return false;
        if (n != 1) {
            if (entriesRead != nullptr) *entriesRead = i;
            return true;
        }
        CopyName(entries[i].name, sizeof(entries[i].name), entry.name);
        entries[i].is_directory = entry.type == nn::fs::DirectoryEntryType_Directory;
    }
    if (entriesRead != nullptr) *entriesRead = count;
    return true;
}

// ============================================================================================
// MainFileDevice
// ============================================================================================

MainFileDevice::MainFileDevice(Heap *heap) : FileDevice("main"), mHeap(heap), mFileDevice(nullptr) {
    void *mem = heap != nullptr ? heap->tryAlloc(sizeof(NinContentFileDevice), alignof(NinContentFileDevice))
                                : nullptr;
    mFileDevice = mem != nullptr ? static_cast<FileDevice *>(new (mem) NinContentFileDevice())
                                 : static_cast<FileDevice *>(new NinContentFileDevice());
}

MainFileDevice::~MainFileDevice() {
    if (mHeap != nullptr && mHeap->isInclude(mFileDevice)) {
        mFileDevice->~FileDevice();
        mHeap->free(mFileDevice);
    } else {
        delete mFileDevice;
    }
}

// ============================================================================================
// FileDeviceMgr
// ============================================================================================

namespace {
alignas(FileDeviceMgr) unsigned char gMgrStorage[sizeof(FileDeviceMgr)];
FileDeviceMgr *gMgr = nullptr;
} // namespace

FileDeviceMgr *FileDeviceMgr::createInstance(Heap *heap) {
    deleteInstance();
    gMgr = new (gMgrStorage) FileDeviceMgr(heap);
    return gMgr;
}

void FileDeviceMgr::deleteInstance() {
    if (gMgr == nullptr) return;
    gMgr->~FileDeviceMgr();
    gMgr = nullptr;
}

FileDeviceMgr *FileDeviceMgr::instance() { return gMgr; }

// 0x7100e64864: mount the RomFS, then make the main device the mounted default.
FileDeviceMgr::FileDeviceMgr(Heap *heap) : mHeap(heap) {
    mount_(heap);
    void *mem = heap != nullptr ? heap->tryAlloc(sizeof(MainFileDevice), alignof(MainFileDevice)) : nullptr;
    mMainFileDevice = mem != nullptr ? new (mem) MainFileDevice(heap) : new MainFileDevice(heap);
    mount(mMainFileDevice);
    mDefaultFileDevice = mMainFileDevice;
}

FileDeviceMgr::~FileDeviceMgr() {
    if (mMainFileDevice != nullptr) {
        if (mHeap != nullptr && mHeap->isInclude(mMainFileDevice)) {
            mMainFileDevice->~MainFileDevice();
            mHeap->free(mMainFileDevice);
        } else {
            delete mMainFileDevice;
        }
        mMainFileDevice = nullptr;
    }
    while (!mDeviceList.empty()) mDeviceList.root()->next()->erase_();
    unmount_();
}

void FileDeviceMgr::mount_(Heap *heap) {
    std::uint64_t cacheSize = 0;
    if (nn::fs::QueryMountRomCacheSize(&cacheSize) != 0 || cacheSize == 0) return;
    if (heap != nullptr) mRomCache = static_cast<std::uint8_t *>(heap->tryAlloc(cacheSize, 8));
    if (mRomCache == nullptr) return;
    mRomMounted = nn::fs::MountRom("content", mRomCache, cacheSize) == 0;
}

void FileDeviceMgr::unmount_() {
    if (mRomMounted) nn::fs::Unmount("content");
    mRomMounted = false;
    if (mRomCache != nullptr && mHeap != nullptr) mHeap->free(mRomCache);
    mRomCache = nullptr;
}

void FileDeviceMgr::mount(FileDevice *device, const char *name) {
    if (device == nullptr) return;
    if (name != nullptr && name[0] != '\0') device->setDriveName(name);
    if (device->mListNode.isLinked()) device->mListNode.erase_();
    mDeviceList.pushBack(&device->mListNode);
}

void FileDeviceMgr::unmount(FileDevice *device) {
    if (device == nullptr) return;
    if (device->mListNode.isLinked()) device->mListNode.erase_();
    if (device == mDefaultFileDevice) mDefaultFileDevice = nullptr;
}

void FileDeviceMgr::unmount(const char *name) { unmount(findDevice(name)); }

FileDevice *FileDeviceMgr::findDevice(const char *name) const {
    if (name == nullptr) return nullptr;
    List &list = const_cast<List &>(mDeviceList);
    for (ListNode *n = list.root()->next(); n != list.root(); n = n->next()) {
        FileDevice *device = static_cast<FileDevice::DeviceNode *>(n)->device;
        if (std::strcmp(device->getDriveName(), name) == 0) return device;
    }
    return nullptr;
}

// Path::getDriveName / getPathExceptDrive: the drive is what precedes the first ':', and
// the remaining path is what follows "://".
FileDevice *FileDeviceMgr::findDeviceFromPath(const char *path, char *pathNoDrive,
                                              std::size_t cap) const {
    if (path == nullptr) return nullptr;
    FileDevice *device = nullptr;
    const char *colon = std::strchr(path, ':');
    if (colon == nullptr) {
        device = mDefaultFileDevice;
    } else {
        char drive[32];
        const std::size_t len = static_cast<std::size_t>(colon - path);
        if (len >= sizeof(drive)) return nullptr;
        std::memcpy(drive, path, len);
        drive[len] = '\0';
        device = findDevice(drive);
    }
    if (device == nullptr) return nullptr;

    if (pathNoDrive != nullptr && cap > 0) {
        const char *sep = std::strstr(path, "://");
        CopyName(pathNoDrive, cap, sep != nullptr ? sep + 3 : path);
    }
    return device;
}

FileDevice *FileDeviceMgr::tryOpen(FileHandle *handle, const char *path,
                                   FileDevice::FileOpenFlag flag, std::uint32_t divSize) {
    char rest[256];
    FileDevice *device = findDeviceFromPath(path, rest, sizeof(rest));
    return device != nullptr ? device->tryOpen(handle, rest, flag, divSize) : nullptr;
}

FileDevice *FileDeviceMgr::tryOpenDirectory(DirectoryHandle *handle, const char *path) {
    char rest[256];
    FileDevice *device = findDeviceFromPath(path, rest, sizeof(rest));
    if (device == nullptr) return nullptr;
    bool exists = false;
    if (!device->tryIsExistDirectory(&exists, rest) || !exists) return nullptr;
    return device->tryOpenDirectory(handle, rest);
}

std::uint8_t *FileDeviceMgr::tryLoad(FileDevice::LoadArg &arg) {
    char rest[256];
    FileDevice *device = findDeviceFromPath(arg.path, rest, sizeof(rest));
    if (device == nullptr) return nullptr;
    FileDevice::LoadArg inner = arg;
    inner.path = rest;
    std::uint8_t *data = device->tryLoad(inner);
    arg.read_size = inner.read_size;
    arg.roundup_size = inner.roundup_size;
    arg.need_unload = inner.need_unload;
    return data;
}

void FileDeviceMgr::unload(std::uint8_t *data) {
    if (data == nullptr) return;
    if (Heap *heap = HeapMgr::findContainHeap(data)) heap->free(data);
}

} // namespace sead
