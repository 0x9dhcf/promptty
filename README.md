# promptty

![C++23](https://img.shields.io/badge/C%2B%2B-23-blue.svg)
![build](https://github.com/0x9dhcf/promptty/actions/workflows/ci.yml/badge.svg)
![platform](https://img.shields.io/badge/platform-Linux-informational.svg?logo=linux&logoColor=white)
![license](https://img.shields.io/badge/license-MIT-green.svg)

Lightweight C++ line editor library for terminal applications. UTF-8 aware with
ANSI color support, multiline editing, tab completion, command history, and
interactive choice menus.

## Features

- **Line editing** with cursor movement, kill/yank, and emacs keybindings
- **UTF-8 / Unicode** support with grapheme-aware cursor and display width
- **ANSI color prompts** with correct cursor positioning
- **Multiline editing** via Alt+Enter and backslash continuation
- **Tab completion** with pluggable callback and cycling
- **Choice menu** with arrow-key navigation and scrolling
- **Command history** with file-backed persistence
- **SIGINT handling** with clean terminal restore

## Dependencies

- C++23 compiler (GCC 14+, Clang 18+)
- CMake 3.21+

No external library dependencies.

## Build

```sh
cmake --preset debug
cmake --build --preset debug

cmake --preset release
cmake --build --preset release
```

## Install

```sh
cmake --preset release
cmake --build --preset release
cmake --install build/release --prefix /usr/local
```

## Usage

### Basic line editing

```cpp
#include "promptty/promptty.hpp"

ptty::LineEditor editor(">>> ", "./history");
while (auto line = editor.get_line()) {
  if (*line == "quit") break;
  // process *line
}
```

### Colored prompt

```cpp
ptty::Prompt prompt("\x1b[32m>>> \x1b[0m", 4); // green prompt, 4 visible columns
ptty::LineEditor editor(prompt, "./history");
```

### Tab completion

```cpp
editor.set_completion([](ptty::CompletionRequest req) -> ptty::CompletionResult {
  auto start = req.cursor;
  while (start > 0 && req.buffer[start - 1] != ' ') --start;
  auto prefix = req.buffer.substr(start, req.cursor - start);

  std::vector<std::string> matches;
  for (auto& cmd : {"help", "history", "quit"})
    if (std::string_view(cmd).starts_with(prefix))
      matches.emplace_back(cmd);

  return {start, req.cursor - start, std::move(matches)};
});
```

### Choice menu

```cpp
std::vector<std::string> options = {"option A", "option B", "option C"};
auto choice = editor.choose(options);
if (choice) {
  // choice->index, choice->value
}
```

### Multiline input

Press **Alt+Enter** to insert a newline, or end a line with `\` and press
**Enter** for continuation. Arrow keys navigate within the multiline buffer.

## API Overview

| Header | Description |
|--------|-------------|
| `promptty/promptty.hpp` | All types and the `LineEditor` class |

| Type | Purpose |
|------|---------|
| `Prompt` | Prompt string with ANSI-aware visible width |
| `LineEditor` | Line editor with history, completion, and menus |
| `CompletionRequest` | Buffer and cursor position for completion callback |
| `CompletionResult` | Replacement range and candidate list |
| `CompletionCallback` | `std::function` for pluggable completion |
| `ChoiceResult` | Selected index and value from choice menu |
| `KeyEvent` | Key type and UTF-8 character data |

## License

[MIT](LICENSE)
