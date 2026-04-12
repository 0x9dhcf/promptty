# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [0.2.0] - 2026-04-11

### Added
- utf8proc-based grapheme handling so multi-codepoint characters (emoji,
  combining marks) are treated as a single unit for cursor movement, deletion,
  and width calculation.
- User-configurable key bindings.
- Optional `header` parameter on `choose()` for rendering a caption above the
  selection list.
- `promptty/version.hpp` header exposing `PROMPTTY_VERSION_MAJOR`,
  `PROMPTTY_VERSION_MINOR`, `PROMPTTY_VERSION_PATCH` and
  `PROMPTTY_VERSION_STRING` for downstream consumers.

### Changed
- Build now requires GCC 15 for full C++23 `<print>` / `std::println` support;
  CI updated accordingly.
- The library is now built as both a static and a shared library. The shared
  library carries `VERSION` and `SOVERSION` properties, producing a proper
  `libpromptty.so` -> `libpromptty.so.0` -> `libpromptty.so.0.2.0` symlink
  chain.

[Unreleased]: https://github.com/0x9dhcf/promptty/compare/v0.2.0...HEAD
[0.2.0]: https://github.com/0x9dhcf/promptty/releases/tag/v0.2.0
