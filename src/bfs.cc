// Copyright (c) 2015, The Regents of the University of California (Regents)
// See LICENSE.txt for license details

#include <chrono>
#include <cstdlib>
#include <execinfo.h>
#include <filesystem>
#include <iostream>
#include <unistd.h>
#include <vector>

#include "benchmark.h"
#include "bitmap.h"
#include "builder.h"
#include "command_line.h"
#include "graph.h"
#include "platform_atomics.h"
#include "pvector.h"
#include "sliding_queue.h"
#include "timer.h"

/*
GAP Benchmark Suite
Kernel: Breadth-First Search (BFS)
Author: Scott Beamer

Will return parent array for a BFS traversal from a source vertex

This BFS implementation makes use of the Direction-Optimizing approach [1].
It uses the alpha and beta parameters to determine whether to switch search
directions. For representing the frontier, it uses a SlidingQueue for the
top-down approach and a Bitmap for the bottom-up approach. To reduce
false-sharing for the top-down approach, thread-local QueueBuffer's are used.

To save time computing the number of edges exiting the frontier, this
implementation precomputes the degrees in bulk at the beginning by storing
them in parent array as negative numbers. Thus the encoding of parent is:
  parent[x] < 0 implies x is unvisited and parent[x] = -out_degree(x)
  parent[x] >= 0 implies x been visited

[1] Scott Beamer, Krste Asanović, and David Patterson. "Direction-Optimizing
    Breadth-First Search." International Conference on High Performance
    Computing, Networking, Storage and Analysis (SC), Salt Lake City, Utah,
    November 2012.
*/

using namespace std;
const char vtune_bin[] = "/opt/intel/oneapi/vtune/2023.1.0/bin64/vtune";
const char damo_bin[] = "/home/cc/damo/damo";

int64_t BUStep(const Graph &g, pvector<NodeID> &parent, Bitmap &front,
               Bitmap &next) {
  int64_t awake_count = 0;
  next.reset();
#pragma omp parallel for reduction(+ : awake_count) schedule(dynamic, 1024)
  for (NodeID u = 0; u < g.num_nodes(); u++) {
    if (parent[u] < 0) {
      for (NodeID v : g.in_neigh(u)) {
        if (front.get_bit(v)) {
          parent[u] = v;
          awake_count++;
          next.set_bit(u);
          break;
        }
      }
    }
  }
  return awake_count;
}

int64_t TDStep(const Graph &g, pvector<NodeID> &parent,
               SlidingQueue<NodeID> &queue) {
  int64_t scout_count = 0;
#pragma omp parallel
  {
    QueueBuffer<NodeID> lqueue(queue);
    // const int node = 0; // Use NUMA node 0
    // const size_t objectSize = sizeof(QueueBuffer<T>);
    // std::cout << "QueueBuffer size: " << objectSize << "\n" << std::flush;
    // void *memory = numa_alloc_onnode(local_size * objectSize, node);
    // if (memory == nullptr) {
    //   std::cerr << "Failed to allocate memory on NUMA node " << node
    //             << std::endl;
    // }
    // for (int i = 0; i < local_size; ++i) {
    //   void *objectMemory = static_cast<char *>(memory) + i * objectSize;
    //   QueueBuffer<T> *object = new (objectMemory) QueueBuffer<T>(i);
    // }

    std::cout << "TDStep addr: " << &lqueue << "\n" << std::flush;
    std::cout << "TDStep size: " << sizeof(lqueue) << "\n" << std::flush;
#pragma omp for reduction(+ : scout_count) nowait
    for (auto q_iter = queue.begin(); q_iter < queue.end(); q_iter++) {
      NodeID u = *q_iter;
      for (NodeID v : g.out_neigh(u)) {
        NodeID curr_val = parent[v];
        if (curr_val < 0) {
          if (compare_and_swap(parent[v], curr_val, u)) {
            lqueue.push_back(v);
            scout_count += -curr_val;
          }
        }
      }
    }
    lqueue.flush();
  }
  return scout_count;
}

void QueueToBitmap(const SlidingQueue<NodeID> &queue, Bitmap &bm) {
#pragma omp parallel for
  for (auto q_iter = queue.begin(); q_iter < queue.end(); q_iter++) {
    NodeID u = *q_iter;
    bm.set_bit_atomic(u);
  }
}

void BitmapToQueue(const Graph &g, const Bitmap &bm,
                   SlidingQueue<NodeID> &queue) {
#pragma omp parallel
  {
    QueueBuffer<NodeID> lqueue(queue);
    std::cout << "BitmapToQueue addr: " << &lqueue << "\n" << std::flush;
    std::cout << "BitmapToQueue size: " << sizeof(lqueue) << "\n" << std::flush;
#pragma omp for nowait
    for (NodeID n = 0; n < g.num_nodes(); n++)
      if (bm.get_bit(n))
        lqueue.push_back(n);
    lqueue.flush();
  }
  queue.slide_window();
}

pvector<NodeID> InitParent(const Graph &g) {
  pvector<NodeID> parent(g.num_nodes());
#pragma omp parallel for
  for (NodeID n = 0; n < g.num_nodes(); n++)
    parent[n] = g.out_degree(n) != 0 ? -g.out_degree(n) : -1;
  return parent;
}

void print_backtrace() {
  const int max_frames = 50;
  void *callstack[max_frames];
  int frames = backtrace(callstack, max_frames);

  char **strs = backtrace_symbols(callstack, frames);

  for (int i = 0; i < frames; i++) {
    std::cout << strs[i] << "\n" << std::flush;
  }

  free(strs);
}

pvector<NodeID> DOBFS(const Graph &g, NodeID source, int alpha = 15,
                      int beta = 18) {
  // PrintStep("Source", static_cast<int64_t>(source));
  Timer t;
  t.Start();
  pvector<NodeID> parent = InitParent(g);
  t.Stop();
  // PrintStep("i", t.Seconds());
  parent[source] = source;
  SlidingQueue<NodeID> queue(g.num_nodes());
  queue.push_back(source);
  queue.slide_window();
  Bitmap curr(g.num_nodes());
  curr.reset();
  Bitmap front(g.num_nodes());
  front.reset();
  int64_t edges_to_check = g.num_edges_directed();
  int64_t scout_count = g.out_degree(source);
  while (!queue.empty()) {
    if (scout_count > edges_to_check / alpha) {
      int64_t awake_count, old_awake_count;
      TIME_OP(t, QueueToBitmap(queue, front));
      // PrintStep("e", t.Seconds());
      awake_count = queue.size();
      queue.slide_window();
      do {
        t.Start();
        old_awake_count = awake_count;
        awake_count = BUStep(g, parent, front, curr);
        front.swap(curr);
        t.Stop();
        // PrintStep("bu", t.Seconds(), awake_count);
      } while ((awake_count >= old_awake_count) ||
               (awake_count > g.num_nodes() / beta));
      TIME_OP(t, BitmapToQueue(g, front, queue));
      // PrintStep("c", t.Seconds());
      scout_count = 1;
    } else {
      t.Start();
      edges_to_check -= scout_count;
      scout_count = TDStep(g, parent, queue);
      queue.slide_window();
      t.Stop();
      // PrintStep("td", t.Seconds(), queue.size());
    }
  }
// print_backtrace();
#pragma omp parallel for
  for (NodeID n = 0; n < g.num_nodes(); n++)
    if (parent[n] < -1)
      parent[n] = -1;
  return parent;
}

void PrintBFSStats(const Graph &g, const pvector<NodeID> &bfs_tree) {
  int64_t tree_size = 0;
  int64_t n_edges = 0;
  for (NodeID n : g.vertices()) {
    if (bfs_tree[n] >= 0) {
      n_edges += g.out_degree(n);
      tree_size++;
    }
  }
  cout << "BFS Tree has " << tree_size << " nodes and ";
  cout << n_edges << " edges" << endl;
}

// BFS verifier does a serial BFS from same source and asserts:
// - parent[source] = source
// - parent[v] = u  =>  depth[v] = depth[u] + 1 (except for source)
// - parent[v] = u  => there is edge from u to v
// - all vertices reachable from source have a parent
bool BFSVerifier(const Graph &g, NodeID source, const pvector<NodeID> &parent) {
  pvector<int> depth(g.num_nodes(), -1);
  depth[source] = 0;
  vector<NodeID> to_visit;
  to_visit.reserve(g.num_nodes());
  to_visit.push_back(source);
  for (auto it = to_visit.begin(); it != to_visit.end(); it++) {
    NodeID u = *it;
    for (NodeID v : g.out_neigh(u)) {
      if (depth[v] == -1) {
        depth[v] = depth[u] + 1;
        to_visit.push_back(v);
      }
    }
  }
  for (NodeID u : g.vertices()) {
    if ((depth[u] != -1) && (parent[u] != -1)) {
      if (u == source) {
        if (!((parent[u] == u) && (depth[u] == 0))) {
          cout << "Source wrong" << endl;
          return false;
        }
        continue;
      }
      bool parent_found = false;
      for (NodeID v : g.in_neigh(u)) {
        if (v == parent[u]) {
          if (depth[v] != depth[u] - 1) {
            cout << "Wrong depths for " << u << " & " << v << endl;
            return false;
          }
          parent_found = true;
          break;
        }
      }
      if (!parent_found) {
        cout << "Couldn't find edge from " << parent[u] << " to " << u << endl;
        return false;
      }
    } else if (depth[u] != parent[u]) {
      cout << "Reachability mismatch" << endl;
      return false;
    }
  }
  return true;
}

void GetCurTime(const char *identifier) {
  auto now = std::chrono::system_clock::now();
  auto seconds = std::chrono::time_point_cast<std::chrono::seconds>(now);
  auto fraction = now - seconds;

  std::cout << identifier << " at: " << seconds.time_since_epoch().count()
            << "." << fraction.count() << "\n"
            << std::flush;
  // std::cout  << " nanoseconds within current second\n";
}

void run_vtune_bg(pid_t cur_pid) {
  cout << "running vtune \n" << std::flush;
  char vtune_cmd[200];
  char vtune_path[] =
      "/home/cc/functions/run_bench/vtune_log/gapbs_bfs_twitter_whole";
  std::filesystem::path dir_path(vtune_path);
  if (std::filesystem::exists(dir_path)) {
    if (!std::filesystem::is_empty(dir_path)) {
      for (const auto &entry : std::filesystem::directory_iterator(dir_path)) {
        if (entry.is_regular_file()) {
          std::filesystem::remove(entry.path());
        }
      }
    }
  }
  std::sprintf(vtune_cmd, "%s -collect uarch-exploration -r %s -target-pid %d",
               vtune_bin, vtune_path, cur_pid);
  int ret = system(vtune_cmd);
  if (ret == -1) {
    std::cerr << "Error: failed to execute command" << std::flush;
    exit(EXIT_FAILURE);
  }
  exit(EXIT_SUCCESS);
}

void run_damo_bg(pid_t cur_pid) {
  cout << "running damo \n" << std::flush;
  char damo_cmd[200];
  char damo_path[] = "/home/cc/functions/run_bench/playground/"
                     "gapbs_bfs_twitter_whole/gapbs_bfs_twitter_whole.data";
  std::sprintf(
      damo_cmd,
      "sudo %s record -s 1000 -a 100000 -u 1000000 -n 1024 -m 1024 -o %s %d",
      damo_bin, damo_path, cur_pid);
  int ret = system(damo_cmd);
  if (ret == -1) {
    std::cerr << "Error: failed to execute command" << std::flush;
    exit(EXIT_FAILURE);
  }
  exit(EXIT_SUCCESS);
}

int main(int argc, char *argv[]) {
  GetCurTime("whole start");
  CLApp cli(argc, argv, "breadth-first search");
  if (!cli.ParseArgs())
    return -1;
  Builder b(cli);
  Graph g = b.MakeGraph();
  SourcePicker<Graph> sp(g, cli.start_vertex());
  auto BFSBound = [&sp](const Graph &g) { return DOBFS(g, sp.PickNext()); };
  SourcePicker<Graph> vsp(g, cli.start_vertex());
  auto VerifierBound = [&vsp](const Graph &g, const pvector<NodeID> &parent) {
    return BFSVerifier(g, vsp.PickNext(), parent);
  };
  // get trace
  // bool run_damo = false;
  // bool run_vtune = false;
  pid_t cur_pid = getpid();
  if (cli.do_vtune()) {
    // std::cout << "get into do vtune\n" << std::flush;
    pid_t vtune_pid = fork();
    if (vtune_pid == -1) {
      std::cerr << "Error: fork() failed" << std::flush;
      exit(EXIT_FAILURE);
    } else if (vtune_pid == 0) {
      run_vtune_bg(cur_pid);
    }
  }
  if (cli.do_heatmap()) {
    // std::cout << "get into do damo\n" << std::flush;
    pid_t damo_pid = fork();
    if (damo_pid == -1) {
      std::cerr << "Error: fork() failed" << std::flush;
      exit(EXIT_FAILURE);
    } else if (damo_pid == 0) {
      run_damo_bg(cur_pid);
    }
  }

  GetCurTime("computing start");
  BenchmarkKernel(cli, g, BFSBound, PrintBFSStats, VerifierBound);
  GetCurTime("all finish");
  return 0;
}
