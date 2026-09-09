# Third-Party Libraries

Dependencies are stored as source code in this directory and committed with tos.
CMake builds them locally; no dependency download or Git submodule setup is needed.

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
