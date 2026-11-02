// The reconstructed boot sequence.
//
// Order and arguments are taken from the decompiled body of Lp::Sys::SysInit::Init
// (0x71011f5b20). Each step either calls a reconstructed engine function or declares the
// dependency missing via LP_ENGINE_REQUIRE, which throws and stops the run. Nothing is
// silently stubbed.
#include "lp/boot_sequence.hpp"

#include "lp/recon.hpp"
#include "lp/utl.hpp"
#include "lp/root_task.hpp"
#include "sead/delegate_event.hpp"
#include "sead/framework_args.hpp"
#include "sead/game_framework_nx.hpp"
#include "lp/heap_group.hpp"
#include "sead/task_base.hpp"
#include "sead/heap_mgr.hpp"
#include "nn/os.hpp"

#include <cstdio>

namespace lp::recon {

std::string BootRunResult::summary() const {
    std::string s;
    s += "reconstructed steps completed: " + std::to_string(completed.size()) + "\n";
    for (std::size_t i = 0; i < completed.size(); ++i) {
        s += "  [" + std::to_string(i + 1) + "] " + completed[i] + "\n";
    }
    if (reachedEnd) {
        s += "run reached the end of the reconstructed sequence\n";
    } else {
        char buf[48];
        std::snprintf(buf, sizeof(buf), " (0x%llx)",
                      static_cast<unsigned long long>(stoppedAtAddress));
        s += "STOPPED AT: " + stoppedAt + (stoppedAtAddress ? buf : "") + "\n";
    }
    return s;
}

namespace {

// Addresses of the boot root's 13 direct engine dependencies, from
// docs/ENGINE_RECONSTRUCTION.md. Kept here because the sequence reports them.
constexpr std::uintptr_t kDumpDateTime            = 0x71012cdbf4;
constexpr std::uintptr_t kAssertRegisterCallback  = 0x7101397fac;
constexpr std::uintptr_t kInitializeArgCtor       = 0x7100e69894;
constexpr std::uintptr_t kGameFrameworkInitialize = 0x7100e6724c;
constexpr std::uintptr_t kDumpMemoryInfo          = 0x71012cdccc;
constexpr std::uintptr_t kForegroundBucketInit    = 0x710139fb3c;
constexpr std::uintptr_t kHeapGroupCreateInstance = 0x7100ea1f54;
constexpr std::uintptr_t kHeapGroupCreateHeap     = 0x71011f5130;
constexpr std::uintptr_t kFrameworkCtor           = 0x7100e696a4;
constexpr std::uintptr_t kInitializeGraphicsSystem= 0x7100e6738c;
constexpr std::uintptr_t kMgrTaskArgCtor          = 0x7100e6b7bc;
constexpr std::uintptr_t kRunArgCtor              = 0x7100e698a0;
constexpr std::uintptr_t kCreateParallelInitThread = 0x71011fcadc;
constexpr std::uintptr_t kRunParallelInitThread    = 0x71011fcb44;
constexpr std::uintptr_t kFrameworkRun             = 0x7100e69908;
constexpr std::uintptr_t kWaitDoneParallelInit     = 0x71011fcba4;

// Tracks what this run has completed, and catches the loud failure so the caller gets a
// precise "the port ends here" report instead of an abort.
struct Runner {
    BootRunResult result;

    void step(const char *name, void (*fn)()) {
        fn();
        result.completed.push_back(name);
    }

    void missing(const char *name, std::uintptr_t addr) {
        // Either this throws (dependency absent) or the dependency is present.
        LP_ENGINE_REQUIRE(name, addr);
    }

    void run(void (*body)(Runner &)) {
        try {
            body(*this);
            result.reachedEnd = true;
        } catch (const MissingEngineDependency &e) {
            result.stoppedAt = e.name();
            result.stoppedAtAddress = e.address();
        }
    }
};

void SequenceBody(Runner &r) {
    // --- step 1: dumpDateTime(" Application Start Time") ---------------------------
    // The literal is the one at the call site in the boot root's body.
    r.step("Lp::Utl::dumpDateTime(\" Application Start Time\")", [] {
        Lp::Utl::dumpDateTime(" Application Start Time");
    });

    // --- step 2: GetSystemTick -> sRunStartTime ------------------------------------
    // Platform-layer call; already implemented, so it runs rather than being declared.
    r.step("nn::os::GetSystemTick -> sRunStartTime", [] {
        volatile std::uint64_t recorded = nn::os::GetSystemTick();
        (void)recorded;
    });

    // --- step 3: sead::AssertConfig::registerCallback ------------------------------
    // Now ported (port/src/delegate_event.cpp) against sead's real DelegateEvent, so this
    // RUNS rather than stopping the sequence.
    r.step("sead::AssertConfig::registerCallback", [] {
        static sead::DelegateEventSlot sAssertSlot;
        sead::AssertConfig::registerCallback(sAssertSlot);
    });

    // --- step 4: sead::Framework::InitializeArg + Framework::initialize -------------
    // Both ported: the constructor (framework_args.cpp) and the consumer
    // (framework.cpp), which reaches the ported HeapMgr.
    r.step("sead::Framework::InitializeArg::InitializeArg", [] {
        static sead::Framework::InitializeArg sArg;
        (void)sArg;
    });
    r.step("sead::Framework::initialize", [] {
        sead::Framework::InitializeArg arg;
        sead::Framework::initialize(arg); // reaches HeapMgr + ExpHeap
    });

    // --- step 5: sead::GameFrameworkNx::initialize ---------------------------------
    // Ported (game_framework_nx.cpp + game_framework.cpp), and it reaches the heap layer
    // (heap.cpp / heap_mgr.cpp) -- so the runner now executes real heap construction.
    r.step("sead::GameFrameworkNx::initialize", [] {
        sead::Framework::InitializeArg arg;
        sead::GameFrameworkNx::initialize(arg);
    });

    // --- step 6: dumpMemoryInfo("Application Start") -------------------------------
    // NOTE ORDERING: this is ported, but it sits AFTER two unported dependencies in the
    // boot root's order. With fail-loudly the run stops at step 3 and never reaches it,
    // which is the correct behaviour -- but it means this step is unreachable until steps
    // 3-5 are ported. Recorded so the gap is visible rather than surprising.
    r.step("Lp::Utl::dumpMemoryInfo(\"Application Start\")", [] {
        Lp::Utl::dumpMemoryInfo("Application Start");
    });

    // --- step 7 onward: remaining boot-root dependencies ---------------------------
    // Ported: 0x710139fb3c is a 4-byte no-op, so running it IS faithful.
    r.step("sead::ForegroundBucketMgr::initialize", [] {
        sead::ForegroundBucketMgr::initialize(sead::HeapMgr::getRootHeap(0), 0, 0, false);
    });
    // Ported: 0x71011f4ffc, 160 bytes -- allocates its 0x160 singleton from the root heap.
    r.step("Lp::Sys::HeapGroup::createInstance", [] {
        Lp::Sys::HeapGroup::createInstance(sead::HeapMgr::getRootHeap(0));
    });
    // Ported: 0x71011f5130, 508 bytes -- creates six named heaps as children of the parent.
    r.step("Lp::Sys::HeapGroup::createHeap", [] {
        Lp::Sys::HeapSizes sizes;
        const char *names[5] = {"cGroup0", "cGroup1", "cGroup2", "cGroup3", "cGroup4"};
        if (auto *hg = Lp::Sys::HeapGroup::instance()) {
            hg->createHeap(sead::HeapMgr::getRootHeap(0), sizes, names, "cOthers");
        }
    });
    // Ported: 0x7100e696a4, 60 bytes -- the boot root constructs one of these.
    r.step("sead::Framework::Framework", [] {
        static sead::Framework sFramework;
        (void)sFramework;
    });
    // Ported: 0x7100e6738c, ~400 bytes. Six nn::vi calls + nvn::LoadCProcs.
    r.step("sead::GameFrameworkNx::initializeGraphicsSystem", [] {
        sead::GameFrameworkNx::initializeGraphicsSystem();
    });
    // Ported: 0x7100e6b7bc, 308 bytes. Reads the heap layer's root-heap count for its flags.
    r.step("sead::TaskBase::MgrTaskArg::MgrTaskArg", [] {
        sead::TaskClassID id;
        sead::MgrTaskArg arg(id);
        (void)arg;
    });
    // Ported: 0x7100e698a0, 20 bytes. Stores 0xffffffff00000000 verbatim (see framework_args.cpp).
    r.step("sead::Framework::RunArg::RunArg", [] {
        static sead::Framework::RunArg sRunArg;
        (void)sRunArg;
    });

    // --- steps 14–17: RootTask parallel-init + Framework::run -------------------
    // In SysInit::Init the real order is:
    //   createParallelInitThread → runParallelInitThread → Framework::run (game loop)
    //   → waitDoneParallelInitThread
    // A static pointer threads the RootTask instance across the three separate steps.
    static Lp::Sys::RootTask *sRootTask = nullptr;

    // Ported: 0x71011fcadc, 104 bytes. Launches the background worker thread.
    // sRootTask is a static local (static storage) — accessible inside lambdas
    // without capture.
    r.step("Lp::Sys::RootTask::createParallelInitThread", [] {
        static Lp::Sys::RootTask sStorage;
        sRootTask = &sStorage;
        sRootTask->createParallelInitThread();
    });

    // Ported: 0x71011fcb44, 96 bytes. Enqueues the init task and fires the worker.
    r.step("Lp::Sys::RootTask::runParallelInitThread", [] {
        sRootTask->runParallelInitThread(0);
    });

    // Ported: 0x7100e69908. The main game loop; bounded to 1 frame so the boot
    // sequence test does not hang. In the real binary this runs until the game exits.
    r.step("sead::Framework::run (1 frame)", [] {
        static sead::Framework sFw;
        sead::Framework::setFrameLimit(1);
        sead::Framework::RunArg runArg;
        sFw.run(runArg);
        sead::Framework::setFrameLimit(-1);
    });

    // Ported: 0x71011fcba4, 56 bytes. Spin-waits until the parallel-init batch is done.
    r.step("Lp::Sys::RootTask::waitDoneParallelInitThread", [] {
        sRootTask->waitDoneParallelInitThread();
    });
}

} // namespace

BootRunResult runBootSequence() {
    Runner r;
    r.run(SequenceBody);
    return r.result;
}

} // namespace lp::recon
