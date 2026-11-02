#pragma once
// Sead-heap-aware global operator new/delete overloads.
//
// Mirrors three functions in the boot closure (depth 1):
//
//   _ZnwmPN4sead4HeapEi  0x7100e5629c  116B  4 call targets  — operator new  (scalar)
//   _ZnamPN4sead4HeapEi  0x7100e56310  116B  2 call targets  — operator new[] (array)
//   _ZdaPv               0x7100e565b0  100B  3 call targets  — operator delete[] (global replacement)
//
// Signature (decompiler, Ghidra):
//   void * operator_new(size_t param_1, long *param_2, undefined4 param_3)
//   i.e. (size_t size, sead::Heap* heap, int align)
//
// These are PLACEMENT new overloads (extra arguments distinguish them from the
// standard operator new — they do not replace the global allocator).  C++ requires
// matching placement-delete overloads; they are provided below.
//
// _ZdaPv IS the global operator delete[] replacement.  It dispatches through
// sead::HeapMgr::findContainHeap to locate the owning heap, then calls heap->free.
// For pointers not in any sead heap it falls back to std::free so that standard-
// library allocations (from test harness code) are freed correctly.
#include "sead/heap.hpp"
#include <cstddef>

// ---------------------------------------------------------------------------
// Placement new — scalar  (0x7100e5629c)
// ---------------------------------------------------------------------------
void *operator new(std::size_t size, sead::Heap *heap, int align);

// ---------------------------------------------------------------------------
// Placement new — array  (0x7100e56310)
// ---------------------------------------------------------------------------
void *operator new[](std::size_t size, sead::Heap *heap, int align);

// ---------------------------------------------------------------------------
// Matching placement-delete counterparts (C++ §18.6.1.3 requires these when
// placement new is defined; never called directly by user code, only by the
// compiler when a constructor throws after a successful placement new).
// ---------------------------------------------------------------------------
void operator delete(void *ptr, sead::Heap *heap, int align) noexcept;
void operator delete[](void *ptr, sead::Heap *heap, int align) noexcept;

// ---------------------------------------------------------------------------
// Global operator delete[] replacement  (_ZdaPv, 0x7100e565b0)
//
// The binary's body dispatches through heap->free (virtual call, vtable +0x38).
// The port uses findContainHeap to reproduce that dispatch faithfully.
// ---------------------------------------------------------------------------
void operator delete[](void *ptr) noexcept;
