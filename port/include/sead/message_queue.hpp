// sead::MessageQueue — a bounded queue of 64-bit messages over nn::os::MessageQueueType.
//
// Binary: MessageQueue::MessageQueue (0x7100eba350, depth 4, 8B) and MessageQueue::allocate
// (0x7100eba35c, depth 4, 108B, 5 calls). Interface and behaviour from open-ead/sead (no license, written
// independently). Its layout — the 0x48-byte nn::os queue directly followed by the buffer
// pointer — matches the "always exactly 0x48 apart" measurement recorded in nn/os.hpp.
#pragma once

#include "nn/os.hpp"

#include <cstdint>

namespace sead {

class Heap;

class MessageQueue {
public:
    using Element = std::int64_t;

    enum class BlockType { Blocking = 0, NonBlocking = 1 };

    // Returned by a non-blocking pop/peek on an empty queue.
    static constexpr Element cNullElement = 0;

    MessageQueue() = default;
    ~MessageQueue() = default;

    MessageQueue(const MessageQueue &) = delete;
    MessageQueue &operator=(const MessageQueue &) = delete;

    // `size` slots, allocated from `heap` (host memory when heap is null).
    void allocate(int size, Heap *heap);
    void free();
    bool push(Element message, BlockType blockType);
    Element pop(BlockType blockType);
    Element peek(BlockType blockType) const;
    // Like push, but the message goes to the front of the queue.
    bool jam(Element message, BlockType blockType);

private:
    nn::os::MessageQueueType mMessageQueueInner{};
    Element *mBuffer = nullptr;
    // Port-only: remembers where mBuffer came from so free() returns it there.
    Heap *mBufferHeap = nullptr;
};

} // namespace sead
