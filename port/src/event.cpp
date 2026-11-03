// sead::Event — see port/include/sead/event.hpp.
#include "sead/event.hpp"

namespace sead {

Event::Event() : IDisposer() {}

Event::Event(bool manualReset) : Event() { initialize(manualReset); }

Event::Event(Heap *disposerHeap) : Event(disposerHeap, HeapNullOption::UseSpecifiedOrContainHeap) {}

Event::Event(Heap *disposerHeap, bool manualReset) : Event(disposerHeap) {
    initialize(manualReset);
}

Event::Event(Heap *disposerHeap, HeapNullOption option) : IDisposer(disposerHeap, option) {}

Event::Event(Heap *disposerHeap, HeapNullOption option, bool manualReset)
    : Event(disposerHeap, option) {
    initialize(manualReset);
}

Event::~Event() { nn::os::FinalizeLightEvent(&mEventInner); }

void Event::initialize(bool manualReset) {
    nn::os::InitializeLightEvent(&mEventInner, false,
                                 manualReset ? nn::os::EventClearMode::ManualClear
                                             : nn::os::EventClearMode::AutoClear);
}

void Event::wait() { nn::os::WaitLightEvent(&mEventInner); }

bool Event::wait(TickSpan duration) {
    return nn::os::TimedWaitLightEvent(&mEventInner,
                                       nn::os::ConvertToTimeSpan(duration.toS64()));
}

void Event::setSignal() { nn::os::SignalLightEvent(&mEventInner); }

void Event::resetSignal() { nn::os::ClearLightEvent(&mEventInner); }

} // namespace sead
