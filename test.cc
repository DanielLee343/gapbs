#include <bitset>
#include <iostream>
// #include <numa++/numa.h>
#include <numa.h>
#include <numaif.h>
typedef int32_t NodeID;

template <typename T> class QueueBuffer {
  int in;
  T *local_queue;

public:
  QueueBuffer(size_t given_size) {
    std::cout << "allocate\n" << std::flush;
    this->in = given_size;
    local_queue = new T[given_size];
  }
  ~QueueBuffer() {
    std::cout << "deallocate\n" << std::flush;
    delete[] local_queue;
  }
  void printValue() { std::cout << "in: " << in << std::endl; }
};

// void cpp_version() {
//   const int node = 0;       // Use NUMA node 0
//   const int numObjects = 5; // Create 5 objects

//   // Set the current thread's affinity to the specified NUMA node
//   numa::set_thread_affinity(node);

//   // Create objects on the specified NUMA node
//   QueueBuffer<int> *objects =
//       numa::allocate_onnode<QueueBuffer<int>>(numObjects, node);
//   for (int i = 0; i < numObjects; ++i) {
//     objects[i] = QueueBuffer<int>(i);
//   }

//   // Access the objects
//   for (int i = 0; i < numObjects; ++i) {
//     objects[i].printValue();
//   }

//   // Free the allocated memory
//   numa::deallocate(objects, numObjects);
// }

int_least32_t c_version() {
  // Specify the desired NUMA node for the allocation
  const int node = 0; // Use NUMA node 0
  const int maxNode = numa_max_node();
  size_t given_size = 5;
  const size_t objectSize = sizeof(QueueBuffer<NodeID>);

  // pre-allocate memory on node 0
  void *memory = numa_alloc_onnode(given_size * objectSize, node);
  if (memory == nullptr) {
    std::cerr << "Failed to allocate memory on NUMA node " << node << std::endl;
    return 1;
  }
  // Construct objects in the allocated memory using placement new
  for (int i = 0; i < given_size; ++i) {
    void *objectMemory = static_cast<char *>(memory) + i * objectSize;
    QueueBuffer<NodeID> *object = new (objectMemory) QueueBuffer<NodeID>(i);
  }

  // Access the objects
  for (int i = 0; i < given_size; ++i) {
    void *objectMemory = static_cast<char *>(memory) + i * objectSize;
    QueueBuffer<NodeID> *object =
        static_cast<QueueBuffer<NodeID> *>(objectMemory);
    object->printValue();
  }
  numa_free(memory, given_size * objectSize);
  return 0;
}

int main() {
  c_version();
  return 0;
}
