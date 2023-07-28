// Copyright (c) 2015, The Regents of the University of California (Regents)
// See LICENSE.txt for license details

#ifndef UTIL_H_
#define UTIL_H_

#include <cinttypes>
#include <execinfo.h>
#include <filesystem>
#include <stdio.h>
#include <string>

#include "timer.h"
const char vtune_bin[] = "/opt/intel/oneapi/vtune/2023.1.0/bin64/vtune";
const char damo_bin[] = "/home/cc/damo/damo";
// const char damo_bin[] = "damo";

/*
GAP Benchmark Suite
Author: Scott Beamer

Miscellaneous helpers that don't fit into classes
*/

static const int64_t kRandSeed = 27491095;

void PrintLabel(const std::string &label, const std::string &val) {
  printf("%-21s%7s\n", (label + ":").c_str(), val.c_str());
}

void PrintTime(const std::string &s, double seconds) {
  printf("%-21s%3.5lf\n", (s + ":").c_str(), seconds);
}

void PrintStep(const std::string &s, int64_t count) {
  printf("%-14s%14" PRId64 "\n", (s + ":").c_str(), count);
}

void PrintStep(int step, double seconds, int64_t count = -1) {
  if (count != -1)
    printf("%5d%11" PRId64 "  %10.5lf\n", step, count, seconds);
  else
    printf("%5d%23.5lf\n", step, seconds);
}

void PrintStep(const std::string &s, double seconds, int64_t count = -1) {
  if (count != -1)
    printf("%5s%11" PRId64 "  %10.5lf\n", s.c_str(), count, seconds);
  else
    printf("%5s%23.5lf\n", s.c_str(), seconds);
}

// Runs op and prints the time it took to execute labelled by label
#define TIME_PRINT(label, op)                                                  \
  {                                                                            \
    Timer t_;                                                                  \
    t_.Start();                                                                \
    (op);                                                                      \
    t_.Stop();                                                                 \
    PrintTime(label, t_.Seconds());                                            \
  }

void run_vtune_bg(pid_t cur_pid) {
  std::cout << "running vtune \n" << std::flush;
  char vtune_cmd[200];
  char vtune_path[] =
      "/home/cc/functions/run_bench/vtune_log/gapbs_cc_twitter_whole";
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

void run_damo_bg(pid_t cur_pid, char* damo_path) {
  std::cout << "running damo \n" << std::flush;
  char damo_cmd[300];
  char damo_path_[150];
  std::sprintf(damo_path_, "/home/cc/functions/run_bench/playground/%s/%s.data",
               damo_path, damo_path);
  
  std::sprintf(
      damo_cmd,
      "sudo %s record -o %s %d",
      // "sudo %s record -s 1000 -a 200000 -u 2000000 "
      // "-n 20000 -m 22000 -o %s %d",
      damo_bin, damo_path_, cur_pid);
  int ret = system(damo_cmd);
  if (ret == -1) {
    std::cerr << "Error: failed to execute command" << std::flush;
    exit(EXIT_FAILURE);
  }
  exit(EXIT_SUCCESS);
}

void GetCurTime(const char *identifier) {
  // auto now = std::chrono::system_clock::now();
  // auto seconds = std::chrono::time_point_cast<std::chrono::seconds>(now);
  // auto fraction = now - seconds;

  // std::cout << identifier << " at: " << seconds.time_since_epoch().count()
  //           << "." << fraction.count() << "\n"
  //           << std::flush;
  // std::cout  << " nanoseconds within current second\n";
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  // printf("%s at: %ld.%ld\n", identifier, ts.tv_sec, ts.tv_nsec);
  // if (ts.tv_nsec < 1000000000) {
  //   std::cout << identifier << " at: " << ts.tv_sec << "0" << ts.tv_nsec << "\n" << std::flush;
  // }else {
  std::cout << identifier << " at: " << ts.tv_sec << "." << ts.tv_nsec << "\n" << std::flush;
  // }
}

template <typename T_> class RangeIter {
  T_ x_;

public:
  explicit RangeIter(T_ x) : x_(x) {}
  bool operator!=(RangeIter const &other) const { return x_ != other.x_; }
  T_ const &operator*() const { return x_; }
  RangeIter &operator++() {
    ++x_;
    return *this;
  }
};

template <typename T_> class Range {
  T_ from_;
  T_ to_;

public:
  explicit Range(T_ to) : from_(0), to_(to) {}
  Range(T_ from, T_ to) : from_(from), to_(to) {}
  RangeIter<T_> begin() const { return RangeIter<T_>(from_); }
  RangeIter<T_> end() const { return RangeIter<T_>(to_); }
};

#endif // UTIL_H_
