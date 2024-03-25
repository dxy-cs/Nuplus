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
#include <atomic>
#include <thread>
#include <chrono>

#include <runtime.h>

#include "nu/dis_hash_table.hpp"
#include "nu/proclet.hpp"
#include "nu/runtime.hpp"
#include "nu/utils/farmhash.hpp"
#include "nu/utils/thread.hpp"

/*dxy++*/
static std::atomic_uint64_t count = 0;
static std::atomic_uint64_t sum_time = 0; 
extern std::atomic_uint64_t nu_call_count;
extern std::atomic_uint64_t nu_call_lc;
extern std::atomic_uint64_t nu_call_rt;
extern std::atomic_uint64_t nu_call_rt_cachemiss;

constexpr uint32_t kKeyLen = 20;
constexpr uint32_t kValLen = 2;
constexpr double kLoadFactor = 0.30;
constexpr uint32_t kNumProxies = 8;
constexpr uint32_t kProxyPort = 10086;

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

  template <class Archive> void serialize(Archive &ar) {
    ar(cereal::binary_data(data, sizeof(data)));
  }
};

struct Req {
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
      std::random_device rd;
      std::mt19937 mt(rd());
      std::uniform_int_distribution<int> dist('A', 'z');
      auto num_pairs = kNumPairs / kNumThreads;
      for (size_t j = 0; j < num_pairs; j++) {
        Key key;
        Val val;
        random_str(dist, mt, kKeyLen, key.data);
        random_str(dist, mt, kValLen, val.data);
        hash_table->put(key, val);
      }
    });
  }
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
      //if (req.end_of_req) [[unlikely]] {
      //  std::cout << "count:" << count << std::endl;
      //  std::cout << "sum_time: " << sum_time / 3300 << std::endl;  // us
      //  BUG_ON(c->WriteFull(&resp, sizeof(resp)) < 0);
      //  break;
      //}
      bool is_local;
      /*dxy++*/
      uint64_t s1 = rdtsc1();
      auto optional_v = hash_table_.get_with_profile2(req.key, &is_local);
      uint64_t e1 = rdtsc1();
      sum_time += (e1 - s1);
      count ++;

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
  init(&hash_table);

  /*dxy++*/
  std::thread([] {
    uint64_t tag = 0;
    while (true) {
      enum {
        kPrintThreshold = 100,
        kPrintInterval = 1,
      };
      uint64_t cur_count = count.load();
      uint64_t cur_time = sum_time.load();
      uint64_t cur_call_count = nu_call_count.load();
      uint64_t cur_call_lc = nu_call_lc.load();
      uint64_t cur_call_rt = nu_call_rt.load();
      uint64_t cur_rt_cachemiss = nu_call_rt_cachemiss.load();
      if ((cur_count - tag) >= kPrintThreshold) {
        tag = cur_count;
        std::cout << "count:" << cur_count << std::endl;
        std::cout << "sum_time: " << 1.0 * cur_time / 3300 << std::endl;  // us
        std::cout << "us/req: " << (1.0 * cur_time / 3300) / count << std::endl;
        std::cout << "call_local: " << cur_call_lc << "(" << 1.0 * cur_call_lc / cur_call_count << ")" << std::endl;
        std::cout << "call_remote: " << cur_call_rt << "(" << 1.0 * cur_call_rt / cur_call_count << ")" << std::endl;
        std::cout << "call_cache_miss: " << cur_rt_cachemiss << "(" << 1.0 * cur_rt_cachemiss / cur_call_rt << ")" << std::endl;
      } else {
        std::this_thread::sleep_for(std::chrono::seconds(kPrintInterval));
      }
    }
  }).detach();

  std::vector<nu::Future<void>> futures;
  nu::Proclet<Proxy> proxies[kNumProxies];
  for (uint32_t i = 0; i < kNumProxies; i++) {
    proxies[i] =
        nu::make_proclet<Proxy>(std::forward_as_tuple(hash_table), true);
    futures.emplace_back(proxies[i].run_async(&Proxy::run_loop));
  }
  std::cout << "finish initing..." << std::endl;
  futures.front().get();
}

int main(int argc, char **argv) {
  return nu::runtime_main_init(argc, argv, [](int, char **) { do_work(); });
}
