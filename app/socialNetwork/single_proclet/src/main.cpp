#include <iostream>
#include <memory>
#include <nu/proclet.hpp>
#include <nu/runtime.hpp>

/*dxy++*/
#include <atomic>
#include <thread>
#include <chrono>

#include "ThriftBackEndServer.hpp"

/*dxy++*/
extern std::atomic_uint64_t nu_call_count;
extern std::atomic_uint64_t nu_call_lc;
extern std::atomic_uint64_t nu_call_rt;
extern std::atomic_uint64_t nu_call_rt_cachemiss;
extern std::atomic_uint64_t nu_commu_time;

constexpr uint32_t kNumEntries = 1;

using namespace social_network;

class ServiceEntry {
public:
  ServiceEntry(States states) {
    json config_json;

    BUG_ON(LoadConfigFile("config/service-config.json", &config_json) != 0);

    auto port = config_json["back-end-service"]["port"];
    std::cout << "port = " << port << std::endl;
    std::shared_ptr<TServerSocket> server_socket =
        std::make_shared<TServerSocket>("0.0.0.0", port);

    states.secret = config_json["secret"];
    auto back_end_handler =
        std::make_shared<ThriftBackEndServer>(std::move(states));

    TThreadedServer server(
        std::make_shared<BackEndServiceProcessor>(std::move(back_end_handler)),
        server_socket, std::make_shared<TFramedTransportFactory>(),
        std::make_shared<TBinaryProtocolFactory>());
    std::cout << "Starting the ThriftBackEndServer..." << std::endl;
    server.serve();
  }
};

void DoWork() {
  /*dxy++*/
  uint64_t t_s = microtime();
  std::thread([t_s] {
    while (true) {
      enum {
        kPrintInterval = 5,
      };
      //uint64_t cur_call_lc = nu_call_lc.load();
      //uint64_t cur_call_rt = nu_call_rt.load();
      //uint64_t cur_rt_cachemiss = nu_call_rt_cachemiss.load();
      std::this_thread::sleep_for(std::chrono::seconds(kPrintInterval));
      std::cout << std::endl;
      std::cout << "call_local: " << nu_call_count << std::endl;
      uint64_t time_ = microtime() - t_s;
      std::cout << "time: " << time_ << std::endl;
      std::cout << "commu_ratio: " << 1.0 * nu_commu_time / time_ << std::endl;
      //std::cout << "call_local: " << cur_call_lc << "(" << 1.0 * cur_call_lc / cur_call_count << ")" << std::endl;
      //std::cout << "call_remote: " << cur_call_rt << "(" << 1.0 * cur_call_rt / cur_call_count << ")" << std::endl;
      //std::cout << "call_cache_miss: " << cur_rt_cachemiss << "(" << 1.0 * cur_rt_cachemiss / cur_call_rt << ")" << std::endl;
      std::cout << std::endl;
    }
  }).detach();

  
  auto states = make_states();

  std::vector<nu::Future<nu::Proclet<ServiceEntry>>> thrift_futures;
  for (uint32_t i = 0; i < kNumEntries; i++) {
    thrift_futures.emplace_back(nu::make_proclet_async<ServiceEntry>(
        std::forward_as_tuple(states), true));
  }
}

int main(int argc, char **argv) {
  return nu::runtime_main_init(argc, argv, [](int, char **) { DoWork(); });
}
