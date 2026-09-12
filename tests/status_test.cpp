#include "tos/base/status.h"

#include <gtest/gtest.h>
#include <string>
#include <string_view>
#include <utility>

namespace tos {
namespace {

struct StatusStringCase {
    StatusCode code;
    const char* name;
    bool (*matches)(const Status&) noexcept;
};

class StatusStringTest : public testing::TestWithParam<StatusStringCase> {};

constexpr StatusStringCase kStatusStringCases[] = {
    {StatusCode::kOk, "OK", IsOk},
    {StatusCode::kCancelled, "CANCELLED", IsCancelled},
    {StatusCode::kUnknown, "UNKNOWN", IsUnknown},
    {StatusCode::kInvalidArgument, "INVALID_ARGUMENT", IsInvalidArgument},
    {StatusCode::kTimeout, "TIMEOUT", IsTimeout},
    {StatusCode::kNotFound, "NOT_FOUND", IsNotFound},
    {StatusCode::kAlreadyExists, "ALREADY_EXISTS", IsAlreadyExists},
    {StatusCode::kPermissionDenied, "PERMISSION_DENIED", IsPermissionDenied},
    {StatusCode::kUnauthenticated, "UNAUTHENTICATED", IsUnauthenticated},
    {StatusCode::kResourceExhausted, "RESOURCE_EXHAUSTED", IsResourceExhausted},
    {StatusCode::kFailedPrecondition, "FAILED_PRECONDITION", IsFailedPrecondition},
    {StatusCode::kAborted, "ABORTED", IsAborted},
    {StatusCode::kOutOfRange, "OUT_OF_RANGE", IsOutOfRange},
    {StatusCode::kUnimplemented, "UNIMPLEMENTED", IsUnimplemented},
    {StatusCode::kInternal, "INTERNAL", IsInternal},
    {StatusCode::kUnavailable, "UNAVAILABLE", IsUnavailable},
    {StatusCode::kDataLoss, "DATA_LOSS", IsDataLoss},
};

TEST_P(StatusStringTest, FormatsCodeWithoutMessage) {
    const auto& param = GetParam();
    const Status status(param.code);
    EXPECT_EQ(status.ToString(), param.name);
    EXPECT_EQ(status.ok(), param.code == StatusCode::kOk);
    EXPECT_EQ(static_cast<bool>(status), status.ok());
    EXPECT_EQ(status.code(), param.code);
    EXPECT_TRUE(status.message().empty());
    EXPECT_TRUE(param.matches(status));
}

TEST_P(StatusStringTest, FormatsCodeWithMessage) {
    const auto& param = GetParam();
    std::string message = "operation failed";
    const Status status(param.code, message);
    message.assign("changed by caller");
    const std::string expected =
        param.code == StatusCode::kOk ? "OK" : std::string(param.name) + ": operation failed";
    EXPECT_EQ(status.ToString(), expected);
    EXPECT_EQ(status.ok(), param.code == StatusCode::kOk);
    EXPECT_EQ(static_cast<bool>(status), status.ok());
    EXPECT_EQ(status.code(), param.code);
    EXPECT_EQ(status.message(), param.code == StatusCode::kOk ? "" : "operation failed");
    EXPECT_TRUE(param.matches(status));
}

INSTANTIATE_TEST_SUITE_P(Codes, StatusStringTest, testing::ValuesIn(kStatusStringCases),
                         [](const testing::TestParamInfo<StatusStringCase>& info) {
                             return info.param.name;
                         });

TEST(StatusTest, ClassificationPredicatesMatchOnlyTheirCode) {
    for (const StatusStringCase& expected : kStatusStringCases) {
        const Status status(expected.code);
        for (const StatusStringCase& candidate : kStatusStringCases) {
            EXPECT_EQ(candidate.matches(status), candidate.code == expected.code)
                << "status=" << expected.name << ", predicate=" << candidate.name;
        }
    }

    const Status unknown_value(static_cast<StatusCode>(99));
    for (const StatusStringCase& candidate : kStatusStringCases) {
        EXPECT_FALSE(candidate.matches(unknown_value)) << candidate.name;
    }
}

TEST(StatusTest, ToStringPreservesLongMessagesAndNullBytes) {
    for (const auto& message : {std::string(1024, 'x'), std::string("before\0after", 12)}) {
        SCOPED_TRACE(message.size());
        const Status status(StatusCode::kInternal, message);
        const std::string expected = "INTERNAL: " + message;
        EXPECT_EQ(status.ToString(), expected);
    }
}

TEST(StatusTest, ToStringFormatsUnknownCodes) {
    for (const int code : {99, -1}) {
        SCOPED_TRACE(code);
        const std::string name = "UNKNOWN(" + std::to_string(code) + ")";
        const Status empty(static_cast<StatusCode>(code));
        const Status error(static_cast<StatusCode>(code), "missing");
        EXPECT_EQ(empty.ToString(), name);
        EXPECT_EQ(error.ToString(), name + ": missing");
    }
}

TEST(StatusTest, ToStringOwnsTextIndependentlyOfSource) {
    const std::string message(1024, 'x');
    std::string text;
    {
        const Status status(StatusCode::kNotFound, message);
        text = status.ToString();
        text.append(" copied");
        EXPECT_EQ(status.message(), message);
        EXPECT_EQ(status.code(), StatusCode::kNotFound);
    }
    EXPECT_EQ(text, "NOT_FOUND: " + message + " copied");
}

TEST(StatusTest, DefaultsToSuccess) {
    const Status status;
    EXPECT_TRUE(status.ok());
    EXPECT_TRUE(static_cast<bool>(status));
    EXPECT_EQ(status.code(), StatusCode::kOk);
    EXPECT_TRUE(status.message().empty());
    EXPECT_EQ(status, Status::Ok());
}

TEST(StatusTest, ComparesBothCodeAndMessage) {
    const Status status(StatusCode::kNotFound, "missing");
    EXPECT_EQ(status, Status(StatusCode::kNotFound, "missing"));
    EXPECT_NE(status, Status(StatusCode::kNotFound, "different message"));
    EXPECT_NE(status, Status(StatusCode::kInternal, "missing"));
    EXPECT_NE(status, Status::Ok());
}

TEST(StatusTest, SelfMovePreservesState) {
    Status status(StatusCode::kInternal, "internal error");
    Status& alias = status;
    status = std::move(alias);
    EXPECT_EQ(status, Status(StatusCode::kInternal, "internal error"));
}

TEST(StatusTest, SuccessIgnoresMessagesWithoutConsumingThem) {
    for (const auto& expected : {std::string{}, std::string("short"), std::string(1024, 'x')}) {
        SCOPED_TRACE(expected.size());
        std::string message = expected;
        const Status from_lvalue(StatusCode::kOk, message);
        const Status from_view(StatusCode::kOk, std::string_view(message));
        const Status from_pointer(StatusCode::kOk, message.c_str());
        const Status from_rvalue(StatusCode::kOk, std::move(message));

        EXPECT_EQ(from_lvalue, Status::Ok());
        EXPECT_EQ(from_view, Status::Ok());
        EXPECT_EQ(from_pointer, Status::Ok());
        EXPECT_EQ(from_rvalue, Status::Ok());
        EXPECT_EQ(message, expected);
    }
}

TEST(StatusTest, OwnsLongMessageCopiedFromLvalue) {
    std::string message(1024, 'x');
    const Status status(StatusCode::kNotFound, message);
    message.assign(1024, 'y');
    EXPECT_EQ(status.message(), std::string(1024, 'x'));
    EXPECT_EQ(status.code(), StatusCode::kNotFound);
}

TEST(StatusTest, OwnsExactStringViewContentsIncludingNullBytes) {
    std::string storage(2048, 'x');
    storage[500] = '\0';
    const std::string_view view(storage.data() + 10, 1024);
    const std::string expected(view);
    const Status status(StatusCode::kInternal, view);
    storage.assign(2048, 'y');
    EXPECT_EQ(status.message(), expected);
    EXPECT_EQ(status.message().size(), 1024u);
}

TEST(StatusTest, AcceptsAllEmptyMessageForms) {
    const Status expected(StatusCode::kInternal);
    EXPECT_EQ(Status(StatusCode::kInternal, {}), expected);
    EXPECT_EQ(Status(StatusCode::kInternal, ""), expected);
    EXPECT_EQ(Status(StatusCode::kInternal, std::string_view{}), expected);
    EXPECT_EQ(Status(StatusCode::kInternal, std::string{}), expected);
}

TEST(StatusTest, MovesOwnershipAcrossStates) {
    for (const auto& expected : {std::string{}, std::string("short"), std::string(1024, 'x')}) {
        SCOPED_TRACE(expected.size());
        std::string message = expected;
        Status original(StatusCode::kNotFound, std::move(message));
        const char* data = original.message().data();
        Status moved(std::move(original));
        EXPECT_EQ(moved.message(), expected);
        EXPECT_EQ(moved.message().data(), data);
        EXPECT_EQ(original, Status::Ok());
        EXPECT_EQ(original.ToString(), "OK");
        EXPECT_EQ(moved.ToString(), expected.empty() ? "NOT_FOUND" : "NOT_FOUND: " + expected);

        Status assigned;
        assigned = std::move(moved);
        EXPECT_EQ(assigned.message().data(), data);
        EXPECT_EQ(moved, Status::Ok());

        Status overwritten(StatusCode::kInternal, std::string(2048, 'y'));
        overwritten = std::move(assigned);
        EXPECT_EQ(overwritten.message(), expected);
        EXPECT_EQ(overwritten.message().data(), data);
        EXPECT_EQ(overwritten.code(), StatusCode::kNotFound);
        EXPECT_EQ(assigned, Status::Ok());

        Status success;
        overwritten = std::move(success);
        EXPECT_EQ(overwritten, Status::Ok());
        EXPECT_EQ(success, Status::Ok());
        original = Status(StatusCode::kTimeout);
        EXPECT_EQ(original, Status(StatusCode::kTimeout));
    }
}

TEST(StatusTest, MovedMessagesSurviveSourceDestruction) {
    for (const std::string& text : {std::string("short"), std::string(1024, 'x')}) {
        Status survivor;
        {
            Status original(StatusCode::kInternal, text);
            const char* data = original.message().data();
            survivor = std::move(original);
            EXPECT_EQ(data, survivor.message().data());
            EXPECT_TRUE(original.ok());
        }
        EXPECT_EQ(survivor.message(), text);
        EXPECT_EQ(survivor.code(), StatusCode::kInternal);
    }
}

}  // namespace
}  // namespace tos
