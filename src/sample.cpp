#include "AuEncoder.h"
#include "AuDecoder.h"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

using namespace std;

void encode() {
  std::ostringstream os;
  Au au(os);

  au.encode([](AuFormatter &f) { f.map(); });
  au.encode([](AuFormatter &f) { f.map("key1", "value1", "key2", -5000, "keyToIntern3", false); });
  au.encode([](AuFormatter &f) { f.array(6, 1, 0, -7, -2, 5.9, -5.9); });
  au.encode([](AuFormatter &f) { f.array(); });

  auto NaNs = [](AuFormatter &f) { f.array(
      std::numeric_limits<float>::quiet_NaN(),
      std::numeric_limits<double>::quiet_NaN(),
      std::numeric_limits<long double>::quiet_NaN(), // converted to double
      0 / 0.0,
      std::sqrt(-1)
  ); };

  au.encode([&](AuFormatter &f) {
    f.map("NaNs", [&]() {
      NaNs(f);
    });
  });

  //au.encode([](AuFormatter &f) { f.value(-500); f.value(3987); }); // Invalid

  au.clearDictionary(false);
  au.encode([](AuFormatter &f) { f.map("key1", "value1", "key2", -5000, "keyToIntern3", false); });
  au.encode([](AuFormatter &f) {
    f.map("RepeatedVals",
      f.arrayVals([&]() {
        for (auto i = 0; i < 12; ++i) {
          f.value("valToIntern");
        }
      })
    );
  });
  au.encode([](AuFormatter &f) { f.value("valToIntern"); });

  cout << os.str();
}

/**
 * Pass the output through `od -tcz -tu1` to view the encoding.
 *
 * @param argc
 * @param argv
 * @return
 */
int main(int argc, const char **argv) {
  encode();
  return 0;
}