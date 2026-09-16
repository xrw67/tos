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

// TOS_ARRAYSIZE()
//
// Returns the number of elements in an array as a compile-time constant, which
// can be used in defining new arrays. If you use this macro on a pointer by
// mistake, you will get a compile-time error.
#define TOS_ARRAYSIZE(array) (sizeof(::tos::macros_internal::ArraySizeHelper(array)))

namespace tos {
namespace macros_internal {
// Note: this internal template function declaration is used by
// TOS_ARRAYSIZE. The function doesn't need a definition, as we only use its
// type.
template <typename T, std::size_t N>
auto ArraySizeHelper(const T (&array)[N]) -> char (&)[N];
}  // namespace macros_internal
}  // namespace tos

// TOS_BAD_CALL_IF()
//
// Used on a function overload to trap bad calls: any call that matches the
// overload will cause a compile-time error. This macro uses a clang-specific
// "enable_if" attribute, as described at
// https://clang.llvm.org/docs/AttributeReference.html#enable-if
//
// Overloads which use this macro should be bracketed by
// `#ifdef TOS_BAD_CALL_IF`.
//
// Example:
//
//   int isdigit(int c);
//   #ifdef TOS_BAD_CALL_IF
//   int isdigit(int c)
//     TOS_BAD_CALL_IF(c <= -1 || c > 255,
//                       "'c' must have the value of an unsigned char or EOF");
//   #endif // TOS_BAD_CALL_IF
#if TOS_HAVE_ATTRIBUTE(enable_if)
#define TOS_BAD_CALL_IF(expr, msg) \
    __attribute__((enable_if(expr, "Bad call trap"), unavailable(msg)))
#endif

// TOS_ASSERT()
//
// In C++11, `assert` can't be used portably within constexpr functions.
// TOS_ASSERT functions as a runtime assert but works in C++11 constexpr
// functions.  Example:
//
// constexpr double Divide(double a, double b) {
//   return TOS_ASSERT(b != 0), a / b;
// }
//
// This macro is inspired by
// https://akrzemi1.wordpress.com/2017/05/18/asserts-in-constexpr-functions/
#if defined(NDEBUG)
#define TOS_ASSERT(expr) (false ? static_cast<void>(expr) : static_cast<void>(0))
#else
#define TOS_ASSERT(expr) \
    (TOS_PREDICT_TRUE((expr)) ? static_cast<void>(0) : [] { assert(false && #expr); }())  // NOLINT
#endif

// `TOS_INTERNAL_HARDENING_ABORT()` controls how
// `TOS_HARDENING_ASSERT()` aborts the program in release mode (when NDEBUG
// is defined). The implementation should abort the program as quickly as
// possible and ideally it should not be possible to ignore the abort request.
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

// TOS_HARDENING_ASSERT()
//
// `TOS_HARDENING_ASSERT()` is like `TOS_ASSERT()`, but used to
// implement runtime assertions that should be enabled in hardened builds even
// when `NDEBUG` is defined.
//
// When `NDEBUG` is not defined, `TOS_HARDENING_ASSERT()` is identical to
// `TOS_ASSERT()`.
//
// Define TOS_OPTION_HARDENED as 1 to enable hardened assertions in release builds.
#if defined(TOS_OPTION_HARDENED) && TOS_OPTION_HARDENED == 1 && defined(NDEBUG)
#define TOS_HARDENING_ASSERT(expr) \
    (TOS_PREDICT_TRUE((expr)) ? static_cast<void>(0) : [] { TOS_INTERNAL_HARDENING_ABORT(); }())
#else
#define TOS_HARDENING_ASSERT(expr) TOS_ASSERT(expr)
#endif

// TOS_GUARDED_BY()
//
// Documents if a shared field or global variable needs to be protected by a
// mutex. TOS_GUARDED_BY() allows the user to specify a particular mutex that
// should be held when accessing the annotated variable.
//
// Although this annotation (and TOS_PT_GUARDED_BY, below) cannot be applied to
// local variables, a local variable and its associated mutex can often be
// combined into a small class or struct, thereby allowing the annotation.
//
// Example:
//
//   class Foo {
//     Mutex mu_;
//     int p1_ TOS_GUARDED_BY(mu_);
//     ...
//   };
#if TOS_HAVE_ATTRIBUTE(guarded_by)
#define TOS_GUARDED_BY(x) __attribute__((guarded_by(x)))
#else
#define TOS_GUARDED_BY(x)
#endif

// TOS_PT_GUARDED_BY()
//
// Documents if the memory location pointed to by a pointer should be guarded
// by a mutex when dereferencing the pointer.
//
// Example:
//   class Foo {
//     Mutex mu_;
//     int *p1_ TOS_PT_GUARDED_BY(mu_);
//     ...
//   };
//
// Note that a pointer variable to a shared memory location could itself be a
// shared variable.
//
// Example:
//
//   // `q_`, guarded by `mu1_`, points to a shared memory location that is
//   // guarded by `mu2_`:
//   int *q_ TOS_GUARDED_BY(mu1_) TOS_PT_GUARDED_BY(mu2_);
#if TOS_HAVE_ATTRIBUTE(pt_guarded_by)
#define TOS_PT_GUARDED_BY(x) __attribute__((pt_guarded_by(x)))
#else
#define TOS_PT_GUARDED_BY(x)
#endif

#endif  // TOS_BASE_MICROS_H_
