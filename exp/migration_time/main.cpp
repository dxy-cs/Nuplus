#include <algorithm>
#include <cstdint>
#include <iostream>
#include <numeric>
#include <vector>

extern "C" {
#include <base/time.h>
#include <net/ip.h>
#include <runtime/runtime.h>
}
#include <runtime.h>

#include "nu/pressure_handler.hpp"
#include "nu/proclet.hpp"
#include "nu/runtime.hpp"

constexpr uint32_t kObjSize = 65536;
constexpr uint32_t kNumObjs = 1024;

class Obj {
public:
  uint32_t get_ip() { return get_cfg_ip(); }
private:
  uint8_t bytes[kObjSize];
};

class Migrator {
 public:
  void migrate() { nu::get_runtime()->pressure_handler()->mock_set_pressure(); }
};

int main(int argc, char **argv) {
  // main proclet runs in 10.10.1.2 (caladan ip 18.18.1.3)
  return nu::runtime_main_init(argc, argv, [](int, char **) {
    auto l_ip = MAKE_IP_ADDR(18, 18, 1, 2);
    std::vector<nu::Proclet<Obj>> objs;
    // make 1024 proclet in real ip 10.10.1.1 (caladan ip 18.18.1.2)  
    for (uint32_t i = 0; i < kNumObjs; i++) {
      objs.emplace_back(nu::make_proclet<Obj>(false, std::nullopt, l_ip));
    }

    timer_sleep(1000*1000);
    // traverse invocate all proclet, which expect to have 1024 remotepath logs
    for (auto &obj : objs) {
        std::cout << "round1==obj_ip:" << obj.run(&Obj::get_ip)  << std::endl;
    }

    // migrate 1024 proclets to real ip 10.10.1.2 (caladan ip 18.18.1.3)
    auto migrator = nu::make_proclet<Migrator>(true, std::nullopt, l_ip);
    migrator.run(&Migrator::migrate);

    timer_sleep(1000 * 1000);
    // traverse invocate all proclet, which expect to have 1024 localpath logs
    for (auto &obj : objs) {
        std::cout << "round2==obj_ip:" << obj.run(&Obj::get_ip)  << std::endl;
    }

    std::cout << "Exiting..." << std::endl;
  });
}
