#pragma once

#include <cstdint>
#include <limits>
#include <random>
#include <vector>

namespace nu {

//
// Example usage:
//
//  std::random_device rd;
//  std::mt19937 gen(rd());
//  zipf_distribution zipf(300, 1.0);
//
//  for (int i = 0; i < 100; i++)
//    cout << zipf(gen) << endl;

class zipf_distribution {
 public:
  // Zipf distribution for `num` items, in the range `[0, num - 1]` inclusive.
  // The distribution follows the power-law 1/n^q with exponent `q`.
  zipf_distribution(uint64_t num, double q);
  // Returns the random result.
  uint64_t operator()(std::mt19937 &rng);
  // Returns the minimum value potentially generated by the distribution.
  uint64_t min() const;
  // Returns the maximum value potentially generated by the distribution.
  uint64_t max() const;

 private:
  std::vector<double> pdf_;                    // Prob. distribution
  uint64_t num_;                               // Number of elements
  double q_;                                   // Exponent
  std::discrete_distribution<uint64_t> dist_;  // Draw generator
};
}  // namespace nu