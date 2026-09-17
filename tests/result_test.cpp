#include "tos/base/result.h"

#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

#include <gtest/gtest.h>

namespace tos {
namespace {

struct NoDefault {
    explicit NoDefault(int number) : number(number) {}
    int number;
};

struct AcceptsAnything {
    template <typename U>
    explicit AcceptsAnything(U&&) {}
};

struct AddressTrap {
    AddressTrap* operator&() { return nullptr; }
    const AddressTrap* operator&() const { return nullptr; }
    int number = 42;
};

struct ThrowingValue {
    explicit ThrowingValue(int number) : number(number) {}

    ThrowingValue(const ThrowingValue& other)
        : number(other.number), fail_copy(other.fail_copy), fail_move(other.fail_move) {
        if (fail_copy) {
            throw std::runtime_error("copy failed");
        }
    }

    ThrowingValue(ThrowingValue&& other)
        : number(other.number), fail_copy(other.fail_copy), fail_move(other.fail_move) {
        if (fail_move) {
            throw std::runtime_error("move failed");
        }
        other.number = -1;
    }

    ThrowingValue& operator=(ThrowingValue&& other) {
        if (other.fail_move) {
            throw std::runtime_error("move assignment failed");
        }
        number = other.number;
        fail_copy = other.fail_copy;
        fail_move = other.fail_move;
        other.number = -1;
        return *this;
    }

    int number;
    bool fail_copy = false;
    bool fail_move = false;
};

using StringResult = Result<std::string>;
using PointerResult = Result<std::unique_ptr<int>>;

TEST(ResultTest, ConstructsSuccessFromValue) {
    const Result<int> result = 42;
    EXPECT_TRUE(result.ok());
    EXPECT_TRUE(static_cast<bool>(result));
    EXPECT_EQ(result.value(), 42);
    EXPECT_EQ(result.status(), Status::Ok());
}

TEST(ResultTest, OwnsCopiedValue) {
    std::string original = "original";
    const StringResult result = original;
    original.assign("changed");
    EXPECT_EQ(*result, "original");
}

TEST(ResultTest, SupportsImplicitValueAndErrorReturns) {
    const auto load = [](bool exists) -> StringResult {
        if (!exists) {
            Status failure(StatusCode::kNotFound, "configuration missing");
            return failure;
        }
        return "configuration";
    };

    EXPECT_EQ(load(true).value(), "configuration");
    EXPECT_EQ(load(false).status(), Status(StatusCode::kNotFound, "configuration missing"));
}

TEST(ResultTest, MovesFailureStatusIntoResult) {
    Status original(StatusCode::kTimeout, "request timed out");
    const char* data = original.message().data();
    const Result<int> result = std::move(original);
    EXPECT_TRUE(original.ok());

    EXPECT_FALSE(result.ok());
    EXPECT_FALSE(static_cast<bool>(result));
    EXPECT_EQ(result.status(), Status(StatusCode::kTimeout, "request timed out"));
    EXPECT_EQ(result.status().message().data(), data);
}

TEST(ResultTest, ExtractsOwningLongFailureStatusFromRvalues) {
    StringResult result = Status(StatusCode::kNotFound, std::string(1024, 'x'));
    const Status extracted = std::move(result).status();
    EXPECT_EQ(extracted.message(), std::string(1024, 'x'));
    EXPECT_EQ(extracted.code(), StatusCode::kNotFound);
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.status().code(), StatusCode::kInternal);
    EXPECT_EQ(result.status().message(), "Result error has been moved");
    EXPECT_EQ(std::move(result).status(), result.status());
    EXPECT_THROW(static_cast<void>(result.value()), std::logic_error);

    result = "recovered";
    EXPECT_EQ(extracted.message(), std::string(1024, 'x'));
}

TEST(ResultTest, ReturnedStatusesOutliveTemporaryResults) {
    const auto make_failure = []() -> StringResult {
        return Status(StatusCode::kNotFound, std::string(1024, 'x'));
    };
    const Status& temporary_status = make_failure().status();
    EXPECT_EQ(temporary_status.message(), std::string(1024, 'x'));
    EXPECT_EQ(temporary_status.code(), StatusCode::kNotFound);
}

TEST(ResultTest, SuccessStatusAccessDoesNotConsumeTheValue) {
    StringResult result = "value";
    const StringResult& const_result = result;
    EXPECT_EQ(result.status(), Status::Ok());
    EXPECT_EQ(const_result.status(), Status::Ok());
    EXPECT_EQ(std::move(result).status(), Status::Ok());
    EXPECT_EQ(result.value(), "value");
}

TEST(ResultTest, RejectsSuccessStatusConstructionAndAssignment) {
    Status success;
    EXPECT_THROW(static_cast<void>(Result<int>(std::move(success))), std::invalid_argument);
    EXPECT_THROW(static_cast<void>(Result<int>(Status::Ok())), std::invalid_argument);

    Result<int> result = 42;
    EXPECT_THROW(result = Status::Ok(), std::invalid_argument);
    EXPECT_EQ(result.value(), 42);
}

TEST(ResultTest, DoesNotTreatStatusOrResultAsAForwardedValue) {
    Status failure(StatusCode::kInternal, "failure");
    Result<AcceptsAnything> result = std::move(failure);
    EXPECT_EQ(result.status(), Status(StatusCode::kInternal, "failure"));
    const Result<AcceptsAnything> moved(std::move(result));
    EXPECT_EQ(moved.status(), Status(StatusCode::kInternal, "failure"));
    EXPECT_THROW(static_cast<void>(Result<AcceptsAnything>(Status::Ok())), std::invalid_argument);
}

TEST(ResultTest, ValueAccessPreservesReferences) {
    StringResult result = "value";
    const StringResult& const_result = result;
    const std::string* address = std::addressof(result.value());

    EXPECT_EQ(std::addressof(const_result.value()), address);
    EXPECT_EQ(result.operator->(), address);
    EXPECT_EQ(const_result.operator->(), address);
    EXPECT_EQ(std::addressof(*result), address);
    EXPECT_EQ(std::addressof(*const_result), address);

    auto&& moved_value = std::move(result).value();
    auto&& const_moved_value = std::move(const_result).value();
    auto&& moved_dereference = *std::move(result);
    auto&& const_moved_dereference = *std::move(const_result);
    EXPECT_EQ(std::addressof(moved_value), address);
    EXPECT_EQ(std::addressof(const_moved_value), address);
    EXPECT_EQ(std::addressof(moved_dereference), address);
    EXPECT_EQ(std::addressof(const_moved_dereference), address);

    *result = "changed";
    result->append(" value");
    EXPECT_EQ(const_result.value(), "changed value");
}

TEST(ResultTest, ArrowIgnoresOverloadedAddressOf) {
    Result<AddressTrap> result = AddressTrap{};
    const Result<AddressTrap>& const_result = result;
    EXPECT_NE(result.operator->(), nullptr);
    EXPECT_NE(const_result.operator->(), nullptr);
    EXPECT_EQ(result->number, 42);
    EXPECT_EQ(const_result->number, 42);
}

TEST(ResultTest, AllFailureAccessorsThrowLogicError) {
    StringResult result = Status(StatusCode::kNotFound, "missing");
    const StringResult& const_result = result;

    EXPECT_THROW(static_cast<void>(result.value()), std::logic_error);
    EXPECT_THROW(static_cast<void>(const_result.value()), std::logic_error);
    EXPECT_THROW(static_cast<void>(std::move(result).value()), std::logic_error);
    EXPECT_THROW(static_cast<void>(std::move(const_result).value()), std::logic_error);
    EXPECT_THROW(static_cast<void>(*result), std::logic_error);
    EXPECT_THROW(static_cast<void>(*const_result), std::logic_error);
    EXPECT_THROW(static_cast<void>(*std::move(result)), std::logic_error);
    EXPECT_THROW(static_cast<void>(*std::move(const_result)), std::logic_error);
    EXPECT_THROW(static_cast<void>(result.operator->()), std::logic_error);
    EXPECT_THROW(static_cast<void>(const_result.operator->()), std::logic_error);
}

TEST(ResultTest, FailureExceptionIncludesCodeAndMessage) {
    const Result<int> result = Status(StatusCode::kNotFound, "configuration missing");
    try {
        static_cast<void>(result.value());
        FAIL() << "Expected failed value access to throw";
    } catch (const std::logic_error& error) {
        const std::string message = error.what();
        EXPECT_NE(message.find("code=2"), std::string::npos);
        EXPECT_NE(message.find("configuration missing"), std::string::npos);
    }
}

TEST(ResultTest, SupportsNonDefaultConstructibleValues) {
    const Result<NoDefault> result = 7;
    EXPECT_EQ(result->number, 7);
    const Result<NoDefault> failure = Status(StatusCode::kInternal);
    EXPECT_FALSE(failure.ok());
}

TEST(ResultTest, MovesOwnershipIntoAndOutOfResult) {
    auto pointer = std::make_unique<int>(42);
    PointerResult source = std::move(pointer);
    EXPECT_EQ(pointer, nullptr);
    PointerResult moved(std::move(source));
    EXPECT_TRUE(source.ok());
    EXPECT_EQ(source.value(), nullptr);
    ASSERT_NE(moved.value(), nullptr);
    EXPECT_EQ(**moved, 42);

    PointerResult assigned = Status(StatusCode::kNotFound);
    assigned = std::move(moved);
    auto extracted = std::move(assigned).value();
    ASSERT_NE(extracted, nullptr);
    EXPECT_EQ(*extracted, 42);
    EXPECT_TRUE(assigned.ok());
    EXPECT_EQ(assigned.value(), nullptr);

    PointerResult dereferenced = std::make_unique<int>(7);
    auto second = *std::move(dereferenced);
    EXPECT_EQ(*second, 7);
}

TEST(ResultTest, NullPointersAndFalseValuesCanBeSuccessful) {
    const Result<int*> pointer = nullptr;
    EXPECT_TRUE(pointer.ok());
    EXPECT_EQ(pointer.value(), nullptr);
    const Result<bool> boolean = false;
    EXPECT_TRUE(static_cast<bool>(boolean));
    EXPECT_FALSE(boolean.value());
}

TEST(ResultTest, MovesAndAssignsBetweenValuesAndErrors) {
    StringResult first = "first";
    StringResult second = "second";
    const StringResult moved(std::move(first));
    EXPECT_EQ(moved.value(), "first");
    first = std::move(second);
    EXPECT_EQ(first.value(), "second");

    StringResult error = Status(StatusCode::kInvalidArgument, "invalid value");
    first = std::move(error);
    EXPECT_EQ(first.status(), Status(StatusCode::kInvalidArgument, "invalid value"));
    StringResult error_moved(std::move(first));
    EXPECT_EQ(error_moved.status(), Status(StatusCode::kInvalidArgument, "invalid value"));
    first = "second";
    EXPECT_EQ(first.value(), "second");
    first = Status(StatusCode::kTimeout, "timeout");
    first = std::move(error_moved);
    EXPECT_EQ(first.status(), Status(StatusCode::kInvalidArgument, "invalid value"));
    first = "recovered";
    EXPECT_EQ(first.value(), "recovered");
}

TEST(ResultTest, MovingFailureDoesNotTurnTheSourceIntoSuccess) {
    StringResult source = Status(StatusCode::kNotFound, "missing");
    StringResult moved(std::move(source));
    EXPECT_EQ(moved.status(), Status(StatusCode::kNotFound, "missing"));
    EXPECT_FALSE(source.ok());
    EXPECT_FALSE(source.status().ok());

    StringResult assigned = "value";
    assigned = std::move(moved);
    EXPECT_EQ(assigned.status(), Status(StatusCode::kNotFound, "missing"));
    EXPECT_FALSE(moved.ok());
    EXPECT_FALSE(moved.status().ok());

    StringResult moved_source(std::move(source));
    EXPECT_EQ(source.status().code(), StatusCode::kInternal);
    EXPECT_EQ(moved_source.status(), source.status());
    EXPECT_EQ(std::move(moved_source).status(), source.status());
    EXPECT_THROW(static_cast<void>(source.value()), std::logic_error);
    moved_source = "recovered";
    EXPECT_EQ(moved_source.value(), "recovered");
}

TEST(ResultTest, PreservesValueConstructionExceptions) {
    ThrowingValue source(7);
    source.fail_copy = true;
    EXPECT_THROW(static_cast<void>(Result<ThrowingValue>(source)), std::runtime_error);
    source.fail_move = true;
    EXPECT_THROW(static_cast<void>(Result<ThrowingValue>(std::move(source))), std::runtime_error);
}

TEST(ResultTest, PreservesSameAlternativeAssignmentExceptions) {
    Result<ThrowingValue> source = 7;
    Result<ThrowingValue> target = 9;
    source->fail_move = true;
    EXPECT_THROW(target = std::move(source), std::runtime_error);
    ASSERT_TRUE(target.ok());
    EXPECT_EQ(target->number, 9);
}

TEST(ResultTest, ReportsAndRecoversFromValuelessStorage) {
    Result<ThrowingValue> source = 7;
    Result<ThrowingValue> target = Status(StatusCode::kNotFound, "missing");
    source->fail_move = true;

    // Changing alternatives with a throwing move constructor must leave the
    // variant valueless, unlike a same-alternative assignment failure.
    EXPECT_THROW(target = std::move(source), std::runtime_error);
    EXPECT_FALSE(target.ok());
    EXPECT_FALSE(static_cast<bool>(target));
    EXPECT_EQ(target.status(),
              Status(StatusCode::kInternal, "Result has no value after an exception"));
    EXPECT_THROW(static_cast<void>(target.value()), std::logic_error);
    EXPECT_THROW(static_cast<void>(*target), std::logic_error);
    EXPECT_THROW(static_cast<void>(target.operator->()), std::logic_error);

    Result<ThrowingValue> moved(std::move(target));
    EXPECT_EQ(moved.status(), target.status());
    const Status& borrowed = target.status();
    const Status moved_status = std::move(target).status();
    EXPECT_EQ(moved_status, borrowed);
    EXPECT_FALSE(target.ok());
    target = Status(StatusCode::kTimeout, "recovered as error");
    EXPECT_EQ(target.status(), Status(StatusCode::kTimeout, "recovered as error"));
    moved = 11;
    ASSERT_TRUE(moved.ok());
    EXPECT_EQ(moved->number, 11);
}

}  // namespace
}  // namespace tos
