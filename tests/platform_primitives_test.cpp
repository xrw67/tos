#include <chrono>
#include <filesystem>
#include <future>
#include <gtest/gtest.h>
#include <iterator>
#include <limits>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>

#include "test_util.h"
#include "tos/base/file_lock.h"
#include "tos/base/filesystem.h"
#include "tos/base/process.h"
#include "tos/base/secure_random.h"
#include "tos/base/termination.h"
#include "tos/base/uuid.h"

namespace tos {
namespace {

Path NativePath(const std::filesystem::path& path) {
    auto result = Path::Parse(path.u8string());
    EXPECT_TRUE(result) << result.status().ToString();
    return std::move(result).value();
}

TEST(PlatformPrimitivesTest, GeneratesSecureBytesAndUuidV4) {
    auto empty = SecureRandomBytes(0);
    ASSERT_TRUE(empty);
    EXPECT_TRUE(empty->empty());

    auto first = SecureRandomBytes(32);
    auto second = SecureRandomBytes(32);
    ASSERT_TRUE(first);
    ASSERT_TRUE(second);
    EXPECT_EQ(first->size(), 32U);
    EXPECT_EQ(second->size(), 32U);
    EXPECT_NE(*first, *second);

    auto uuid = GenerateUuidV4();
    ASSERT_TRUE(uuid);
    ASSERT_EQ(uuid->size(), 36U);
    EXPECT_TRUE(IsUuid(*uuid));
    EXPECT_EQ(uuid->find_first_of("ABCDEF"), std::string::npos);
    EXPECT_EQ((*uuid)[14], '4');
    EXPECT_TRUE((*uuid)[19] == '8' || (*uuid)[19] == '9' || (*uuid)[19] == 'a' ||
                (*uuid)[19] == 'b');
}

TEST(PlatformPrimitivesTest, FileLockIsMoveOnlyAndNonBlocking) {
    test::TemporaryDirectory directory("tos-lock-test-");
    const Path lock_path = NativePath(directory.path() / "state.lock");
    auto first_result = FileLock::TryAcquire(lock_path);
    ASSERT_TRUE(first_result);
    auto second = FileLock::TryAcquire(lock_path);
    ASSERT_FALSE(second);
    EXPECT_EQ(second.status().code(), StatusCode::kUnavailable);

    {
        FileLock moved = std::move(first_result).value();
        auto blocked = FileLock::TryAcquire(lock_path);
        ASSERT_FALSE(blocked);
        moved = FileLock{};
        EXPECT_TRUE(FileLock::TryAcquire(lock_path));
    }
    auto released = FileLock::TryAcquire(lock_path);
    EXPECT_TRUE(released);
}

TEST(PlatformPrimitivesTest, AtomicWriterSupportsDurabilityModesAndCleanup) {
    test::TemporaryDirectory directory("tos-atomic-test-");
    const Path target = NativePath(directory.path() / "state.pb");
    ASSERT_TRUE(WriteTextFileAtomic(target, "first"));
    auto content = ReadTextFile(target);
    ASSERT_TRUE(content);
    EXPECT_EQ(*content, "first");

    ASSERT_TRUE(WriteTextFileAtomic(target, "second", AtomicWriteDurability::kData));
    content = ReadTextFile(target);
    ASSERT_TRUE(content);
    EXPECT_EQ(*content, "second");

#ifdef _WIN32
    EXPECT_EQ(WriteTextFileAtomic(target, "third", AtomicWriteDurability::kDataAndDirectory).code(),
              StatusCode::kUnimplemented);
    EXPECT_EQ(ReadTextFile(target).value(), "second");
    EXPECT_EQ(SyncDirectory(NativePath(directory.path())).code(), StatusCode::kUnimplemented);
    ASSERT_TRUE(WriteTextFileAtomic(target, "third", AtomicWriteDurability::kData));
#else
    ASSERT_TRUE(WriteTextFileAtomic(target, "third", AtomicWriteDurability::kDataAndDirectory));
#endif
    content = ReadTextFile(target);
    ASSERT_TRUE(content);
    EXPECT_EQ(*content, "third");

    auto writer = AtomicFileWriter::Create(target);
    ASSERT_TRUE(writer);
    ASSERT_TRUE(writer->Write("discarded"));
    writer = Result<AtomicFileWriter>(AtomicFileWriter{});
    content = ReadTextFile(target);
    ASSERT_TRUE(content);
    EXPECT_EQ(*content, "third");
}

TEST(PlatformPrimitivesTest, TerminationControllerCanBeRequestedAndTimedOut) {
    TerminationController controller;
    ASSERT_TRUE(controller.Install());
    auto timeout = controller.Wait(std::chrono::milliseconds(1));
    ASSERT_FALSE(timeout);
    EXPECT_EQ(timeout.status().code(), StatusCode::kTimeout);

    controller.RequestStop(TerminationReason::kExplicitRequest);
    auto reason = controller.Wait(std::chrono::milliseconds(100));
    ASSERT_TRUE(reason);
    EXPECT_EQ(*reason, TerminationReason::kExplicitRequest);
    EXPECT_TRUE(controller.Requested());
    controller.Uninstall();
    EXPECT_FALSE(controller.IsInstalled());
    EXPECT_EQ(controller.Install().code(), StatusCode::kOk);
    EXPECT_FALSE(controller.Requested());
    EXPECT_EQ(controller.Wait(std::chrono::milliseconds(0)).status().code(), StatusCode::kTimeout);
}

TEST(PlatformPrimitivesTest, SecureRandomAndUuidContractsHoldConcurrently) {
    EXPECT_EQ(SecureRandomBytes(std::numeric_limits<std::size_t>::max()).status().code(),
              StatusCode::kOutOfRange);
    EXPECT_TRUE(IsUuid("ABCDEF01-2345-6789-ABCD-0123456789AB"));
    EXPECT_FALSE(IsUuid(std::string("abcdef01-2345-6789-abcd-0123456789a") + '\xff'));
    for (const auto* invalid :
         {"", "abcdef01-2345-6789-abcd-0123456789a!", "abcdef0122345-6789-abcd-0123456789ab"})
        EXPECT_FALSE(IsUuid(invalid));
    std::future<bool> tasks[8];
    for (auto& task : tasks)
        task = std::async(std::launch::async, [] {
            auto bytes = SecureRandomBytes(4096);
            auto uuid = GenerateUuidV4();
            return bytes && bytes->size() == 4096 && uuid && IsUuid(*uuid) && (*uuid)[14] == '4';
        });
    for (auto& task : tasks) EXPECT_TRUE(task.get());
}

TEST(PlatformPrimitivesTest, FileLockMoveAssignmentReleasesOldLockAndCompetesAcrossProcesses) {
    static_assert(!std::is_copy_constructible_v<FileLock>);
    test::TemporaryDirectory directory("tos-lock-test-");
    const Path one = NativePath(directory.path() / u8"锁.lock");
    const Path two = NativePath(directory.path() / "two.lock");
    auto first = FileLock::TryAcquire(one);
    auto second = FileLock::TryAcquire(two);
    ASSERT_TRUE(first);
    ASSERT_TRUE(second);
    FileLock owner = std::move(first).value();
    owner = std::move(second).value();
    EXPECT_TRUE(FileLock::TryAcquire(one));
    EXPECT_FALSE(FileLock::TryAcquire(two));
    ProcessOptions process;
    process.executable = NativePath(TOS_PLATFORM_PRIMITIVES_HELPER);
    process.arguments = {"lock", two.utf8(), "blocked"};
    auto blocked = RunCommand(process);
    ASSERT_TRUE(blocked) << blocked.status().ToString();
    EXPECT_EQ(blocked->exit.exit_code, 0);
    owner = FileLock{};
    process.arguments.back() = "available";
    auto available = RunCommand(process);
    ASSERT_TRUE(available);
    EXPECT_EQ(available->exit.exit_code, 0);
    EXPECT_TRUE(std::filesystem::exists(directory.path() / "two.lock"));
    EXPECT_EQ(FileLock::TryAcquire(Path{}).status().code(), StatusCode::kInvalidArgument);
    EXPECT_EQ(
        FileLock::TryAcquire(NativePath(directory.path() / "missing" / "lock")).status().code(),
        StatusCode::kNotFound);
}

TEST(PlatformPrimitivesTest, AtomicWriterMoveFailureAndCommitLifecycle) {
    static_assert(!std::is_copy_constructible_v<AtomicFileWriter>);
    test::TemporaryDirectory directory("tos-writer-test-");
    const Path target = NativePath(directory.path() / u8"状态.pb");
    ASSERT_TRUE(WriteTextFileAtomic(target, "old"));
    auto first = AtomicFileWriter::Create(target);
    auto second = AtomicFileWriter::Create(target);
    ASSERT_TRUE(first);
    ASSERT_TRUE(second);
    ASSERT_TRUE(first->Write("discard"));
    ASSERT_TRUE(second->Write(std::string("new\0", 4)));
    AtomicFileWriter owner = std::move(first).value();
    owner = std::move(second).value();
    EXPECT_EQ(first->Write("").code(), StatusCode::kFailedPrecondition);
    EXPECT_EQ(ReadTextFile(target).value(), "old");
    ASSERT_TRUE(owner.Write("tail"));
    ASSERT_TRUE(owner.Commit());
    EXPECT_EQ(ReadTextFile(target).value(), std::string("new\0tail", 8));
    EXPECT_EQ(owner.Commit().code(), StatusCode::kFailedPrecondition);
    EXPECT_EQ(owner.Write("").code(), StatusCode::kFailedPrecondition);
    const Path collision = NativePath(directory.path() / "directory");
    ASSERT_TRUE(CreateDirectories(collision));
    {
        auto writer = AtomicFileWriter::Create(collision);
        ASSERT_TRUE(writer);
        ASSERT_TRUE(writer->Write("data"));
        EXPECT_FALSE(writer->Commit());
        EXPECT_EQ(writer->Commit().code(), StatusCode::kFailedPrecondition);
        EXPECT_EQ(writer->Write("retry").code(), StatusCode::kFailedPrecondition);
    }
    EXPECT_TRUE(std::filesystem::is_directory(directory.path() / "directory"));
    EXPECT_EQ(std::distance(std::filesystem::directory_iterator(directory.path()),
                            std::filesystem::directory_iterator()),
              2);
    EXPECT_EQ(AtomicFileWriter::Create(Path{}).status().code(), StatusCode::kInvalidArgument);
    EXPECT_EQ(
        AtomicFileWriter::Create(target, static_cast<AtomicWriteDurability>(9)).status().code(),
        StatusCode::kInvalidArgument);
}

TEST(PlatformPrimitivesTest, TerminationRequestsWakeAllWaitersAndFirstReasonWins) {
    TerminationController controller, other;
    EXPECT_EQ(controller.Wait().status().code(), StatusCode::kFailedPrecondition);
    ASSERT_TRUE(controller.Install());
    EXPECT_TRUE(controller.Install());
    EXPECT_EQ(other.Install().code(), StatusCode::kAlreadyExists);
    EXPECT_EQ(controller.Wait(std::chrono::milliseconds(-1)).status().code(),
              StatusCode::kInvalidArgument);
    std::future<Result<TerminationReason>> waiters[4];
    for (auto& waiter : waiters)
        waiter = std::async(std::launch::async,
                            [&] { return controller.Wait(std::chrono::seconds(2)); });
    controller.RequestStop(TerminationReason::kTerminate);
    controller.RequestStop(TerminationReason::kInterrupt);
    for (auto& waiter : waiters) {
        auto reason = waiter.get();
        ASSERT_TRUE(reason);
        EXPECT_EQ(*reason, TerminationReason::kTerminate);
    }
    EXPECT_EQ(controller.reason(), TerminationReason::kTerminate);
    EXPECT_EQ(controller.Wait(std::chrono::milliseconds(0)).value(), TerminationReason::kTerminate);
    controller.Uninstall();
    controller.Uninstall();
    EXPECT_TRUE(other.Install());
}

TEST(PlatformPrimitivesTest, NativeSignalsAreHandledAndPosixHandlersRestored) {
    ProcessOptions process;
    process.executable = NativePath(TOS_PLATFORM_PRIMITIVES_HELPER);
    process.arguments = {"signal"};
    auto result = RunCommand(process);
    ASSERT_TRUE(result) << result.status().ToString();
    EXPECT_EQ(result->exit.exit_code, 0) << result->stderr_output;
}

}  // namespace
}  // namespace tos
