#include <gtest/gtest.h>
#include <string>
#include <vector>

#include "tos/base/cli.h"

namespace {

TEST(Cli11Test, ParsesTypedOptionsRepeatedValuesAndPositionals) {
    CLI::App app{"Example command", "tos-tool"};
    int port = 0;
    bool verbose = false;
    std::vector<std::string> includes;
    std::vector<std::string> files;
    app.add_option("-p,--port", port)->check(CLI::Range(1, 65535));
    app.add_flag("-v,--verbose", verbose);
    app.add_option("-I,--include", includes);
    app.add_option("files", files);
    const char* argv[] = {"tos-tool", "--port=8080", "-Ione", "-I", "two", "-v", "--", "-file"};

    app.parse(8, argv);

    EXPECT_EQ(port, 8080);
    EXPECT_TRUE(verbose);
    EXPECT_EQ(includes, (std::vector<std::string>{"one", "two"}));
    EXPECT_EQ(files, (std::vector<std::string>{"-file"}));
}

TEST(Cli11Test, ExposesValidationErrorsAndSubcommands) {
    CLI::App app{"Example command", "tos-tool"};
    auto* serve = app.add_subcommand("serve");
    int port = 0;
    serve->add_option("--port", port)->required()->check(CLI::Range(1, 65535));
    const char* invalid[] = {"tos-tool", "serve", "--port", "70000"};
    EXPECT_THROW(app.parse(4, invalid), CLI::ValidationError);

    const char* valid[] = {"tos-tool", "serve", "--port", "443"};
    ASSERT_NO_THROW(app.parse(4, valid));
    EXPECT_TRUE(serve->parsed());
    EXPECT_EQ(port, 443);
}

}  // namespace
