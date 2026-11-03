// sead::DelegateEvent / Slot — ported against the open-ead/sead reference.
//
// This REPLACES an earlier attempt that invented a `RefCounted`/`DelegateEventConnection`
// pair. That model does not exist: sead's mechanism has NO reference count, and the
// pointer at slot+0x38 is the DELEGATE. See port/attempted/LAYOUT_ANSWERED.md.
//
// Reference (copies in reference/, reading reference only — sead ships no licence file):
//
//   template <typename T> class DelegateEvent {
//     class Slot : public IDisposer {
//       SlotListNode mNode{this};                  // TListNode<Slot*>
//       IDelegate1<T>* mDelegatePtr = nullptr;
//       StorageFor<AnyDelegate1<T>, true> mDelegate;
//       bool mConnectedToDelegateEvent = false;
//     };
//     void connect(Slot& slot) {
//       slot.release();
//       mList.pushBack(&slot.mNode);
//       slot.mConnectedToDelegateEvent = true;
//     }
//     void release() { if (mConnectedToDelegateEvent) { mNode.erase(); mConnectedToDelegateEvent = false; } }
//     SlotList mList;   // TList<Slot*>
//   };
//
// Offsets the decompiled registerCallback (0x7101397fac) reaches, all confirmed by
// port/tests/layout_probe.cpp: node at +0x20, delegate at +0x38, connected flag at +0x68;
// for the event, list head at +0x08 and count at +0x18.
#pragma once

#include "sead/list_node.hpp"

#include <cstddef>
#include <cstdint>

namespace sead {

// The delegate the slot invokes. Only its address is significant to registerCallback; the
// decompiled body writes into it (`*(int *)(delegate + 0x10)`) when releasing.
//
// Deliberately NOT polymorphic: giving it a virtual destructor would place a vtable at
// +0x00 and shift mRefAt0x10, which the static_assert rejects. The decompiled code only
// ever reaches +0x10 through this pointer, so a plain struct is the faithful model.
struct InvokableDelegate {
    unsigned char mPad0[0x10] = {}; // places mRefAt0x10 at +0x10
    int mRefAt0x10 = 0;
};

// A subscriber slot. Derived from IDisposer in sead; only the members registerCallback
// touches are modelled, at their observed offsets.
class DelegateEventSlot {
public:
    DelegateEventSlot() = default;

    bool isConnected() const { return mConnectedToDelegateEvent != 0; }
    ListNode *node() { return &mNode; }
    InvokableDelegate *delegate() const { return mDelegatePtr; }
    void setDelegate(InvokableDelegate *d) { mDelegatePtr = d; }
    void setConnected(bool v) { mConnectedToDelegateEvent = v ? 1 : 0; }

    // sead::DelegateEvent::Slot::release() — the conditional unlink.
    void release() {
        if (mConnectedToDelegateEvent != 0) {
            mNode.erase_();
            mConnectedToDelegateEvent = 0;
        }
    }

    // Offsets, enforced by static_assert in the implementation:
    //   +0x00 IDisposer base (vtable + gadget)
    //   +0x20 SlotListNode mNode{this}
    //   +0x38 IDelegate1<T>* mDelegatePtr
    //   +0x68 bool mConnectedToDelegateEvent
    unsigned char mBase[0x20] = {};
    ListNode mNode;
    unsigned char mPad1[0x08] = {};
    InvokableDelegate *mDelegatePtr = nullptr;
    unsigned char mPad2[0x28] = {};
    unsigned char mConnectedToDelegateEvent = 0;
};

// The event a slot subscribes to (sead::DelegateEvent<T>).
class DelegateEvent {
public:
    // TList<Slot*> is a list whose nodes are TListNode<Slot*>. pushBack stores the node.
    void pushBack(DelegateEventSlot *slot) {
        if (slot == nullptr) return;
        mList.pushBack(slot->node());
    }

    int getNumSlots() const { return static_cast<int>(mList.size()); }

    // sead::DelegateEvent::connect — the body registerCallback is a thin wrapper over.
    void connect(DelegateEventSlot &slot) {
        slot.release();       // unconditional release; release() itself is conditional
        pushBack(&slot);
        slot.setConnected(true);
    }

    void disconnect(DelegateEventSlot &slot) { slot.release(); }

    // Offsets: list head at +0x08 (after the vptr), count at +0x18.
    void *mVptr = nullptr; // +0x00 (DelegateEvent has a virtual destructor)
    List mList;            // +0x08

private:
};

// sead::AssertConfig — only the function the boot root calls.
class AssertConfig {
public:
    // 0x7101397fac, 160 bytes. Reaches DelegateEvent::connect.
    static void registerCallback(DelegateEventSlot &slot);

    // The global the binary links slots into (PTR_sAssertEvent_7102fd79a0).
    static DelegateEvent &assertEvent();
};

} // namespace sead
