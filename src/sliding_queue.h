// Copyright (c) 2015, The Regents of the University of California (Regents)
// See LICENSE.txt for license details

#ifndef SLIDING_QUEUE_H_
#define SLIDING_QUEUE_H_

#include <algorithm>
#include <bitset>
#include <iostream>
#include <numa.h>
#include <numaif.h>
#include <unistd.h>

#include "platform_atomics.h"

/*
GAP Benchmark Suite
Class:  SlidingQueue
Author: Scott Beamer

Double-buffered queue so appends aren't seen until SlideWindow() called
 - Use QueueBuffer when used in parallel to avoid false sharing by doing
   bulk appends from thread-local storage
*/

template <typename T> class QueueBuffer;

template <typename T> class SlidingQueue {
  T *shared;
  size_t shared_in;
  size_t shared_out_start;
  size_t shared_out_end;
  friend class QueueBuffer<T>;

public:
  explicit SlidingQueue(size_t shared_size) {
    // std::cout << "shared_size: " << shared_size << "\n" << std::flush;
    shared = new T[shared_size];
    // std::cout << "shared sliding queue: " << shared << " ; "
    //           << static_cast<intptr_t>(reinterpret_cast<intptr_t>(shared))
    //           << "\n"
    //           << std::flush;
    reset();
  }

  ~SlidingQueue() { delete[] shared; }

  void push_back(T to_add) { shared[shared_in++] = to_add; }

  bool empty() const { return shared_out_start == shared_out_end; }

  void reset() {
    shared_out_start = 0;
    shared_out_end = 0;
    shared_in = 0;
  }

  void slide_window() {
    shared_out_start = shared_out_end;
    shared_out_end = shared_in;
  }

  typedef T *iterator;

  iterator begin() const { return shared + shared_out_start; }

  iterator end() const { return shared + shared_out_end; }

  size_t size() const { return end() - begin(); }
};

template <typename T> class QueueBuffer {
  size_t in;
  T *local_queue;
  SlidingQueue<T> &sq;
  const size_t local_size;

public:
  explicit QueueBuffer(SlidingQueue<T> &master, size_t given_size = 16384)
      : sq(master), local_size(given_size) {
    in = 0;
    // how to replace this statement using numalib?
    local_queue = new T[local_size];
    // const size_t objectSize = sizeof(T);
    // void *memory = numa_alloc_onnode(local_size * objectSize, 1);

    // for (int i = 0; i < 16384; ++i) {
    //   void *objectMemory = static_cast<char *>(memory) + i * objectSize;
    //   void *object = new (objectMemory) T;
    // }
    // local_queue = static_cast<T *>(memory);

    // std::cout << "local size in constructor: " << local_size << "\n"
    //           << std::flush;
  }

  ~QueueBuffer() {
    delete[] local_queue;
    // numa_free(local_queue, local_size * sizeof(T));
  }

  void push_back(T to_add) {
    if (in == local_size)
      flush();
    local_queue[in++] = to_add;
  }

  void flush() {
    T *shared_queue = sq.shared;
    size_t copy_start = fetch_and_add(sq.shared_in, in);
    std::copy(local_queue, local_queue + in, shared_queue + copy_start);
    in = 0;
  }
};

#endif // SLIDING_QUEUE_H_
