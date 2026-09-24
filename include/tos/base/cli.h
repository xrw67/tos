#ifndef TOS_BASE_CLI_H_
#define TOS_BASE_CLI_H_

// CLI11 is shipped in the public include tree. Use its CLI namespace directly;
// parsing, help, and version use CLI11 exceptions, which application boundaries
// must handle. Bound option variables must outlive parsing, and a CLI::App must
// not be parsed or mutated concurrently. Allocation exceptions propagate.
#if defined(__clang__)
// Treat the upstream include as a system header: libc++ reports codecvt template
// deprecations at instantiation even through CLI11's local diagnostic pragmas.
#pragma clang system_header
#endif
#include "tos/vendor/cli11/CLI11.hpp"

#endif  // TOS_BASE_CLI_H_
