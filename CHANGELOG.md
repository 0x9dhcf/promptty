# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [0.2.1] - 2026-04-13

### Added
- Bracketed-paste support: pasted multiline text is inserted verbatim at
  the cursor instead of the first newline being interpreted as submit.
  `\r` and `\r\n` inside a paste are normalized to `\n`.

### Changed
- `LineEditor::choose()` post-selection echo now renders as
  `<prompt> <header>: <choice>` with whitespace between the parts.

### Fixed
- Terminal state is restored on abnormal exits (uncaught exceptions,
  `abort()`, fatal signals such as `SIGSEGV` / `SIGABRT` / `SIGTERM`), so
  a crashing host no longer leaves the shell in raw / no-echo mode. Fatal
  signals are re-raised with the default disposition so core dumps and
  sanitizer reports still fire.

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

[Unreleased]: https://github.com/0x9dhcf/promptty/compare/v0.2.1...HEAD
[0.2.1]: https://github.com/0x9dhcf/promptty/releases/tag/v0.2.1
[0.2.0]: https://github.com/0x9dhcf/promptty/releases/tag/v0.2.0
