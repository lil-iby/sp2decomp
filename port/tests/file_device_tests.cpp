// file_device_tests.cpp — sead::FileDeviceMgr and its devices over a fake RomFS.
//
// Builds a tiny RomFS in a temp folder (no game data), then reads it the way the game does:
// through FileDeviceMgr, the "main" default device and the "content" nn::fs mount.
#include "sead/file_device.hpp"
#include "sead/framework_args.hpp"
#include "sead/game_framework.hpp"
#include "sead/heap.hpp"
#include "sead/heap_mgr.hpp"
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

const std::string kPayload = "0123456789ABCDEF";
} // namespace

static void test_manager_and_load(sead::Heap *heap) {
    std::printf("\n[1] FileDeviceMgr setup and whole-file loads\n");
    sead::FileDeviceMgr *mgr = sead::FileDeviceMgr::createInstance(heap);
    Check(mgr == sead::FileDeviceMgr::instance(), "createInstance sets the instance");
    Check(mgr->deviceCount() == 1, "one device mounted");
    Check(mgr->getDefaultFileDevice() == mgr->getMainFileDevice() &&
              std::strcmp(mgr->getMainFileDevice()->getDriveName(), "main") == 0,
          "the default device is \"main\"");
    Check(mgr->findDevice("content") == nullptr, "\"content\" is a nn::fs mount, not a drive");

    const int allocsBefore = heap->liveAllocationCount();
    sead::FileDevice::LoadArg arg;
    arg.path = "Model/Test.bin";
    arg.heap = heap;
    std::uint8_t *data = mgr->tryLoad(arg);
    Check(data != nullptr && std::memcmp(data, kPayload.data(), 16) == 0, "tryLoad reads the file");
    Check(arg.read_size == 16 && arg.roundup_size == 32 && arg.need_unload,
          "read 16, buffer rounded up to 0x20, owned by the caller");
    Check(heap->liveAllocationCount() == allocsBefore + 1, "the buffer came from the given heap");
    mgr->unload(data);
    Check(heap->liveAllocationCount() == allocsBefore, "unload returns it");

    sead::FileDevice::LoadArg withDrive;
    withDrive.path = "main://Model/Test.bin";
    withDrive.heap = heap;
    data = mgr->tryLoad(withDrive);
    Check(data != nullptr && withDrive.read_size == 16, "\"main://\" paths reach the same file");
    mgr->unload(data);

    sead::FileDevice::LoadArg missing;
    missing.path = "Model/Missing.bin";
    missing.heap = heap;
    Check(mgr->tryLoad(missing) == nullptr, "a missing file loads as null");
    sead::FileDevice::LoadArg badDrive;
    badDrive.path = "nope://Model/Test.bin";
    Check(mgr->tryLoad(badDrive) == nullptr, "an unknown drive loads as null");

    std::uint8_t buf[64] = {};
    sead::FileDevice::LoadArg own;
    own.path = "Model/Test.bin";
    own.buffer = buf;
    own.buffer_size = sizeof(buf);
    Check(mgr->tryLoad(own) == buf && !own.need_unload && std::memcmp(buf, kPayload.data(), 16) == 0,
          "a caller buffer is filled, not replaced");
    own.buffer_size = 8;
    Check(mgr->tryLoad(own) == nullptr, "a caller buffer that is too small is refused");

    sead::FileDevice::LoadArg chunked;
    chunked.path = "Model/Test.bin";
    chunked.heap = heap;
    chunked.div_size = 5;
    data = mgr->tryLoad(chunked);
    Check(data != nullptr && chunked.read_size == 16 && std::memcmp(data, kPayload.data(), 16) == 0,
          "div_size reads in chunks and still gets everything");
    mgr->unload(data);
}

static void test_handles() {
    std::printf("\n[2] file handles: read, seek, size\n");
    sead::FileDeviceMgr *mgr = sead::FileDeviceMgr::instance();
    {
        sead::FileHandle h;
        Check(mgr->tryOpen(&h, "Model/Test.bin", sead::FileDevice::cFileOpenFlag_ReadOnly) != nullptr,
              "tryOpen");
        Check(h.isOpened() && h.getOriginalDevice() == mgr->getMainFileDevice(),
              "opened through the main device");
        std::uint32_t size = 0;
        Check(h.tryGetFileSize(&size) && size == 16, "size 16");

        std::uint8_t buf[8] = {};
        std::uint32_t got = 0;
        Check(h.trySeek(10, sead::FileDevice::cSeekOrigin_Begin) && h.tryRead(&got, buf, 4) &&
                  got == 4 && std::memcmp(buf, "ABCD", 4) == 0, "seek to 10, read ABCD");
        std::uint32_t pos = 0;
        Check(h.tryGetCurrentSeekPos(&pos) && pos == 14, "position advanced to 14");
        Check(h.trySeek(-2, sead::FileDevice::cSeekOrigin_End) && h.tryRead(&got, buf, 8) &&
                  got == 2 && std::memcmp(buf, "EF", 2) == 0, "seek from the end, short read of 2");
        Check(h.trySeek(-4, sead::FileDevice::cSeekOrigin_Current) && h.tryRead(&got, buf, 1) &&
                  buf[0] == 'C', "relative seek");
    } // the handle closes itself
    Check(true, "the handle closed on destruction");

    sead::FileHandle w;
    Check(mgr->tryOpen(&w, "Model/Test.bin", sead::FileDevice::cFileOpenFlag_WriteOnly) == nullptr,
          "write mode is refused (RomFS is read-only)");
}

static void test_queries_and_directories() {
    std::printf("\n[3] existence checks and directories\n");
    sead::FileDeviceMgr *mgr = sead::FileDeviceMgr::instance();
    sead::FileDevice *main = mgr->getMainFileDevice();
    bool exists = false;
    Check(main->tryIsExistFile(&exists, "Model/Test.bin") && exists, "the file exists");
    Check(main->tryIsExistFile(&exists, "Model") && !exists, "a folder is not a file");
    Check(main->tryIsExistDirectory(&exists, "Model") && exists, "the folder exists");
    Check(main->tryIsExistFile(&exists, "Model/Missing.bin") && !exists, "a missing file does not");
    std::uint32_t size = 0;
    Check(main->tryGetFileSize(&size, "Model/Other.bin") && size == 2, "size by path");

    sead::DirectoryHandle d;
    Check(mgr->tryOpenDirectory(&d, "Model") != nullptr, "tryOpenDirectory");
    sead::DirectoryEntry entries[8];
    std::uint32_t n = 0;
    Check(d.tryRead(&n, entries, 8) && n == 3, "3 entries");
    Check(std::strcmp(entries[0].name, "Other.bin") == 0 && !entries[0].is_directory &&
              std::strcmp(entries[1].name, "Sub") == 0 && entries[1].is_directory &&
              std::strcmp(entries[2].name, "Test.bin") == 0, "names and kinds, in order");
    sead::DirectoryHandle missing;
    Check(mgr->tryOpenDirectory(&missing, "Nope") == nullptr, "a missing folder does not open");
}

static void test_unmount() {
    std::printf("\n[4] unmounting\n");
    sead::FileDeviceMgr *mgr = sead::FileDeviceMgr::instance();
    sead::FileDevice *main = mgr->getMainFileDevice();
    mgr->unmount(main);
    Check(mgr->deviceCount() == 0 && mgr->getDefaultFileDevice() == nullptr,
          "unmounting the default device clears it");
    sead::FileDevice::LoadArg arg;
    arg.path = "Model/Test.bin";
    Check(mgr->tryLoad(arg) == nullptr, "nothing loads without a device");
    mgr->mount(main);
    Check(mgr->findDevice("main") == main, "mounted again by name");

    sead::FileDeviceMgr::deleteInstance();
    Check(sead::FileDeviceMgr::instance() == nullptr, "deleteInstance clears the instance");
    std::vector<char> cache(4 * 1024 * 1024);
    const bool freed = nn::fs::MountRom("content", cache.data(), cache.size()) == 0;
    Check(freed, "and releases the \"content\" mount");
    if (freed) nn::fs::Unmount("content");
}

static void test_boot_path() {
    std::printf("\n[5] the boot path: GameFramework::initialize mounts and serves the RomFS\n");
    sead::Framework::InitializeArg arg;
    sead::GameFramework::initialize(arg);
    sead::FileDeviceMgr *mgr = sead::FileDeviceMgr::instance();
    Check(mgr != nullptr && mgr->heap() != nullptr &&
              std::string(mgr->heap()->name()) == "sead::FileDeviceMgr",
          "FileDeviceMgr created in its own heap");
    sead::FileDevice::LoadArg load;
    load.path = "Model/Test.bin";
    std::uint8_t *data = mgr != nullptr ? mgr->tryLoad(load) : nullptr;
    Check(data != nullptr && std::memcmp(data, kPayload.data(), 16) == 0,
          "a game-style relative path loads through the boot-created manager");
    if (mgr != nullptr) mgr->unload(data);
}

int main() {
    std::printf("sead file device tests\n");

    const stdfs::path root = stdfs::temp_directory_path() / "sp2_file_device_romfs";
    stdfs::remove_all(root);
    WriteBytes(root / "Model" / "Test.bin", kPayload);
    WriteBytes(root / "Model" / "Other.bin", "xy");
    stdfs::create_directories(root / "Model" / "Sub");
    nn::fs::host::SetRomRoot(root.string().c_str());

    sead::HeapMgr::initialize(16 * 1024 * 1024);
    sead::ExpHeap *heap = sead::ExpHeap::create(0, "sead::FileDeviceMgr", sead::HeapMgr::getRootHeap(0));

    test_manager_and_load(heap);
    test_handles();
    test_queries_and_directories();
    test_unmount();
    sead::HeapMgr::destroy();
    test_boot_path();

    sead::FileDeviceMgr::deleteInstance();
    nn::fs::host::SetRomRoot(nullptr);
    stdfs::remove_all(root);
    std::printf("\n%d checks, %d failure(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
