# tos Agent Guide

## Project Scope

`tos` is a C++17 application-foundation library. Its shipped public APIs use
the `tos::` namespace and `<tos/...>` headers; `tos::base` is the sole CMake
consumer target and builds the `tosbase` static library. GoogleTest tests,
examples, and Linux/macOS/Windows Release CI are in place. Treat items in
`TODO.md` as planned work, not existing behavior.

Read `TODO.md` before adding a component. It defines the implementation order,
acceptance criteria, and intended dependency direction. Do not copy Pallas
source, target names, or public error types; it is a design reference only.

## Repository Layout

| Path | Purpose |
| --- | --- |
| `include/tos/` | Public headers. Each header must be independently includable. |
| `tests/` | GoogleTest unit, allocation-regression, shutdown, and example checks. |
| `examples/` | Small consumer-facing examples. |
| `third_party/` | Vendored dependencies and their license/source records. |
| `.github/workflows/ci.yml` | Release checks on GCC, Apple Clang, and MSVC. |
| `README.md` | Supported behavior, build instructions, and user-facing examples. |
| `TODO.md` | Roadmap and completion checklist. |

Do not edit vendored GoogleTest unless the task explicitly concerns the
dependency. Build configuration must remain offline: do not add `FetchContent`
or a dependency on Git submodules.

## Public API Rules

- Use C++17 only. Public symbols belong in namespace `tos`; public headers use
  the established include guard and include every standard or project header
  they require.
- Use RAII and make ownership explicit. Do not use raw pointers to transfer
  ownership. Document lifetime, ownership, thread-safety, shutdown/cancellation
  behavior, errors, and platform differences for every public component.
- Expected operational failures return `tos::Status` for no-value operations
  and `tos::Result<T>` for value operations. Do not add a second result/error
  model, `Result<void>`, or `Result<Status>`.
- `Status` and `Result<T>` are `[[nodiscard]]` and move-only. Propagate an
  existing error with `std::move`; do not accidentally introduce copies or
  references that outlive their source. Preserve the existing `Status` and
  `Result<T>` ownership, allocation, move, and invalid-value contracts unless a
  task explicitly changes their documented API.
- API comments must state exception behavior. Allocation and value-type
  construction exceptions are not silently converted to `Status`.
- Keep public APIs portable across Linux, macOS, and Windows. Report unsupported
  platform capabilities explicitly instead of simulating success.

## Build And Test

Use an out-of-source build directory. The standard full validation is:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DTOS_BUILD_EXAMPLES=ON
cmake --build build --config Release --parallel
ctest --test-dir build -C Release -L unit --output-on-failure --no-tests=error --timeout 30
ctest --test-dir build -C Release -L example --output-on-failure --no-tests=error --timeout 30
```

`CMAKE_BUILD_TYPE=Release` supports single-config generators; `--config Release`
and `-C Release` support multi-config generators. Keep both forms in commands
that are intended to work on all CI platforms.

Run the narrowest relevant test while iterating, then run the full commands
above before handing off a behavioral, public-header, or CMake change. All
added or modified C++ source files must conform to the repository
`.clang-format` configuration. Before handoff, run `clang-format -i` on every
changed public header, source file, and test:

```bash
clang-format -i include/tos/<changed-header>.h tests/<changed-test>.cpp
```

Do not commit build products, generated CMake files, or local IDE state.

## Test Conventions

- Add ordinary GoogleTest sources to `tos_unit_tests` in `tests/CMakeLists.txt`.
  `gtest_discover_tests()` supplies the `tos.` prefix, `unit` label, and
  30-second timeout; tests do not define their own `main()`.
- Keep global allocation hooks in the separate `tos_allocation_tests` executable
  so they cannot affect regular tests. Count allocations only during the action
  under test.
- Use a separate executable when a test must observe process exit or static
  destruction, following `tos_result_shutdown_test`.
- Register integration or example checks with an appropriate CTest label and
  an explicit timeout. Exercise failure paths, move/lifetime boundaries, and
  cross-thread behavior when the API exposes them.
- New public behavior needs focused tests and a user-facing documentation update.
  Mark a `TODO.md` item complete only after its stated acceptance criteria have
  automated evidence.

## CMake And Dependency Rules

- The sole consumer target is `tos::base`, backed by the `tosbase` static
  library; do not add component-level consumer targets. `add_subdirectory`
  consumers must continue to work with tests and examples off by default.
- Keep `BUILD_TESTING` and `TOS_BUILD_EXAMPLES` optional. Tests bring in the
  vendored GoogleTest source only when enabled; examples must build without
  tests.
- Respect the planned one-way dependency graph: Foundation, Task, Platform,
  Network, and IPC must not depend on Runtime or extension modules. Add target
  dependency checks when introducing component targets.
- Prefer the standard library and mature, documented third-party libraries.
  Record any new dependency's exact version, source, checksum where applicable,
  and license in `third_party/README.md`.

## Documentation And Change Discipline

- Keep `README.md` accurate about what is implemented today. Add examples only
  when they compile and run as part of the build.
- Record public-contract and architectural decisions in the roadmap-required
  docs as those documents are introduced. In the 0.x series, breaking API
  changes are allowed only with a migration note.
- Preserve unrelated working-tree changes. Check `git status` and the relevant
  diff before editing or finalizing work.
- Do not claim platform support, sanitizer coverage, or an implementation exists
  without a corresponding automated check or recorded native verification.
