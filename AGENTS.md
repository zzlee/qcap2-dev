# Repository Guidelines

## Project Structure & Module Organization
- `include/`: Public QCAP headers, split into modular `qcap.*.h` files plus umbrella headers like `qcap.h`.

## Build, Test, and Development Commands
- To setup the environment and install dependencies, run `./setup-env.sh`.
- Example consumer compile (from README): `g++ your_program.cpp -I./include -L./lib -lqcap -lavcodec -lavformat -lavutil -lswscale -lboost_system -o your_program`.
  Adjust paths to where the real QCAP library lives; this repo does not ship binaries.

## Coding Style & Naming Conventions
- Headers use tabs for indentation and align enums and macro blocks with tab spacing; keep that style consistent.
- Public API headers are named `qcap.<module>.h` (e.g., `qcap.broadcast.h`).
- Mock files follow `mock_qcap_<module>.h/.cpp`.
- Prefer C-compatible APIs in headers with `extern "C"` guards; avoid C++-only constructs in public headers.

## Testing Guidelines
- No automated test suite is currently included.

## Commit & Pull Request Guidelines
- Commit messages follow a Conventional Commit style prefix (e.g., `feat: ...`, `fix: ...`).
- PRs should describe the API surface area touched, include regeneration steps if headers changed, and note any consumer impact (breaking/behavioral changes).

## Security & Configuration Tips
- The headers expose OS- and device-level interfaces; avoid checking in credentials, device IDs, or private endpoints in examples.
