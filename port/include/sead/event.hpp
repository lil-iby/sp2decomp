// sead::Event — a signal one thread waits on and another sets, over nn::os::LightEvent.
//
// 0x7100e98d5c is called from Lp::Sys::CalcInGSysCalc (boot-callgraph.txt). The binary
// gives no body data for it in ENGINE_REFERENCE.md, so the interface and behaviour come
// from open-ead/sead (a public reimplementation, no license, so written independently):
// Event is an IDisposer holding an nn::os::LightEventType, and every operation forwards
// to the matching LightEvent call.
//
// CalcInGSysCalc's shape (erase from manager, then Event, then a tail call into
// IDisposer) reads like a destructor chain, so the call there is probably ~Event.
#pragma once

#include "nn/os.hpp"
#include "sead/i_disposer.hpp"
#include "sead/tick_span.hpp"

namespace sead {

class Event : public IDisposer {
public:
    Event();
    explicit Event(bool manualReset);
    explicit Event(Heap *disposerHeap);
    Event(Heap *disposerHeap, bool manualReset);
    Event(Heap *disposerHeap, HeapNullOption option);
    Event(Heap *disposerHeap, HeapNullOption option, bool manualReset);
    ~Event() override;

    Event(const Event &) = delete;
    Event &operator=(const Event &) = delete;

    // The constructors without a bool leave the event uninitialised; call this before use.
    void initialize(bool manualReset);
    void wait();
    // Returns true if signalled before `duration` elapsed.
    bool wait(TickSpan duration);
    void setSignal();
    void resetSignal();

private:
    nn::os::LightEventType mEventInner;
};

} // namespace sead
