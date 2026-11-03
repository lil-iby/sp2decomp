// sead::MessageQueue — see port/include/sead/message_queue.hpp.
#include "sead/message_queue.hpp"
#include "sead/heap.hpp"

#include <cstdlib>

namespace sead {

void MessageQueue::allocate(int size, Heap *heap) {
    if (size <= 0) return;
    const std::size_t bytes = sizeof(Element) * static_cast<std::size_t>(size);
    void *mem = heap != nullptr ? heap->tryAlloc(bytes, alignof(Element)) : std::malloc(bytes);
    if (mem == nullptr) return;
    mBuffer = static_cast<Element *>(mem);
    mBufferHeap = heap;
    nn::os::InitializeMessageQueue(&mMessageQueueInner, reinterpret_cast<std::uint64_t *>(mBuffer),
                                   static_cast<std::size_t>(size));
}

void MessageQueue::free() {
    nn::os::FinalizeMessageQueue(&mMessageQueueInner);
    if (mBuffer == nullptr) return;
    if (mBufferHeap != nullptr) mBufferHeap->free(mBuffer);
    else std::free(mBuffer);
    mBuffer = nullptr;
    mBufferHeap = nullptr;
}

bool MessageQueue::push(Element message, BlockType blockType) {
    const auto m = static_cast<std::uint64_t>(message);
    if (blockType == BlockType::Blocking) {
        nn::os::SendMessageQueue(&mMessageQueueInner, m);
        return true;
    }
    return nn::os::TrySendMessageQueue(&mMessageQueueInner, m);
}

MessageQueue::Element MessageQueue::pop(BlockType blockType) {
    std::uint64_t m = 0;
    if (blockType == BlockType::Blocking) {
        nn::os::ReceiveMessageQueue(&m, &mMessageQueueInner);
        return static_cast<Element>(m);
    }
    return nn::os::TryReceiveMessageQueue(&m, &mMessageQueueInner) ? static_cast<Element>(m)
                                                                    : cNullElement;
}

MessageQueue::Element MessageQueue::peek(BlockType blockType) const {
    std::uint64_t m = 0;
    if (blockType == BlockType::Blocking) {
        nn::os::PeekMessageQueue(&m, &mMessageQueueInner);
        return static_cast<Element>(m);
    }
    return nn::os::TryPeekMessageQueue(&m, &mMessageQueueInner) ? static_cast<Element>(m)
                                                                 : cNullElement;
}

bool MessageQueue::jam(Element message, BlockType blockType) {
    const auto m = static_cast<std::uint64_t>(message);
    if (blockType == BlockType::Blocking) {
        nn::os::JamMessageQueue(&mMessageQueueInner, m);
        return true;
    }
    return nn::os::TryJamMessageQueue(&mMessageQueueInner, m);
}

} // namespace sead
