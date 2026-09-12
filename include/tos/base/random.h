#ifndef TOS_BASE_RANDOM_H_
#define TOS_BASE_RANDOM_H_

#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <new>
#include <random>
#include <string>
#include <string_view>
#include <utility>

#include "tos/base/result.h"

namespace tos {

/// ASCII decimal digits in ascending order.
inline constexpr std::string_view kRandomDigits = "0123456789";

/// ASCII lowercase letters in ascending order.
inline constexpr std::string_view kRandomLowercaseLetters = "abcdefghijklmnopqrstuvwxyz";

/// ASCII uppercase letters in ascending order.
inline constexpr std::string_view kRandomUppercaseLetters = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";

/// ASCII uppercase and lowercase letters.
inline constexpr std::string_view kRandomLetters =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";

/// ASCII uppercase and lowercase letters followed by decimal digits.
inline constexpr std::string_view kRandomAlphaNumeric =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";

namespace random_detail {

inline Result<std::mt19937_64*> GetThreadLocalGenerator() {
    thread_local std::unique_ptr<std::mt19937_64> generator;
    if (generator) {
        return generator.get();
    }

    try {
        std::random_device device;
        std::array<std::uint32_t, 8> seed_values{};
        for (std::uint32_t& value : seed_values) {
            value = device();
        }
        std::seed_seq seed(seed_values.begin(), seed_values.end());
        generator = std::make_unique<std::mt19937_64>(seed);
        return generator.get();
    } catch (const std::bad_alloc&) {
        throw;
    } catch (const std::exception& exception) {
        return Status(StatusCode::kUnavailable,
                      std::string("could not initialize random generator: ") + exception.what());
    } catch (...) {
        return Status(StatusCode::kUnavailable, "could not initialize random generator");
    }
}

}  // namespace random_detail

/// Generates an owning string of length random bytes, sampled uniformly from alphabet positions.
/// A nonzero length with an empty alphabet returns kInvalidArgument; zero length always succeeds.
/// Alphabet bytes, including NUL and repeated bytes, are used unchanged, so duplicate bytes
/// increase their sampling weight. The generator is thread-safe because each thread has independent
/// state. It is seeded from std::random_device but is not cryptographically secure and must not
/// generate tokens, keys, passwords, verification codes, or other security-sensitive values.
/// Random-source initialization failures return kUnavailable. Allocation exceptions propagate.
[[nodiscard]] inline Result<std::string> RandomString(
    std::size_t length, std::string_view alphabet = kRandomAlphaNumeric) {
    if (length == 0) {
        return std::string();
    }
    if (alphabet.empty()) {
        return Status(StatusCode::kInvalidArgument,
                      "alphabet must not be empty when generating random bytes");
    }

    auto generator = random_detail::GetThreadLocalGenerator();
    if (!generator) {
        return std::move(generator).status();
    }

    std::string result(length, '\0');
    std::uniform_int_distribution<std::size_t> distribution(0, alphabet.size() - 1);
    for (char& byte : result) {
        byte = alphabet[distribution(*generator.value())];
    }
    return result;
}

}  // namespace tos

#endif  // TOS_BASE_RANDOM_H_
