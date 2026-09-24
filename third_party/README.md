# Third-Party Libraries

Vendored dependencies are stored as source code in this directory and committed
with tos. The crypto component additionally requires a system-installed OpenSSL
development package; CMake never downloads dependencies or uses Git submodules.

## GoogleTest

- Directory: `googletest/`
- Version: `1.17.0` (upstream tag `v1.17.0`)
- Upstream: https://github.com/google/googletest
- Source archive: https://codeload.github.com/google/googletest/tar.gz/refs/tags/v1.17.0
- Archive SHA-256: `65fab701d9829d38cb77c14acdc431d2108bfdbf8979e40eb8ae567edf10b27c`
- License: [BSD-3-Clause](googletest/LICENSE)
- Local modifications: none; the complete upstream archive is preserved.

GoogleTest is built only when `BUILD_TESTING=ON`. GoogleMock sources are included
in the upstream distribution, but GoogleMock and GoogleTest installation are
disabled by the parent CMake configuration.

When updating, verify the new release archive, replace the upstream source tree,
update this version and checksum record, and run the tests on all three platforms.

## nlohmann/json

- Public include directory: `../include/tos/vendor/nlohmann/`
- Version: `3.11.3` (upstream tag `v3.11.3`)
- Upstream: https://github.com/nlohmann/json
- Source archive: https://codeload.github.com/nlohmann/json/tar.gz/refs/tags/v3.11.3
- Archive SHA-256: `0d8ef5af7f9794e3263480193c491549b2ba6cc74bb018906202ada498a79406`
- Vendored files: upstream `single_include/nlohmann/json.hpp` and `LICENSE.MIT` only
- Header SHA-256: `9bea4c8066ef4a1c206b2be5a36302f8926f7fdc6087af5d20b417d0cf103ea6`
- License: [MIT](../include/tos/vendor/nlohmann/LICENSE.MIT)
- Local modifications: none

`<tos/base/json.h>` re-exports `nlohmann::json` and related primary types in namespace
`tos`. It requires no runtime library, separate CMake target, or network download.

## fkYAML

- Public include directory: `../include/tos/vendor/fkyaml/`
- Version: `0.4.4` (upstream tag `v0.4.4`)
- Upstream: https://github.com/fktn-k/fkYAML
- Source archive: https://codeload.github.com/fktn-k/fkYAML/tar.gz/refs/tags/v0.4.4
- Archive SHA-256: `75fa1ce37480ac2ef47b820bfdba04894d4f19ac122ad59d892601553aa45c4e`
- Vendored files: upstream `single_include/fkYAML/node.hpp`, `fkyaml_fwd.hpp`, and `LICENSE.txt`
- Header SHA-256: `87a29ff6d8db6805552cf055de18b53503ecfdcbbb7262eabf78e93dbbe7d639`
- License: [MIT](../include/tos/vendor/fkyaml/LICENSE.txt)
- Local modifications: none

`<tos/base/yaml.h>` exposes the header-only YAML parser. It requires no runtime library,
separate CMake target, or network download. fkYAML parser exceptions are exposed
only through this third-party header; a future tos configuration API must translate
expected input failures to `tos::Result<T>`.

## fmt

- Public include directory: `../include/tos/vendor/fmt/`
- Version: `12.2.0` (upstream tag `12.2.0`)
- Upstream: https://github.com/fmtlib/fmt
- Source archive: https://codeload.github.com/fmtlib/fmt/tar.gz/refs/tags/12.2.0
- Archive SHA-256: `8b852bb5aa6e7d8564f9e81394055395dd1d1936d38dfd3a17792a02bebd7af0`
- Vendored files: the complete upstream `include/fmt/` public header directory and `LICENSE`
- Representative `format.h` SHA-256: `b95f7c5b45d3d93e7cd8e691ae3040282ebcfe2d9c390f2c19d15ffcfae80a9c`
- License: [MIT](../include/tos/vendor/fmt/LICENSE)
- Local modifications: none

`<tos/base/format.h>` exposes fmt with `FMT_HEADER_ONLY` enabled, so it requires no
runtime library, separate CMake target, or network download. When updating either
header-only dependency, verify the release archive and vendored header checksums,
replace the public headers and license from that archive, and run the tests on all
three platforms.

## span-lite

- Public include directory: `../include/tos/vendor/nonstd/`
- Version: `0.11.0` (upstream tag `v0.11.0`)
- Upstream: https://github.com/nonstd-lite/span-lite
- Source archive: https://codeload.github.com/nonstd-lite/span-lite/tar.gz/refs/tags/v0.11.0
- Archive SHA-256: `ef4e028e18ff21044da4b4641ca1bc8a2e2d656e2028322876c0e1b9b6904f9d`
- Vendored files: upstream `include/nonstd/span.hpp` and `LICENSE.txt` only
- Header SHA-256: `c8ad2bd66c33426e5792dcc3d450f76bbba468dc8ec433856df05e8ab302e67e`
- License: [Boost Software License 1.0](../include/tos/vendor/nonstd/LICENSE.txt)
- Local modifications: none

`<tos/base/span.h>` exposes `tos::span<T, Extent>` and `tos::dynamic_extent` over
span-lite. It requires no runtime library, separate CMake target, or network
download. The view is non-owning; its caller remains responsible for the lifetime
and synchronization of the referenced storage.

## OpenSSL

- Delivery: system development package, not vendored
- Minimum version: `3.0`
- Upstream: https://www.openssl.org/
- License: Apache License 2.0; see the system package's distributed license
- CMake requirement: `find_package(OpenSSL 3.0 REQUIRED COMPONENTS Crypto)`

`tos::base` links `OpenSSL::Crypto`; no OpenSSL public header is exposed by
`<tos/base/crypto.h>`. Consumers need the OpenSSL headers and `libcrypto` available
when configuring and linking. This dependency is discovered locally and is never
downloaded by tos. Because it is supplied by the build environment, no vendored
archive checksum applies; deployments must track their package manager's OpenSSL
security updates.

## CLI11

- Public include directory: `../include/tos/vendor/cli11/`
- Version: `2.7.2` (upstream tag `v2.7.2`)
- Upstream: https://github.com/CLIUtils/CLI11
- Release header: https://github.com/CLIUtils/CLI11/releases/download/v2.7.2/CLI11.hpp
- License source: https://raw.githubusercontent.com/CLIUtils/CLI11/v2.7.2/LICENSE
- Vendored files: upstream release single header `CLI11.hpp` and `LICENSE` only
- Header SHA-256: `ffa9a30da295c5858fb5f91f9f45771bab09471d7010a34c7c68c857a330dd76`
- License SHA-256: `cfc76368aef8f51868fa1cda6bcfa26e583406a4cf8e48e725e7624df24d8855`
- License: [BSD-3-Clause](../include/tos/vendor/cli11/LICENSE)
- Local modifications: none

`<tos/base/cli.h>` exposes the upstream `CLI::` API directly. It requires no
runtime library, separate CMake target, or network download. CLI11 parsing and
help/version exceptions are handled at application boundaries; no tos parser
compatibility layer is provided. The tos entry header marks the upstream include
as a Clang system header to contain libc++ codecvt deprecation diagnostics under
`-Werror`; the upstream files remain unchanged. When updating, replace the release header and
license, record their new checksums, and run the integration tests on all three
platforms.
