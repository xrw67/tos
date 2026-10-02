#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <gtest/gtest.h>
#include <limits>
#include <new>
#include <string>
#include <utility>

#include "test_util.h"
#include "tos/base/filesystem.h"
#include "tos/base/result.h"
#include "tos/base/scope_exit.h"
#include "tos/base/status.h"

namespace {

struct AllocationCounts {
    // Allocation hooks update these indirectly; force observations at each read
    // even when the compiler optimizes new expressions in the measured code.
    volatile std::size_t allocations = 0;
    volatile std::size_t deallocations = 0;
    volatile std::size_t attempts = 0;
    std::size_t fail_after = std::numeric_limits<std::size_t>::max();
};

thread_local AllocationCounts* active_counts = nullptr;

class CountScope {
   public:
    explicit CountScope(AllocationCounts& counts) : previous_(active_counts) {
        active_counts = &counts;
    }
    ~CountScope() { active_counts = previous_; }
    CountScope(const CountScope&) = delete;
    CountScope& operator=(const CountScope&) = delete;

   private:
    AllocationCounts* previous_;
};

}  // namespace

// This executable alone replaces ordinary allocations. Assertions run outside
// CountScope, so GoogleTest's diagnostic allocations are not part of the sample.
void* operator new(std::size_t size) {
    if (active_counts && active_counts->attempts++ == active_counts->fail_after) {
        throw std::bad_alloc();
    }
    for (;;) {
        if (void* memory = std::malloc(size == 0 ? 1 : size)) {
            if (active_counts) {
                ++active_counts->allocations;
            }
            return memory;
        }
        const auto handler = std::get_new_handler();
        if (!handler) {
            throw std::bad_alloc();
        }
        handler();
    }
}

void operator delete(void* memory) noexcept {
    if (memory && active_counts) {
        ++active_counts->deallocations;
    }
    std::free(memory);
}

void operator delete(void* memory, std::size_t) noexcept { ::operator delete(memory); }

void* operator new[](std::size_t size) { return ::operator new(size); }

void operator delete[](void* memory) noexcept { ::operator delete(memory); }

void operator delete[](void* memory, std::size_t) noexcept { ::operator delete(memory); }

// Runtime interceptors may otherwise implement nothrow new independently of our ordinary
// replacements, then pair it with our free-backed delete. Keep every ordinary overload paired.
void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
    try {
        return ::operator new(size);
    } catch (...) {
        return nullptr;
    }
}

void* operator new[](std::size_t size, const std::nothrow_t& tag) noexcept {
    return ::operator new(size, tag);
}

void operator delete(void* memory, const std::nothrow_t&) noexcept { ::operator delete(memory); }

void operator delete[](void* memory, const std::nothrow_t&) noexcept { ::operator delete(memory); }

namespace tos {
namespace {

TEST(ScopeExitAllocationTest, CreationMoveReleaseAndCleanupDoNotAllocate) {
    AllocationCounts counts;
    counts.fail_after = 0;
    int cleaned = 0;
    {
        CountScope scope(counts);
        auto first = MakeScopeExit([&] { ++cleaned; });
        auto moved = std::move(first);
        auto released = MakeScopeExit([&] { ++cleaned; });
        released.Release();
    }
    EXPECT_EQ(cleaned, 1);
    EXPECT_EQ(counts.attempts, 0u);
    EXPECT_EQ(counts.allocations, 0u);
}

TEST(ResourceAllocationTest, AtomicWriterAllocationFailuresLeaveNoTemporaryFiles) {
    test::TemporaryDirectory directory("tos-atomic-allocation-");
    auto destination = Path::Parse((directory.path() / "destination").u8string());
    ASSERT_TRUE(destination);
    bool succeeded = false;
    std::size_t failures = 0;
    for (std::size_t fail_after = 0; fail_after < 128; ++fail_after) {
        AllocationCounts counts;
        counts.fail_after = fail_after;
        {
            CountScope scope(counts);
            try {
                auto writer = AtomicFileWriter::Create(destination.value());
                succeeded = writer.ok();
            } catch (const std::bad_alloc&) {
                ++failures;
            }
        }
        EXPECT_TRUE(std::filesystem::is_empty(directory.path())) << fail_after;
        if (succeeded) break;
    }
    EXPECT_TRUE(succeeded);
    EXPECT_GT(failures, 0U);
}

TEST(StatusAllocationTest, SuccessDoesNotAllocateEvenWhenAllocationFails) {
    std::string message(1024, 'x');
    AllocationCounts counts;
    counts.fail_after = 0;
    bool valid = true;
    {
        CountScope scope(counts);
        const Status initial;
        const Status from_code(StatusCode::kOk);
        const Status success(StatusCode::kOk, message);
        const Status ignored(StatusCode::kOk, std::move(message));
        valid = initial.ok() && from_code.ok() && success.ok() && ignored.ok();
    }
    EXPECT_TRUE(valid);
    EXPECT_EQ(message.size(), 1024u);
    EXPECT_EQ(counts.allocations, 0u);
    EXPECT_EQ(counts.deallocations, 0u);
}

TEST(StatusAllocationTest, MessageMovesDoNotAllocateOrReleaseTransferredStorage) {
    for (const auto text : {"short", "a longer message that exceeds inline string storage"}) {
        Status original(StatusCode::kInternal, text);
        const char* data = original.message().data();
        Status assigned;
        AllocationCounts counts;
        counts.fail_after = 0;
        bool transferred = false;
        {
            CountScope scope(counts);
            Status moved(std::move(original));
            assigned = std::move(moved);
            transferred = assigned.message().data() == data;
        }
        EXPECT_TRUE(transferred);
        EXPECT_EQ(counts.allocations, 0u);
        EXPECT_EQ(counts.deallocations, 0u);
    }
}

TEST(StatusAllocationTest, ResultStatusBorrowsAndMovesDoNotAllocate) {
    Result<int> result = Status(StatusCode::kInternal, std::string(1024, 'x'));
    const Result<int>& const_result = result;
    Status extracted;
    AllocationCounts counts;
    counts.fail_after = 0;
    bool valid = false;
    {
        CountScope scope(counts);
        const Status& from_lvalue = result.status();
        const Status& from_const = const_result.status();
        const char* data = from_lvalue.message().data();
        valid = std::addressof(from_lvalue) == std::addressof(from_const);
        extracted = std::move(result).status();
        valid = valid && extracted.message().data() == data;
    }
    EXPECT_TRUE(valid);
    EXPECT_EQ(counts.allocations, 0u);
    EXPECT_EQ(counts.deallocations, 0u);
}

TEST(StatusAllocationTest, RvalueMessageTransfersBufferWithOnlyRepresentationAllocation) {
    std::string message(1024, 'x');
    const char* data = message.data();
    Status error;
    AllocationCounts counts;
    counts.fail_after = 1;
    {
        CountScope scope(counts);
        error = Status(StatusCode::kInternal, std::move(message));
    }
    EXPECT_EQ(error.message().data(), data);
    EXPECT_EQ(error.message(), std::string(1024, 'x'));
    EXPECT_EQ(counts.allocations, 1u);
    EXPECT_EQ(counts.deallocations, 0u);
}

TEST(StatusAllocationTest, RepresentationAllocationFailureDoesNotConsumeRvalueMessage) {
    std::string message(1024, 'x');
    Status original(StatusCode::kNotFound, "original");
    AllocationCounts counts;
    counts.fail_after = 0;
    bool failed = false;
    {
        CountScope scope(counts);
        try {
            original = Status(StatusCode::kInternal, std::move(message));
        } catch (const std::bad_alloc&) {
            failed = true;
        }
    }
    EXPECT_TRUE(failed);
    EXPECT_EQ(original, Status(StatusCode::kNotFound, "original"));
    EXPECT_EQ(message, std::string(1024, 'x'));
    EXPECT_EQ(counts.allocations, 0u);
    EXPECT_EQ(counts.deallocations, 0u);
}

TEST(StatusAllocationTest, MessageAllocationFailureReleasesPartiallyConstructedRepresentation) {
    const std::string message(1024, 'x');
    Status original(StatusCode::kNotFound, "original");
    AllocationCounts counts;
    counts.fail_after = 1;
    bool failed = false;
    {
        CountScope scope(counts);
        try {
            original = Status(StatusCode::kInternal, message);
        } catch (const std::bad_alloc&) {
            failed = true;
        }
    }
    EXPECT_TRUE(failed);
    EXPECT_EQ(original, Status(StatusCode::kNotFound, "original"));
    EXPECT_EQ(counts.allocations, 1u);
    EXPECT_EQ(counts.deallocations, 1u);
}

}  // namespace
}  // namespace tos
