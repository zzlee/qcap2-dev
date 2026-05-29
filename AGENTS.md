# Repository Guidelines

## Project Structure & Module Organization
- `include/`: Public QCAP headers, split into modular `qcap.*.h` files plus umbrella headers like `qcap.h`.

## Build, Test, and Development Commands
- Before refactoring or adding features, read relevant notes in `./wiki/` to understand API semantics and implementation constraints.
- **Docker-Based Building & Testing**:
  - Build the Docker image: `docker build -t qcap2-build-env .`
  - Test in Docker (mounting local workspace, clean build dir): `docker run --rm --user $(id -u):$(id -g) -v $(pwd):/workspace qcap2-build-env sh -c "rm -rf build-docker && mkdir -p build-docker && cd build-docker && cmake .. && make -j\$(nproc) && ctest --output-on-failure"`
  - Launch an interactive development shell: `docker run --rm -it --user $(id -u):$(id -g) -v $(pwd):/workspace qcap2-build-env bash`
  - Build for ARM64 or custom architectures: `docker build --build-arg BASE_IMAGE=arm64v8/ubuntu:24.04 -t qcap2-build-env-arm64 .`
- Example consumer compile (from README): `g++ your_program.cpp -I./include -L./lib -lqcap -lavcodec -lavformat -lavutil -lswscale -lboost_system -o your_program`.
  Adjust paths to where the real QCAP library lives; this repo does not ship binaries.

## Coding Style & Naming Conventions
- Headers use tabs for indentation and align enums and macro blocks with tab spacing; keep that style consistent.
- Public API headers are named `qcap.<module>.h` (e.g., `qcap.broadcast.h`).
- Mock files follow `mock_qcap_<module>.h/.cpp`.
- Prefer C-compatible APIs in headers with `extern "C"` guards; avoid C++-only constructs in public headers.

## Testing Guidelines
- Automated tests are located in the `tests/` directory (e.g. `test_qcap2_sync.cpp`, `test_qcap2_buffer.cpp`).
- You can execute all tests locally via CMake:
  ```bash
  mkdir -p build && cd build
  cmake ..
  make
  ctest --output-on-failure
  ```
  Or inside the Docker build environment.

## Commit & Pull Request Guidelines
- Commit messages follow a Conventional Commit style prefix (e.g., `feat: ...`, `fix: ...`).
- PRs should describe the API surface area touched, include regeneration steps if headers changed, and note any consumer impact (breaking/behavioral changes).

## Security & Configuration Tips
- The headers expose OS- and device-level interfaces; avoid checking in credentials, device IDs, or private endpoints in examples.
