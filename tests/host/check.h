#pragma once

#include <cstdio>
#include <cstring>

namespace check {

inline int &failures() {
  static int count = 0;
  return count;
}

inline int &checks() {
  static int count = 0;
  return count;
}

}  // namespace check

#define CHECK(cond) \
  do { \
    ++check::checks(); \
    if (!(cond)) { \
      ++check::failures(); \
      std::printf("%s:%d: CHECK(%s) fehlgeschlagen\n", __FILE__, __LINE__, #cond); \
    } \
  } while (0)

#define CHECK_EQ(a, b) \
  do { \
    ++check::checks(); \
    const long long check_a = static_cast<long long>(a); \
    const long long check_b = static_cast<long long>(b); \
    if (check_a != check_b) { \
      ++check::failures(); \
      std::printf("%s:%d: %s == %s fehlgeschlagen (%lld != %lld)\n", __FILE__, __LINE__, #a, #b, check_a, check_b); \
    } \
  } while (0)

#define CHECK_STR_CONTAINS(haystack, needle) \
  do { \
    ++check::checks(); \
    if (std::strstr((haystack), (needle)) == nullptr) { \
      ++check::failures(); \
      std::printf("%s:%d: \"%s\" enthält \"%s\" nicht\n", __FILE__, __LINE__, (haystack), (needle)); \
    } \
  } while (0)
