#ifndef TWODFX_PORTABLE_SUPPORT_H
#define TWODFX_PORTABLE_SUPPORT_H
#include <stdint.h>
#define TWODFX_ASSERT_JOIN_(a,b) a##b
#define TWODFX_ASSERT_JOIN(a,b) TWODFX_ASSERT_JOIN_(a,b)
#define TWODFX_STATIC_ASSERT(condition,message) \
  typedef char TWODFX_ASSERT_JOIN(twodfx_static_assert_,__LINE__)[(condition)?1:-1]
#include <stdbool.h>
uint32_t time_us_32(void);
uint32_t get_rand_32(void);
#endif
