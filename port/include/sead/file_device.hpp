// sead file devices — how the game reads its data.
//
//   FileDeviceMgr          ctor 0x7100e64864 (204B): mounts the RomFS as "content"
//                          (QueryMountRomCacheSize + MountRom), creates the MainFileDevice and
//                          mounts it as the default device. createInstance 0x7100e64760.
//   FileDeviceMgr::mount   0x7100e64990
//   MainFileDevice         0x7100e656ac — drive "main", forwards to a NinContentFileDevice
//   NinContentFileDevice   0x7100eb7150 — NinFileDeviceBase("content", "content")
//   NinFileDeviceBase      0x7100eb734c — "path" -> nn::fs "content:/path"
//
// Behaviour from open-ead/sead (no license, written independently). Paths are
// "drive://path"; without a drive they go to the default device ("main", i.e. the RomFS).
//
// Port scope: the READ-ONLY subset game data needs (open, read, seek, size, existence,
// directories, whole-file load). Write/save/remove/flush are not ported: the RomFS is
// read-only and save data goes through a different device. Strings are const char*.
#pragma once

#include "nn/fs.hpp"
#include "sead/i_disposer.hpp"
#include "sead/list_node.hpp"

#include <cstddef>
#include <cstdint>

namespace sead {

class Heap;
class FileDevice;

class HandleBase {
public:
    HandleBase() = default;
    HandleBase(const HandleBase &) = delete;
    HandleBase &operator=(const HandleBase &) = delete;
    virtual ~HandleBase() = default;

    FileDevice *getDevice() const { return mDevice; }
    FileDevice *getOriginalDevice() const { return mOriginalDevice; }
    bool isOpened() const { return mOriginalDevice != nullptr; }

protected:
    friend class FileDevice;

    FileDevice *mDevice = nullptr;         // the device that actually opened it
    FileDevice *mOriginalDevice = nullptr; // the device it was opened through
    alignas(8) unsigned char mHandleBuffer[32] = {}; // device-specific state
};

class FileHandle;
class DirectoryHandle;

struct DirectoryEntry {
    char name[256] = {};
    bool is_directory = false;
};

class FileDevice : public IDisposer {
public:
    enum FileOpenFlag {
        cFileOpenFlag_ReadOnly = 0,
        cFileOpenFlag_WriteOnly = 1,
        cFileOpenFlag_ReadWrite = 2,
        cFileOpenFlag_Create = 3,
    };
    enum SeekOrigin { cSeekOrigin_Begin = 0, cSeekOrigin_Current = 1, cSeekOrigin_End = 2 };

    struct LoadArg {
        const char *path = nullptr;
        std::uint8_t *buffer = nullptr;     // caller's buffer, or null to allocate
        std::uint32_t buffer_size = 0;
        Heap *heap = nullptr;               // null: the current heap
        std::int32_t alignment = 0;         // of the allocation (min cBufferMinAlignment)
        std::int32_t buffer_size_alignment = 0; // the size is rounded up to this
        std::uint32_t div_size = 0;         // read in chunks of this size (0: one read)
        bool assert_on_alloc_fail = true;
        bool check_read_entire_file = true;
        std::uint32_t read_size = 0;        // out
        std::uint32_t roundup_size = 0;     // out: the allocated size
        bool need_unload = false;           // out: true when the buffer was allocated here
    };

    static constexpr std::int32_t cBufferMinAlignment = 0x20; // Switch value

    explicit FileDevice(const char *driveName);
    ~FileDevice() override;

    FileDevice(const FileDevice &) = delete;
    FileDevice &operator=(const FileDevice &) = delete;

    const char *getDriveName() const { return mDriveName; }
    void setDriveName(const char *name);

    bool isAvailable() const { return doIsAvailable_(); }

    // Loads a whole file. Returns the data (and fills read_size/roundup_size/need_unload)
    // or null. Free an allocated result with FileDeviceMgr::unload.
    std::uint8_t *tryLoad(LoadArg &arg);

    FileDevice *tryOpen(FileHandle *handle, const char *path, FileOpenFlag flag,
                        std::uint32_t divSize = 0);
    bool tryClose(FileHandle *handle);
    bool tryRead(std::uint32_t *bytesRead, FileHandle *handle, std::uint8_t *out,
                 std::uint32_t bytesToRead);
    bool trySeek(FileHandle *handle, std::int32_t offset, SeekOrigin origin);
    bool tryGetCurrentSeekPos(std::uint32_t *pos, FileHandle *handle);
    bool tryGetFileSize(std::uint32_t *size, const char *path);
    bool tryGetFileSize(std::uint32_t *size, FileHandle *handle);
    bool tryIsExistFile(bool *exists, const char *path);
    bool tryIsExistDirectory(bool *exists, const char *path);

    FileDevice *tryOpenDirectory(DirectoryHandle *handle, const char *path);
    bool tryCloseDirectory(DirectoryHandle *handle);
    bool tryReadDirectory(std::uint32_t *entriesRead, DirectoryHandle *handle,
                          DirectoryEntry *entries, std::uint32_t count);

protected:
    friend class FileDeviceMgr;
    friend class MainFileDevice;

    virtual bool doIsAvailable_() const = 0;
    virtual std::uint8_t *doLoad_(LoadArg &arg);
    virtual FileDevice *doOpen_(FileHandle *handle, const char *path, FileOpenFlag flag) = 0;
    virtual bool doClose_(FileHandle *handle) = 0;
    virtual bool doRead_(std::uint32_t *bytesRead, FileHandle *handle, std::uint8_t *out,
                         std::uint32_t bytesToRead) = 0;
    virtual bool doSeek_(FileHandle *handle, std::int32_t offset, SeekOrigin origin) = 0;
    virtual bool doGetCurrentSeekPos_(std::uint32_t *pos, FileHandle *handle) = 0;
    virtual bool doGetFileSize_(std::uint32_t *size, const char *path) = 0;
    virtual bool doGetFileSize_(std::uint32_t *size, FileHandle *handle) = 0;
    virtual bool doIsExistFile_(bool *exists, const char *path) = 0;
    virtual bool doIsExistDirectory_(bool *exists, const char *path) = 0;
    virtual FileDevice *doOpenDirectory_(DirectoryHandle *handle, const char *path) = 0;
    virtual bool doCloseDirectory_(DirectoryHandle *handle) = 0;
    virtual bool doReadDirectory_(std::uint32_t *entriesRead, DirectoryHandle *handle,
                                  DirectoryEntry *entries, std::uint32_t count) = 0;
    virtual bool isMatchDevice_(const HandleBase *handle) const { return handle->mDevice == this; }

    static unsigned char *handleBuffer_(HandleBase *handle) { return handle->mHandleBuffer; }

private:
    // This device's link in FileDeviceMgr's device list.
    struct DeviceNode : ListNode {
        FileDevice *device = nullptr;
    };

    char mDriveName[32] = {};
    DeviceNode mListNode;
};

class FileHandle : public HandleBase {
public:
    ~FileHandle() override;

    bool tryClose();
    bool tryRead(std::uint32_t *bytesRead, std::uint8_t *out, std::uint32_t size);
    bool trySeek(std::int32_t offset, FileDevice::SeekOrigin origin);
    bool tryGetCurrentSeekPos(std::uint32_t *pos);
    bool tryGetFileSize(std::uint32_t *size);
    std::uint32_t getDivSize() const { return mDivSize; }

private:
    friend class FileDevice;
    std::uint32_t mDivSize = 0;
};

class DirectoryHandle : public HandleBase {
public:
    ~DirectoryHandle() override;
    bool tryClose();
    bool tryRead(std::uint32_t *entriesRead, DirectoryEntry *entries, std::uint32_t count);
};

// A device over an nn::fs mount: "path" becomes "<mountPoint>:/path".
class NinFileDeviceBase : public FileDevice {
public:
    NinFileDeviceBase(const char *driveName, const char *mountPoint);

protected:
    bool doIsAvailable_() const override { return true; }
    FileDevice *doOpen_(FileHandle *handle, const char *path, FileOpenFlag flag) override;
    bool doClose_(FileHandle *handle) override;
    bool doRead_(std::uint32_t *bytesRead, FileHandle *handle, std::uint8_t *out,
                 std::uint32_t bytesToRead) override;
    bool doSeek_(FileHandle *handle, std::int32_t offset, SeekOrigin origin) override;
    bool doGetCurrentSeekPos_(std::uint32_t *pos, FileHandle *handle) override;
    bool doGetFileSize_(std::uint32_t *size, const char *path) override;
    bool doGetFileSize_(std::uint32_t *size, FileHandle *handle) override;
    bool doIsExistFile_(bool *exists, const char *path) override;
    bool doIsExistDirectory_(bool *exists, const char *path) override;
    FileDevice *doOpenDirectory_(DirectoryHandle *handle, const char *path) override;
    bool doCloseDirectory_(DirectoryHandle *handle) override;
    bool doReadDirectory_(std::uint32_t *entriesRead, DirectoryHandle *handle,
                          DirectoryEntry *entries, std::uint32_t count) override;

private:
    // False when the path does not fit; nn::fs rejects ".." itself.
    bool formatPathForFS_(char *out, std::size_t cap, const char *path) const;

    char mMountPoint[32] = {};
    int mLastError = 0;
};

// The RomFS device: drive "content" over the nn::fs mount "content".
class NinContentFileDevice : public NinFileDeviceBase {
public:
    NinContentFileDevice() : NinFileDeviceBase("content", "content") {}
};

// The default device ("main"), forwarding to a NinContentFileDevice it owns.
class MainFileDevice : public FileDevice {
public:
    explicit MainFileDevice(Heap *heap);
    ~MainFileDevice() override;

protected:
    bool doIsAvailable_() const override { return mFileDevice->doIsAvailable_(); }
    std::uint8_t *doLoad_(LoadArg &arg) override { return mFileDevice->doLoad_(arg); }
    FileDevice *doOpen_(FileHandle *h, const char *p, FileOpenFlag f) override { return mFileDevice->doOpen_(h, p, f); }
    bool doClose_(FileHandle *h) override { return mFileDevice->doClose_(h); }
    bool doRead_(std::uint32_t *r, FileHandle *h, std::uint8_t *o, std::uint32_t n) override { return mFileDevice->doRead_(r, h, o, n); }
    bool doSeek_(FileHandle *h, std::int32_t o, SeekOrigin s) override { return mFileDevice->doSeek_(h, o, s); }
    bool doGetCurrentSeekPos_(std::uint32_t *p, FileHandle *h) override { return mFileDevice->doGetCurrentSeekPos_(p, h); }
    bool doGetFileSize_(std::uint32_t *s, const char *p) override { return mFileDevice->doGetFileSize_(s, p); }
    bool doGetFileSize_(std::uint32_t *s, FileHandle *h) override { return mFileDevice->doGetFileSize_(s, h); }
    bool doIsExistFile_(bool *e, const char *p) override { return mFileDevice->doIsExistFile_(e, p); }
    bool doIsExistDirectory_(bool *e, const char *p) override { return mFileDevice->doIsExistDirectory_(e, p); }
    FileDevice *doOpenDirectory_(DirectoryHandle *h, const char *p) override { return mFileDevice->doOpenDirectory_(h, p); }
    bool doCloseDirectory_(DirectoryHandle *h) override { return mFileDevice->doCloseDirectory_(h); }
    bool doReadDirectory_(std::uint32_t *r, DirectoryHandle *h, DirectoryEntry *e, std::uint32_t n) override { return mFileDevice->doReadDirectory_(r, h, e, n); }
    bool isMatchDevice_(const HandleBase *h) const override { return mFileDevice->isMatchDevice_(h); }

private:
    Heap *mHeap;
    FileDevice *mFileDevice;
};

class FileDeviceMgr {
public:
    // Replaces any previous instance (unmounting it first). The instance lives in static
    // storage; its devices and the RomFS cache are allocated from `heap`.
    static FileDeviceMgr *createInstance(Heap *heap);
    static void deleteInstance();
    static FileDeviceMgr *instance();

    Heap *heap() const { return mHeap; }

    // Adds `device` to the list, renaming it first when `name` is given.
    void mount(FileDevice *device, const char *name = nullptr);
    void unmount(FileDevice *device);
    void unmount(const char *name);

    FileDevice *findDevice(const char *name) const;
    // Device for "drive://rest" (or the default device when there is no drive); writes
    // the part after "://" to pathNoDrive.
    FileDevice *findDeviceFromPath(const char *path, char *pathNoDrive, std::size_t cap) const;

    FileDevice *tryOpen(FileHandle *handle, const char *path, FileDevice::FileOpenFlag flag,
                        std::uint32_t divSize = 0);
    FileDevice *tryOpenDirectory(DirectoryHandle *handle, const char *path);
    std::uint8_t *tryLoad(FileDevice::LoadArg &arg);
    // Frees data tryLoad allocated (need_unload == true).
    void unload(std::uint8_t *data);

    FileDevice *getDefaultFileDevice() const { return mDefaultFileDevice; }
    FileDevice *getMainFileDevice() const { return mMainFileDevice; }
    std::size_t deviceCount() const { return mDeviceList.size(); }

private:
    explicit FileDeviceMgr(Heap *heap);
    ~FileDeviceMgr();

    void mount_(Heap *heap);
    void unmount_();

    Heap *mHeap = nullptr;
    List mDeviceList;
    FileDevice *mDefaultFileDevice = nullptr;
    MainFileDevice *mMainFileDevice = nullptr;
    std::uint8_t *mRomCache = nullptr;
    bool mRomMounted = false;
};

} // namespace sead
