#pragma once

#include <csignal>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <termios.h>
#include <utility>
#include <vector>

namespace ptty {

namespace detail {
extern volatile std::sig_atomic_t got_sigint;
void sigint_handler(int sig);

// UTF-8 utilities
std::size_t utf8_codepoint_length(char lead);
std::pair<char32_t, std::size_t> utf8_decode(std::string_view s, std::size_t pos);
std::size_t utf8_next_grapheme(std::string_view s, std::size_t pos);
std::size_t utf8_prev_grapheme(std::string_view s, std::size_t pos);
int codepoint_display_width(char32_t cp);
std::size_t display_width(std::string_view s);

// ANSI escape utilities
std::size_t ansi_visible_width(std::string_view s);

// Terminal utilities
std::size_t get_terminal_width();
std::size_t get_terminal_height();
} // namespace detail

struct Prompt {
  std::string text;
  std::size_t visible_width {};

  // Plain text prompt (no ANSI escapes) — implicit to allow string conversion
  Prompt(std::string plain); // NOLINT(google-explicit-constructor)
  Prompt(const char *plain); // NOLINT(google-explicit-constructor)

  // Raw ANSI prompt with explicit visible width
  Prompt(std::string raw, std::size_t vis_width);
};

// Tab completion types
struct CompletionRequest {
  std::string_view buffer;
  std::size_t cursor; // byte offset
};

struct CompletionResult {
  std::size_t replace_start;  // byte offset of token to replace
  std::size_t replace_length; // byte length of token to replace
  std::vector<std::string> candidates;
};

using CompletionCallback = std::function<CompletionResult(CompletionRequest)>;

// Choice menu result
struct ChoiceResult {
  std::size_t index;
  std::string value;
};

enum class key : std::uint8_t {
  character,
  enter,
  alt_enter,
  escape,
  tab,
  backspace,
  del,
  up,
  down,
  left,
  right,
  home,
  end,
  ctrl_a,
  ctrl_e,
  ctrl_k,
  ctrl_n,
  ctrl_p,
  ctrl_u,
  ctrl_w,
  ctrl_d,
  ctrl_l,
  unknown
};

struct KeyEvent {
  key k = key::unknown;
  std::string ch;
};

namespace detail {
KeyEvent read_key();
} // namespace detail

class LineEditor {
  struct termios original_ {};
  std::string buffer_;
  std::string saved_buffer_;
  Prompt prompt_;
  Prompt continuation_;
  std::size_t cursor_ {};
  std::deque<std::string> history_;
  std::size_t hindex_ {};
  std::size_t display_rows_ = 1;
  bool owns_terminal_ = false;
  void (*prev_sigint_)(int) = nullptr;
  std::optional<std::filesystem::path> history_file_;

  // Completion state
  CompletionCallback completion_cb_;
  struct CompletionState {
    std::size_t replace_start;
    std::size_t replace_length;
    std::vector<std::string> candidates;
    std::size_t index;
    std::string original; // original token before completion started
  };
  std::optional<CompletionState> completion_;

public:
  LineEditor(Prompt prompt, // NOLINT(google-explicit-constructor)
             std::optional<std::filesystem::path> history_file = std::nullopt);
  ~LineEditor();

  LineEditor(const LineEditor &) = delete;
  LineEditor &operator=(const LineEditor &) = delete;
  LineEditor(LineEditor &&other) noexcept;
  LineEditor &operator=(LineEditor &&other) noexcept;

  std::optional<std::string> get_line();
  void set_completion(CompletionCallback cb);
  std::optional<ChoiceResult> choose(std::span<const std::string> choices);

private:
  void load_history();
  void save_history();
  void refresh();
  void handle(KeyEvent evt);
  void handle_tab();
  void refresh_menu(std::span<const std::string> choices,
                    std::size_t selected, std::size_t scroll_offset,
                    std::size_t &menu_rows);

  // Multiline buffer helpers
  std::size_t current_line() const;
  std::size_t line_count() const;
  std::size_t line_start(std::size_t n) const;
  std::size_t line_length(std::size_t n) const;
  std::size_t column_width() const;
};

} // namespace ptty
