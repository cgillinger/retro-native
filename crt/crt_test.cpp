// Unit check of the Auto rule and presets. Build: c++ -std=c++17 -Wall -Wextra -Werror crt_test.cpp crt.cpp
#include <cstdio>
#include "crt.h"

static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); ++fails; } } while (0)

int main() {
  CHECK(crt_auto_mode(1.0) == CrtMode::Sharp);
  CHECK(crt_auto_mode(3.4999) == CrtMode::Sharp);
  CHECK(crt_auto_mode(3.5) == CrtMode::Crt);
  CHECK(crt_auto_mode(4.82) == CrtMode::Crt);
  CHECK(crt_auto_mode(5.9999) == CrtMode::Crt);
  CHECK(crt_auto_mode(6.0) == CrtMode::CrtMask);
  CHECK(crt_auto_mode(6.43) == CrtMode::CrtMask);
  CHECK(crt_resolve(CrtSetting::Sharp, 10) == CrtMode::Sharp);
  CHECK(crt_resolve(CrtSetting::Crt, 1) == CrtMode::Crt);
  CHECK(crt_resolve(CrtSetting::CrtMask, 1) == CrtMode::CrtMask);
  CHECK(crt_resolve(CrtSetting::Auto, 4.0) == CrtMode::Crt);
  CHECK(crt_params(CrtMode::Crt).maskStrength == 0.0f);
  CHECK(crt_params(CrtMode::CrtMask).maskStrength > 0.0f);
  if (!fails) std::puts("crt_test: OK");
  return fails != 0;
}
