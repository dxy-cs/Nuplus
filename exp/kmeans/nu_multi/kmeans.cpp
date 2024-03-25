/* Copyright (c) 2007-2011, Stanford University
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *      * Redistributions of source code must retain the above copyright
 *         notice, this list of conditions and the following disclaimer.
 *      * Redistributions in binary form must reproduce the above copyright
 *         notice, this list of conditions and the following disclaimer in the
 *         documentation and/or other materials provided with the distribution.
 *      * Neither the name of Stanford University nor the names of its
 *         contributors may be used to endorse or promote products derived from
 *         this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY STANFORD UNIVERSITY ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL STANFORD UNIVERSITY BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <algorithm>
#include <cereal/archives/binary.hpp>
#include <cereal/types/array.hpp>
#include <cereal/types/vector.hpp>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <sys/mman.h>
#include <sys/stat.h>
#include <vector>

/*dxy++*/
#include <atomic>
#include <thread>
#include <chrono>

#include "map_reduce.h"

/*dxy++*/
extern std::atomic_uint64_t nu_call_count;
extern std::atomic_uint64_t nu_call_lc;
extern std::atomic_uint64_t nu_call_rt;
extern std::atomic_uint64_t nu_call_rt_cachemiss;
extern std::atomic_uint64_t nu_commu_time;
 
using Data_t = int64_t;

// Number of vectors
constexpr int kNumPoints = 1000000;
// Dimension of each vector
constexpr int kDim = 400;
// Number of clusters
constexpr int kNumMeans = 1000;
// Size of each dimension of vector space
constexpr Data_t kGridSize = 8ULL << 32;
constexpr bool kDumpResult = false;
constexpr int kChunkSize = 32;

constexpr int kNumWorkerNodes = 1;
constexpr int kNumThreadsPerWorker = 5;
constexpr int kNumWorkerThreads = kNumWorkerNodes * kNumThreadsPerWorker;

struct point {
  Data_t d[kDim];
  int cluster; // cluster point count (for means)

  template <class Archive> void serialize(Archive &ar) { ar(d, cluster); }

  point() { cluster = -1; }

  point(int cluster) { this->cluster = cluster; }

  point(Data_t *d, int cluster) {
    memcpy(this->d, d, sizeof(this->d));
    this->cluster = cluster;
  }

  point &normalize() {
    for (int i = 0; i < kDim; ++i)
      d[i] /= cluster;
    cluster = 1;
    return *this;
  }

  Data_t sq_dist(point const &p) {
    Data_t sum = 0;
    for (int i = 0; i < kDim; i++) {
      Data_t diff = d[i] - p.d[i];
      sum += diff * diff;
    }
    return sum;
  }

  void dump() {
    for (int j = 0; j < kDim; j++)
      printf("%5lld ", static_cast<long long>(d[j]));
    printf("\n");
  }

  void generate() {
    for (int j = 0; j < kDim; j++)
      d[j] = rand() % kGridSize;
  }

  bool operator==(const point &o) const {
    return std::equal(std::begin(this->d), std::end(this->d), o.d);
  }
};

std::vector<point> points;

template <class V, template <class> class Allocator>
class point_combiner
    : public associative_combiner<point_combiner<V, Allocator>, V, Allocator> {
public:
  static void F(point &a, point const &b) {
    a.cluster += b.cluster;
    for (int i = 0; i < kDim; i++)
      a.d[i] += b.d[i];
  }
  static void Init(point &a) {
    std::fill(std::begin(a.d), std::end(a.d), 0);
    a.cluster = 0;
  }
  static bool Empty(point const &a) { return a.cluster == 0; }
};

using task_id = int;

class KmeansMR
    : public MapReduce<KmeansMR, task_id, intptr_t, point, point_combiner> {
public:
  std::vector<point> means;

  KmeansMR() {}

  KmeansMR(uint64_t num_worker_threads) : MapReduce(num_worker_threads) {}

  void map(data_type &task_id, map_container &out) const {
    Data_t min_dist = std::numeric_limits<Data_t>::max();
    uint64_t min_idx = 0;
    auto &p = points[task_id];

    for (size_t j = 0; j < means.size(); j++) {
      Data_t cur_dist = p.sq_dist(means[j]);
      if (cur_dist < min_dist) {
        min_dist = cur_dist;
        min_idx = j;
      }
    }
    emit_intermediate(out, min_idx, point(p.d, 1));
  }
};

void real_main(int argc, char **argv) {
  std::vector<point> local_means;
  for (int i = 0; i < kNumMeans; i++) {
    local_means.emplace_back(0);
    local_means[i].generate();
  }

  printf("KMeans: Calling MapReduce Scheduler\n");

  //std::thread([] {
  //  while (true) {
  //    enum {
  //      kPrintInterval = 5,
  //    };
  //    uint64_t cur_call_count = nu_call_count.load();
  //    uint64_t cur_call_lc = nu_call_lc.load();
  //    uint64_t cur_call_rt = nu_call_rt.load();
  //    uint64_t cur_rt_cachemiss = nu_call_rt_cachemiss.load();
  //    std::this_thread::sleep_for(std::chrono::seconds(kPrintInterval));
  //    std::cout << std::endl;
  //    std::cout << "call_local: " << cur_call_lc << "(" << 1.0 * cur_call_lc / cur_call_count << ")" << std::endl;
  //    std::cout << "call_remote: " << cur_call_rt << "(" << 1.0 * cur_call_rt / cur_call_count << ")" << std::endl;
  //    std::cout << "call_cache_miss: " << cur_rt_cachemiss << "(" << 1.0 * cur_rt_cachemiss / cur_call_rt << ")" << std::endl;
  //    std::cout << std::endl;
  //  }
  //}).detach();

  KmeansMR mapReduce(kNumWorkerThreads);

  std::vector<task_id> tasks;
  for (int i = 0; i < kNumPoints; i++) {
    tasks.push_back(i);
  }

  bool modified;
  int iter = 0;
  do {
    std::cout << "iter = " << iter++ << std::endl;
    /*dxy++*/
    nu_call_count = 0;
    nu_call_lc = 0;
    nu_call_rt = 0;
    nu_call_rt_cachemiss = 0;
    nu_commu_time = 0;

    auto t0 = microtime();

    mapReduce.for_all_worker_threads(
        +[](KmeansMR &mr, std::vector<point> src_means) {
          mr.means = std::move(src_means);
        },
        local_means);

    auto t1 = microtime();

    std::vector<KmeansMR::keyval> result;
    BUG_ON(mapReduce.run(tasks.data(), kNumPoints, result, kChunkSize) < 0);

    auto t2 = microtime();

    modified = false;
    for (size_t i = 0; i < result.size(); i++) {
      auto new_mean = result[i].val.normalize();
      auto &mean = local_means[result[i].key];
      if (mean != new_mean) {
        modified = true;
        mean = new_mean;
      }
    }

    auto t3 = microtime();
    /*dxy++*/
    std::cout << "call_count: " << nu_call_count << std::endl;
    std::cout << "1_time: " << t1 - t0 << " " << "2_time: " << t2 - t1 << " " << "3_time: " << t3 - t2 << std::endl;
    std::cout << "commu_ratio: " << 1.0 * nu_commu_time / (t3 - t0) << std::endl;
    
  } while (modified);

  printf("KMeans: MapReduce Completed\n");

  if constexpr (kDumpResult) {
    printf("\n\nFinal means:\n");
    for (int i = 0; i < kNumMeans; i++)
      local_means[i].dump();
  }
}

int main(int argc, char **argv) {
  // srand(0): the generated 'points' sequence will be the same for each execution.@dxy
  srand(0);
  for (int i = 0; i < kNumPoints; i++) {
    points.emplace_back();
    points.back().generate();
  }
  nu::runtime_main_init(argc, argv,
                        [](int argc, char **argv) { real_main(argc, argv); });
}

// vim: ts=8 sw=4 sts=4 smarttab smartindent
