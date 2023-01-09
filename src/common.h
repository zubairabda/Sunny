#include <stdint.h>
//#include <stdatomic.h>

typedef uint8_t     u8;
typedef uint16_t    u16;
typedef uint32_t    u32;
typedef uint64_t    u64;
typedef int8_t      s8;
typedef int16_t     s16;
typedef int32_t     s32;
typedef int64_t     s64;
typedef float       f32;
typedef double      f64;
typedef s8          b8;
typedef s16         b16;
typedef s32         b32;

#if SY_DEBUG
#define SY_ASSERT(expr) do { if (!(expr)) { printf("[ASSERT]: expression: '%s' at %s:%d, did not evalulate\n", #expr, __FILE__, __LINE__); volatile int* p = 0; *p = 0; } } while(0)
#define SY_INVALID_CASE default: SY_ASSERT(0); break
#else
#define SY_ASSERT(expr)
#define SY_INVALID_CASE
#endif
#define ARRAYCOUNT(arr) (sizeof(arr) / sizeof(arr[0]))

#define MEMBERSIZE(type, element) sizeof(((type*)0)->element)

#define NoImplementation

#ifdef EXPORT_LIB
#define SUNNY_API __declspec(dllexport)
#else
#define SUNNY_API __declspec(dllimport)
#endif

#define kilobytes(val) ((val) * 1024u)
#define megabytes(val) (kilobytes(val) * 1024u)

#define SY_TRUE     1u
#define SY_FALSE    0u