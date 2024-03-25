#include <algorithm>
#include <cereal/types/optional.hpp>
#include <cereal/types/string.hpp>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <numeric>
#include <random>
#include <utility>
#include <vector>
/*dxy++*/
#include <cstdint>
#include <x86intrin.h>
#include <atomic>
#include <mutex>
#include <unordered_map>
#include <runtime.h>

#include "nu/dis_hash_table.hpp"
#include "nu/proclet.hpp"
#include "nu/runtime.hpp"
#include "nu/utils/farmhash.hpp"
#include "nu/utils/thread.hpp"
/*dxy++*/
#include "nu/utils/sync_hash_map.hpp"

constexpr uint32_t kKeyLen = 20;
constexpr uint32_t kValLen = 2;
constexpr double kLoadFactor = 0.30;
constexpr uint32_t kNumProxys = 1;
constexpr uint32_t kProxyPort = 10086;

/*dxy++*/
static std::atomic_uint64_t count = 0;  // atomic<uint64_t>
static std::atomic_uint64_t nu_time = 0;  
static std::atomic_uint64_t local_time = 0;

extern std::atomic_uint64_t nu_get_shard_id_time;
extern std::atomic_uint64_t nu_caller_migr_guard_time;
extern std::atomic_uint64_t nu_callee_migr_guard_time;
extern std::atomic_uint64_t nu_pass_states_time;
extern std::atomic_uint64_t nu_set_monitor_time;
extern std::atomic_uint64_t nu_exec_time;
extern std::atomic_uint64_t nu_actual_exec_time;
extern std::atomic_uint64_t nu_end_monitor_time;
extern std::atomic_uint64_t nu_return_val_to_caller_time;

struct Key {
  char data[kKeyLen];

  bool operator==(const Key &o) const {
    return __builtin_memcmp(data, o.data, kKeyLen) == 0;
  }

  template <class Archive> void serialize(Archive &ar) {
    ar(cereal::binary_data(data, sizeof(data)));
  }
};

struct Val {
  char data[kValLen];

  /*dxy++*/
  bool operator==(const Val &other) const {
    return std::memcmp(this->data, other.data, kValLen) == 0;
  }

  template <class Archive> void serialize(Archive &ar) {
    ar(cereal::binary_data(data, sizeof(data)));
  }
};

struct Req {
  /*dxy+1*/
  bool end_of_req;
  Key key;
  uint32_t shard_id;
};

struct Resp {
  int latest_shard_ip;
  bool found;
  Val val;
};

constexpr static auto kFarmHashKeytoU64 = [](const Key &key) {
  return util::Hash64(key.data, kKeyLen);
};

using DSHashTable =
    nu::DistributedHashTable<Key, Val, decltype(kFarmHashKeytoU64)>;

/*dxy++*/
constexpr static size_t kNumBuckets = (1 << DSHashTable::kDefaultPowerNumShards) *
                                       DSHashTable::kNumBucketsPerShard;
using LCHashTable = 
    nu::SyncHashMap<kNumBuckets, Key, Val, decltype(kFarmHashKeytoU64), std::equal_to<Key>,
                  std::allocator<std::pair<const Key, Val>>, nu::Mutex>;
LCHashTable local_hash_table;

//struct KeyHash {
//  std::size_t operator()(const Key &key) const noexcept {
//    return util::Hash64(key.data, kKeyLen);
//  }
//};
//using LocalHashMap = std::unordered_map<Key, Val, KeyHash>;
//LocalHashMap local_hash_table;
//std::mutex mutex_;

constexpr static size_t kNumPairs = (1 << DSHashTable::kDefaultPowerNumShards) *
                                    DSHashTable::kNumBucketsPerShard *
                                    kLoadFactor;

void random_str(auto &dist, auto &mt, uint32_t len, char *buf) {
  for (uint32_t i = 0; i < len; i++) {
    buf[i] = dist(mt);
  }
}

void init(DSHashTable *hash_table) {
  std::vector<nu::Thread> threads;
  constexpr uint32_t kNumThreads = 400;
  for (uint32_t i = 0; i < kNumThreads; i++) {
    threads.emplace_back([&, tid = i] {
      //std::random_device rd;
      //std::mt19937 mt(rd());
      //std::mt19937 mt(i);
      /*dxy+1*/
      std::minstd_rand mt(i);
      std::uniform_int_distribution<int> dist('A', 'z');
      auto num_pairs = kNumPairs / kNumThreads;
      for (size_t j = 0; j < num_pairs; j++) {
        Key key;
        Val val;
        random_str(dist, mt, kKeyLen, key.data);
        random_str(dist, mt, kValLen, val.data);
        hash_table->put(key, val);
        /*dxy+1*/
        local_hash_table.put(key, val);
      }
    });
  }
  //threads.emplace_back([] {
  //  for (uint32_t i = 0; i < kNumThreads; i++) {
  //    std::minstd_rand mt(i);
  //    std::uniform_int_distribution<int> dist('A', 'z');
  //    auto num_pairs = kNumPairs / kNumThreads;
  //    for (size_t j = 0; j < num_pairs; j++) {
  //      Key key;
  //      Val val;
  //      random_str(dist, mt, kKeyLen, key.data);
  //      random_str(dist, mt, kValLen, val.data);
  //      local_hash_table.insert({key, val});
  //    }
  //  }
  //});
  for (auto &thread : threads) {
    thread.join();
  }
}

class Proxy {
 public:
  Proxy(DSHashTable hash_table) : hash_table_(std::move(hash_table)) {}

  void run_loop() {
    netaddr laddr = {.ip = 0, .port = kProxyPort};
    auto *queue = rt::TcpQueue::Listen(laddr, 128);
    rt::TcpConn *c;
    while ((c = queue->Accept())) {
      nu::Thread([&, c] { handle(c); }).detach();
    }
  }

  void handle(rt::TcpConn *c) {
    while (true) {
      Req req;
      BUG_ON(c->ReadFull(&req, sizeof(req)) <= 0);
      Resp resp;
      /*dxy++*/
      if (req.end_of_req) [[unlikely]] {
        std::cout << "count:" << count << std::endl;
        std::cout << "nu time: " << nu_time / 3300 << std::endl;  // us
        std::cout << "local time: " << local_time / 3300 << std::endl;  // us
        std::cout << "nu/local: " << (nu_time * 1.0 / local_time) << std::endl;
        uint64_t nu_sum_time = nu_get_shard_id_time + nu_caller_migr_guard_time 
                               + nu_callee_migr_guard_time + nu_pass_states_time 
                               + nu_set_monitor_time + nu_exec_time + nu_end_monitor_time
                               + nu_return_val_to_caller_time;
        std::cout << "nu_sum_time: " <<  nu_sum_time / 3300 << std::endl;
        std::cout << "nu_get_shard_id: " << (nu_get_shard_id_time * 1.0 / nu_sum_time) << std::endl;
        std::cout << "nu_caller_migr_guard: " << (nu_caller_migr_guard_time * 1.0 / nu_sum_time) << std::endl;
        std::cout << "nu_callee_migr_guard: " << (nu_callee_migr_guard_time * 1.0 / nu_sum_time) << std::endl;
        std::cout << "nu_pass_states: " << (nu_pass_states_time * 1.0 / nu_sum_time) << std::endl;
        std::cout << "nu_set_monitor: " << (nu_set_monitor_time * 1.0 / nu_sum_time) << std::endl;
        std::cout << "nu_exec: " << (nu_exec_time * 1.0 / nu_sum_time) << std::endl;
        std::cout << "nu_actual_exec: " << (nu_actual_exec_time * 1.0 / 106557224752) << std::endl;
        std::cout << "nu_end_monitor: " << (nu_end_monitor_time * 1.0 / nu_sum_time) << std::endl;
        std::cout << "nu_return_val_to_caller: " << (nu_return_val_to_caller_time * 1.0 / nu_sum_time) << std::endl;

        BUG_ON(c->WriteFull(&resp, sizeof(resp)) < 0);
        break;
      }
      count ++;

      bool is_local;
      /*dxy++*/
      uint64_t nu_s = rdtsc1();
      auto optional_v = hash_table_.get_with_profile(req.key, &is_local);
      uint64_t nu_e = rdtsc1();
      nu_time += (nu_e - nu_s);

      uint64_t local_s = rdtsc1();
      auto local_val = local_hash_table.get_copy(req.key);
      uint64_t local_e = rdtsc1();
      bool local_found = (local_val == std::nullopt) ? 0 : 1;
      BUG_ON(optional_v.has_value() != local_found);
      if (local_found) {
        BUG_ON(optional_v.value() != *local_val);
      }
      local_time += (local_e - local_s);
  
      //mutex_.lock();
      //uint64_t start_local = rdtsc1();
      //auto it = local_hash_table.find(req.key);
      //uint64_t end_local = rdtsc1();
      //mutex_.unlock();
      //bool local_found = it == local_hash_table.end();
      //BUG_ON(optional_v.has_value() != local_found);
      //if (local_found) {
      //  BUG_ON(optional_v.value() != it->second);
      //}
      //local_time_sum += (end_local - start_local);
      

      resp.found = optional_v.has_value();
      if (resp.found) {
        resp.val = *optional_v;
      }
      auto id = hash_table_.get_shard_proclet_id(req.shard_id);
      if (is_local) {
        resp.latest_shard_ip = 0;
      } else {
        resp.latest_shard_ip =
            nu::get_runtime()->rpc_client_mgr()->get_ip_by_proclet_id(id);
      }
      BUG_ON(c->WriteFull(&resp, sizeof(resp)) < 0);
    }
  }

 private:
  DSHashTable hash_table_;
};

void do_work() {
  DSHashTable hash_table =
      nu::make_dis_hash_table<Key, Val, decltype(kFarmHashKeytoU64)>();
  std::cout << "start initing..." << std::endl;
  init(&hash_table);
  std::cout << "finish initing..." << std::endl;

  std::vector<nu::Future<void>> futures;
  nu::Proclet<Proxy> proxies[kNumProxys];
  for (uint32_t i = 0; i < kNumProxys; i++) {
    proxies[i] =
        nu::make_proclet<Proxy>(std::forward_as_tuple(hash_table), true);
    futures.emplace_back(proxies[i].run_async(&Proxy::run_loop));
  }
  futures.front().get();
}

int main(int argc, char **argv) {
  return nu::runtime_main_init(argc, argv, [](int, char **) { do_work(); });
}
