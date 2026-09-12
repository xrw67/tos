#include "tos/base/filesystem.h"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <gtest/gtest.h>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace tos {
namespace {

class TemporaryDirectory {
   public:
    TemporaryDirectory() {
        path_ = std::filesystem::temp_directory_path() /
                ("tos-filesystem-test-" + std::to_string(next_id_.fetch_add(1)));
        std::filesystem::create_directories(path_);
    }

    TemporaryDirectory(const TemporaryDirectory&) = delete;
    TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;

    ~TemporaryDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }

    const std::filesystem::path& path() const noexcept { return path_; }

   private:
    static std::atomic<std::uint64_t> next_id_;
    std::filesystem::path path_;
};

std::atomic<std::uint64_t> TemporaryDirectory::next_id_{0};

Path FromNativePath(const std::filesystem::path& path) {
    auto parsed = Path::Parse(path.u8string());
    if (!parsed) {
        throw std::runtime_error(parsed.status().ToString());
    }
    return std::move(parsed).value();
}

Path ParsePath(std::string_view path) {
    auto parsed = Path::Parse(path);
    if (!parsed) {
        throw std::runtime_error(parsed.status().ToString());
    }
    return std::move(parsed).value();
}

TEST(PathTest, RejectsMalformedUtf8AndEmbeddedNul) {
    const std::string malformed("\xc0\x80", 2);
    const Result<Path> invalid = Path::Parse(malformed);
    EXPECT_FALSE(invalid);
    EXPECT_EQ(invalid.status().code(), StatusCode::kInvalidArgument);

    const std::string nul("a\0b", 3);
    const Result<Path> embedded_nul = Path::Parse(nul);
    EXPECT_FALSE(embedded_nul);
    EXPECT_EQ(embedded_nul.status().code(), StatusCode::kInvalidArgument);

    auto empty = Path::Parse("");
    ASSERT_TRUE(empty);
    const Status empty_path = CreateDirectories(empty.value());
    EXPECT_FALSE(empty_path);
    EXPECT_EQ(empty_path.code(), StatusCode::kInvalidArgument);
}

TEST(PathTest, ProvidesLexicalOperationsForUnicodePaths) {
    const Path root = ParsePath("alpha");
    const Path child = ParsePath(u8"目录");
    const Path file = ParsePath(u8"文件.txt");
    const Path joined = root.Join(child).Join(file);

    EXPECT_EQ(joined.filename(), file);
    EXPECT_EQ(joined.parent_path().filename(), child);
    EXPECT_EQ(joined.stem().utf8(), u8"文件");
    EXPECT_EQ(joined.extension().utf8(), ".txt");
    EXPECT_EQ(root.Join(ParsePath("one"))
                  .Join(ParsePath(".."))
                  .Join(ParsePath("two"))
                  .lexically_normal()
                  .filename(),
              ParsePath("two"));
    EXPECT_FALSE(root.is_absolute());

    TemporaryDirectory directory;
    EXPECT_TRUE(FromNativePath(directory.path()).is_absolute());
}

TEST(FilesystemTest, ReadsWritesAndCreatesUtf8Directories) {
    TemporaryDirectory directory;
    const Path base = FromNativePath(directory.path());
    const Path nested = base.Join(ParsePath(u8"目录")).Join(ParsePath("nested"));
    const Path file = nested.Join(ParsePath(u8"内容.txt"));

    ASSERT_TRUE(CreateDirectories(nested));
    EXPECT_TRUE(CreateDirectories(nested));
    ASSERT_TRUE(WriteTextFile(file, std::string("first\0value", 11)));

    auto read = ReadTextFile(file);
    ASSERT_TRUE(read);
    EXPECT_EQ(read.value(), std::string("first\0value", 11));

    const Path collision = base.Join(ParsePath("collision"));
    ASSERT_TRUE(WriteTextFile(collision, "not a directory"));
    const Status existing_file = CreateDirectories(collision);
    EXPECT_FALSE(existing_file);
    EXPECT_EQ(existing_file.code(), StatusCode::kAlreadyExists);
}

TEST(FilesystemTest, ListsEntriesInUtf8ByteOrderAndReportsMetadata) {
    TemporaryDirectory directory;
    const Path base = FromNativePath(directory.path());
    const Path first = base.Join(ParsePath("a.txt"));
    const Path second = base.Join(ParsePath("z.txt"));
    const Path third = base.Join(ParsePath(u8"中文.txt"));
    const Path subdirectory = base.Join(ParsePath("subdir"));
    ASSERT_TRUE(WriteTextFile(second, "z"));
    ASSERT_TRUE(WriteTextFile(third, "utf8"));
    ASSERT_TRUE(WriteTextFile(first, "abc"));
    ASSERT_TRUE(CreateDirectories(subdirectory));

    auto listed = ListDirectory(base);
    ASSERT_TRUE(listed);
    const std::vector<Path> expected{first, subdirectory, second, third};
    EXPECT_EQ(listed.value(), expected);

    auto file_metadata = GetFileMetadata(first);
    ASSERT_TRUE(file_metadata);
    EXPECT_EQ(file_metadata.value().type, FileType::kRegular);
    EXPECT_EQ(file_metadata.value().size, 3U);

    auto directory_metadata = GetFileMetadata(subdirectory);
    ASSERT_TRUE(directory_metadata);
    EXPECT_EQ(directory_metadata.value().type, FileType::kDirectory);
    EXPECT_EQ(directory_metadata.value().size, 0U);
}

TEST(FilesystemTest, ReportsSymbolicLinksWithoutFollowingThem) {
    TemporaryDirectory directory;
    const std::filesystem::path target = directory.path() / "target";
    const std::filesystem::path link = directory.path() / "link";
    ASSERT_TRUE(WriteTextFile(FromNativePath(target), "target"));

    std::error_code error;
    std::filesystem::create_symlink(target, link, error);
    if (error) {
        GTEST_SKIP() << "symbolic links are unavailable: " << error.message();
    }
    auto metadata = GetFileMetadata(FromNativePath(link));
    ASSERT_TRUE(metadata);
    EXPECT_EQ(metadata.value().type, FileType::kSymlink);
    EXPECT_EQ(metadata.value().size, 0U);
}

TEST(FilesystemTest, AtomicallyWritesAndCleansTemporaryFiles) {
    TemporaryDirectory directory;
    const Path target = FromNativePath(directory.path() / "atomic.txt");
    ASSERT_TRUE(WriteTextFile(target, "old"));
    ASSERT_TRUE(WriteTextFileAtomic(target, "replacement"));

    auto content = ReadTextFile(target);
    ASSERT_TRUE(content);
    EXPECT_EQ(content.value(), "replacement");

    const std::string temporary_prefix = "atomic.txt.tos-tmp-";
    for (const std::filesystem::directory_entry& entry :
         std::filesystem::directory_iterator(directory.path())) {
        EXPECT_NE(entry.path().filename().string().find(temporary_prefix), 0U);
    }

    const Path missing_parent = FromNativePath(directory.path() / "missing" / "target.txt");
    const Status missing_parent_status = WriteTextFileAtomic(missing_parent, "nope");
    EXPECT_FALSE(missing_parent_status);
}

TEST(FilesystemTest, RemovesAndRenamesWithStrictFailureSemantics) {
    TemporaryDirectory directory;
    const Path base = FromNativePath(directory.path());
    const Path source = base.Join(ParsePath("source.txt"));
    const Path destination = base.Join(ParsePath("destination.txt"));
    ASSERT_TRUE(WriteTextFile(source, "source"));
    ASSERT_TRUE(WriteTextFile(destination, "destination"));

    const Status existing_destination = RenamePath(source, destination);
    EXPECT_FALSE(existing_destination);
    EXPECT_EQ(existing_destination.code(), StatusCode::kAlreadyExists);
    ASSERT_TRUE(RemovePath(destination));
    ASSERT_TRUE(RenamePath(source, destination));

    const Path nonempty = base.Join(ParsePath("nonempty"));
    ASSERT_TRUE(CreateDirectories(nonempty));
    ASSERT_TRUE(WriteTextFile(nonempty.Join(ParsePath("child")), "child"));
    const Status nonrecursive = RemovePath(nonempty);
    EXPECT_FALSE(nonrecursive);
    EXPECT_EQ(nonrecursive.code(), StatusCode::kFailedPrecondition);
    ASSERT_TRUE(RemovePath(nonempty, true));

    const Path missing = base.Join(ParsePath("missing"));
    const Result<std::string> missing_read = ReadTextFile(missing);
    EXPECT_FALSE(missing_read);
    EXPECT_EQ(missing_read.status().code(), StatusCode::kNotFound);
    const Status missing_remove = RemovePath(missing);
    EXPECT_FALSE(missing_remove);
    EXPECT_EQ(missing_remove.code(), StatusCode::kNotFound);

    const Status missing_parent_write = WriteTextFile(missing.Join(ParsePath("child")), "child");
    EXPECT_FALSE(missing_parent_write);
    EXPECT_EQ(missing_parent_write.code(), StatusCode::kNotFound);
    const Status directory_write = WriteTextFile(base, "not a file");
    EXPECT_FALSE(directory_write);
    EXPECT_EQ(directory_write.code(), StatusCode::kFailedPrecondition);
}

#ifndef _WIN32
TEST(FilesystemTest, MapsPermissionDeniedWhenTheEnvironmentEnforcesPermissions) {
    TemporaryDirectory directory;
    const std::filesystem::path blocked_native = directory.path() / "blocked";
    std::filesystem::create_directory(blocked_native);
    const Path blocked = FromNativePath(blocked_native);
    const Path file = blocked.Join(ParsePath("file"));

    std::error_code error;
    std::filesystem::permissions(
        blocked_native, std::filesystem::perms::owner_read | std::filesystem::perms::owner_exec,
        std::filesystem::perm_options::replace, error);
    ASSERT_FALSE(error) << error.message();
    const Status status = WriteTextFile(file, "denied");
    std::filesystem::permissions(blocked_native, std::filesystem::perms::owner_all,
                                 std::filesystem::perm_options::replace, error);
    ASSERT_FALSE(error) << error.message();
    if (status) {
        GTEST_SKIP() << "current process can bypass directory permissions";
    }
    EXPECT_EQ(status.code(), StatusCode::kPermissionDenied);
}
#endif

}  // namespace
}  // namespace tos
