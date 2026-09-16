#ifndef TOS_BASE_MICROS_H_
#define TOS_BASE_MICROS_H_

#include <cassert>
#include <cstddef>
#include <cstdlib>

// Feature-test wrappers may be overridden by build configuration when needed.
#ifndef TOS_HAVE_ATTRIBUTE
#if defined(__has_attribute)
#define TOS_HAVE_ATTRIBUTE(attribute) __has_attribute(attribute)
#else
#define TOS_HAVE_ATTRIBUTE(attribute) 0
#endif
#endif

#ifndef TOS_HAVE_BUILTIN
#if defined(__has_builtin)
#define TOS_HAVE_BUILTIN(builtin) __has_builtin(builtin)
#else
#define TOS_HAVE_BUILTIN(builtin) 0
#endif
#endif

// TOS_BLOCK_TAIL_CALL_OPTIMIZATION
#if defined(__pnacl__)
#define TOS_BLOCK_TAIL_CALL_OPTIMIZATION() \
    if (volatile int x = 0) {              \
        (void)x;                           \
    }
#elif defined(__clang__) || defined(__GNUC__)
#define TOS_BLOCK_TAIL_CALL_OPTIMIZATION() __asm__ __volatile__("")
#elif defined(_MSC_VER)
#include <intrin.h>
#define TOS_BLOCK_TAIL_CALL_OPTIMIZATION() __nop()
#else
#define TOS_BLOCK_TAIL_CALL_OPTIMIZATION() \
    if (volatile int x = 0) {              \
        (void)x;                           \
    }
#endif

// Cache-line alignment is implementation-defined; verify its effect before using it in hot code.
#if defined(__GNUC__)
#if defined(__i386__) || defined(__x86_64__) || defined(__aarch64__)
#define TOS_CACHELINE_SIZE 64
#elif defined(__powerpc64__)
#define TOS_CACHELINE_SIZE 128
#elif defined(__ARM_ARCH_5T__)
#define TOS_CACHELINE_SIZE 32
#elif defined(__ARM_ARCH_7A__)
#define TOS_CACHELINE_SIZE 64
#endif
#ifndef TOS_CACHELINE_SIZE
#define TOS_CACHELINE_SIZE 64
#endif
#define TOS_CACHELINE_ALIGNED __attribute__((aligned(TOS_CACHELINE_SIZE)))
#elif defined(_MSC_VER)
#define TOS_CACHELINE_SIZE 64
#define TOS_CACHELINE_ALIGNED __declspec(align(TOS_CACHELINE_SIZE))
#else
#define TOS_CACHELINE_SIZE 64
#define TOS_CACHELINE_ALIGNED
#endif

// TOS_PREDICT_TRUE and TOS_PREDICT_FALSE are branch-prediction hints.
#if TOS_HAVE_BUILTIN(__builtin_expect) || (defined(__GNUC__) && !defined(__clang__))
#define TOS_PREDICT_FALSE(x) (__builtin_expect(false || (x), false))
#define TOS_PREDICT_TRUE(x) (__builtin_expect(false || (x), true))
#else
#define TOS_PREDICT_FALSE(x) (x)
#define TOS_PREDICT_TRUE(x) (x)
#endif

// TOS_UNUSED()
#define TOS_UNUSED(expr) \
    do {                 \
        (void)(expr);    \
    } while (0)  // 消除未使用参数的警报

// TOS_ARRAYSIZE returns an array's compile-time element count and rejects pointers.
#define TOS_ARRAYSIZE(array) (sizeof(::tos::macros_internal::ArraySizeHelper(array)))

namespace tos {
namespace macros_internal {
// Used only for TOS_ARRAYSIZE's type-based check.
template <typename T, std::size_t N>
auto ArraySizeHelper(const T (&array)[N]) -> char (&)[N];
}  // namespace macros_internal
}  // namespace tos

// TOS_BAD_CALL_IF uses Clang's enable_if attribute to reject matching overloads.
#if TOS_HAVE_ATTRIBUTE(enable_if)
#define TOS_BAD_CALL_IF(expr, msg) \
    __attribute__((enable_if(expr, "Bad call trap"), unavailable(msg)))
#endif

// TOS_ASSERT is a constexpr-compatible assertion disabled by NDEBUG.
#if defined(NDEBUG)
#define TOS_ASSERT(expr) (false ? static_cast<void>(expr) : static_cast<void>(0))
#else
#define TOS_ASSERT(expr) \
    (TOS_PREDICT_TRUE((expr)) ? static_cast<void>(0) : [] { assert(false && #expr); }())  // NOLINT
#endif

// TOS_INTERNAL_HARDENING_ABORT terminates a hardened release assertion failure.
#if (TOS_HAVE_BUILTIN(__builtin_trap) && TOS_HAVE_BUILTIN(__builtin_unreachable)) || \
    (defined(__GNUC__) && !defined(__clang__))
#define TOS_INTERNAL_HARDENING_ABORT() \
    do {                               \
        __builtin_trap();              \
        __builtin_unreachable();       \
    } while (false)
#else
#define TOS_INTERNAL_HARDENING_ABORT() abort()
#endif

// TOS_HARDENING_ASSERT remains active in release builds when TOS_OPTION_HARDENED is 1.
#if defined(TOS_OPTION_HARDENED) && TOS_OPTION_HARDENED == 1 && defined(NDEBUG)
#define TOS_HARDENING_ASSERT(expr) \
    (TOS_PREDICT_TRUE((expr)) ? static_cast<void>(0) : [] { TOS_INTERNAL_HARDENING_ABORT(); }())
#else
#define TOS_HARDENING_ASSERT(expr) TOS_ASSERT(expr)
#endif

// TOS_GUARDED_BY documents the mutex protecting a shared variable.
#if TOS_HAVE_ATTRIBUTE(guarded_by)
#define TOS_GUARDED_BY(x) __attribute__((guarded_by(x)))
#else
#define TOS_GUARDED_BY(x)
#endif

// TOS_PT_GUARDED_BY documents the mutex protecting a pointer's target.
#if TOS_HAVE_ATTRIBUTE(pt_guarded_by)
#define TOS_PT_GUARDED_BY(x) __attribute__((pt_guarded_by(x)))
#else
#define TOS_PT_GUARDED_BY(x)
#endif

#endif  // TOS_BASE_MICROS_H_
