#include "promptty/promptty.hpp"
#include <print>

int main() {
  // Unicode prompt with emoji and color
  ptty::Prompt prompt("\x1b[35m\xf0\x9f\x94\xae \x1b[0m", 3); // 🔮 + space = 3 columns
  ptty::LineEditor editor(prompt, "./history");

  // --- Choice menu demo ---
  std::vector<std::string> models = {
      "\xf0\x9f\x94\xae claude-opus",   // 🔮
      "\xe2\x9a\xa1 claude-sonnet",      // ⚡
      "\xf0\x9f\xaa\xb6 claude-haiku",  // 🪶
      "\xf0\x9f\xa7\xa0 gpt-4",         // 🧠
      "\xf0\x9f\x92\x8e gemini-pro",    // 💎
      "\xf0\x9f\xa6\x99 llama-3",       // 🦙
      "\xf0\x9f\x8c\x8a mistral-large", // 🌊
  };

  auto choice = editor.choose(models, "Choose a model to start:");
  if (choice) {
    std::println("\xe2\x9c\x85 selected: {} (index {})", choice->value, choice->index); // ✅
  } else {
    std::println("\xe2\x9d\x8c cancelled"); // ❌
    return 0;
  }

  // --- Line editing demo with completion ---
  editor.set_completion([](ptty::CompletionRequest req) -> ptty::CompletionResult {
    std::vector<std::string> commands = {
        "help", "history", "quit", "clear",
        "\xf0\x9f\x91\x8b hello",   // 👋
        "\xf0\x9f\x9a\x80 launch",  // 🚀
        "\xf0\x9f\x94\xa5 burn",    // 🔥
        "\xe2\x9c\x85 done",        // ✅
        "caf\xc3\xa9",              // café
        "\xce\xbb lambda",          // λ
        "\xe4\xbd\xa0\xe5\xa5\xbd", // 你好
    };
    auto start = req.cursor;
    while (start > 0 && req.buffer[start - 1] != ' ' && req.buffer[start - 1] != '\n')
      --start;
    auto prefix = req.buffer.substr(start, req.cursor - start);
    std::vector<std::string> matches;
    for (const auto &cmd : commands) {
      if (cmd.starts_with(prefix))
        matches.push_back(cmd);
    }
    return {start, req.cursor - start, std::move(matches)};
  });

  std::println("\ntab=complete, alt+enter=multiline, ctrl+d=exit\n");

  while (auto line = editor.get_line()) {
    if (*line == "quit")
      break;
    std::println("\xf0\x9f\x93\x9d got: \"{}\"", *line); // 📝
  }
  std::println("\xf0\x9f\x91\x8b bye!"); // 👋
  return 0;
}
