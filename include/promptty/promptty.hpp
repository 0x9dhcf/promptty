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
#include <unordered_map>
#include <utility>
#include <vector>

namespace ptty {

// --- Detail utilities ---

namespace detail {
extern volatile std::sig_atomic_t got_sigint;
void sigint_handler(int sig);

/// Returns the byte length of a UTF-8 codepoint from its lead byte (1-4).
std::size_t utf8_codepoint_length(char lead);

/// Decodes one UTF-8 codepoint at pos. Returns {codepoint, byte_length}.
std::pair<char32_t, std::size_t> utf8_decode(std::string_view s, std::size_t pos);

/// Advances past one grapheme cluster (base codepoint + combining marks).
std::size_t utf8_next_grapheme(std::string_view s, std::size_t pos);

/// Moves back one grapheme cluster.
std::size_t utf8_prev_grapheme(std::string_view s, std::size_t pos);

/// Terminal display width of a single codepoint (0 for combining, 2 for wide).
int codepoint_display_width(char32_t cp);

/// Total display width of a UTF-8 string.
std::size_t display_width(std::string_view s);

/// Display width of a string after stripping ANSI escape sequences.
std::size_t ansi_visible_width(std::string_view s);

/// Terminal column/row count via ioctl. Falls back to 80x24.
std::size_t get_terminal_width();
std::size_t get_terminal_height();
} // namespace detail

// --- Prompt ---

/// Prompt string paired with its visible display width. Handles ANSI escapes
/// transparently: visible_width excludes escape sequences so cursor math works.
struct Prompt {
  std::string text;
  std::size_t visible_width {};

  /// Plain text prompt. Visible width computed automatically.
  Prompt(std::string plain); // NOLINT(google-explicit-constructor)
  Prompt(const char *plain); // NOLINT(google-explicit-constructor)

  /// Raw ANSI prompt with caller-supplied visible width.
  Prompt(std::string raw, std::size_t vis_width);
};

// --- Completion ---

/// Passed to the completion callback with the current buffer and cursor position.
struct CompletionRequest {
  std::string_view buffer;
  std::size_t cursor; // byte offset
};

/// Returned by the completion callback: the token range to replace and candidates.
struct CompletionResult {
  std::size_t replace_start;  // byte offset
  std::size_t replace_length; // byte length
  std::vector<std::string> candidates;
};

/// User-supplied completion function.
using CompletionCallback = std::function<CompletionResult(CompletionRequest)>;

// --- Choice menu ---

/// Result of an interactive choice menu selection.
struct ChoiceResult {
  std::size_t index;
  std::string value;
};

// --- Key input ---

/// Recognized key types from raw terminal input.
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
  // Bindable keys (also exposed via bindable_key for type-safe binding).
  ctrl_g,
  ctrl_o,
  ctrl_r,
  ctrl_t,
  ctrl_x,
  ctrl_y,
  page_up,
  page_down,
  f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12,
  paste,
  unknown
};

/// Subset of \p key that embedders are allowed to rebind. Built-in editor
/// navigation keys (Enter, arrows, Ctrl-A/E/K/U/W, history, etc.) are
/// deliberately excluded so they can never be silently overridden.
enum class bindable_key : std::uint8_t {
  ctrl_g,
  ctrl_o,
  ctrl_r,
  ctrl_t,
  ctrl_x,
  ctrl_y,
  page_up,
  page_down,
  f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12,
};

/// Callback invoked when a bound key fires from inside get_line().
using KeyCallback = std::function<void()>;

/// A key press: the key type and, for character keys, the UTF-8 bytes.
struct KeyEvent {
  key k = key::unknown;
  std::string ch;
};

namespace detail {
/// Blocking read of one key event from stdin in raw mode.
KeyEvent read_key();
} // namespace detail

// --- Line editor ---

/// Terminal line editor with UTF-8 support, multiline editing, history,
/// tab completion, and interactive choice menus. Manages raw terminal mode
/// via RAII; restores the original terminal state on destruction.
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

  CompletionCallback completion_cb_;
  struct CompletionState {
    std::size_t replace_start;
    std::size_t replace_length;
    std::vector<std::string> candidates;
    std::size_t index;
    std::string original;
  };
  std::optional<CompletionState> completion_;

  std::unordered_map<bindable_key, KeyCallback> bindings_;

public:
  /// Sets up raw terminal mode and loads history from file (if provided).
  LineEditor(Prompt prompt, // NOLINT(google-explicit-constructor)
             std::optional<std::filesystem::path> history_file = std::nullopt);

  /// Restores terminal and saves history.
  ~LineEditor();

  LineEditor(const LineEditor &) = delete;
  LineEditor &operator=(const LineEditor &) = delete;
  LineEditor(LineEditor &&other) noexcept;
  LineEditor &operator=(LineEditor &&other) noexcept;

  /// Reads one (possibly multiline) input. Returns nullopt on Ctrl-D / EOF.
  std::optional<std::string> get_line();

  /// Registers a tab-completion callback.
  void set_completion(CompletionCallback cb);

  /// Binds \p k to \p cb. The callback fires from inside get_line() when the
  /// key is pressed; the editor refreshes the line afterward. Pass an empty
  /// callback to remove a binding. Only keys in \p bindable_key can be bound,
  /// so editor navigation cannot be silently overridden.
  void bind_key(bindable_key k, KeyCallback cb);

  /// Replaces the prompt in-place without destroying the editor. Safe to call
  /// from inside a key binding callback (unlike constructing a new editor).
  void set_prompt(Prompt prompt);

  /// Presents an interactive choice menu. Returns nullopt on cancel (Ctrl-D / ESC).
  /// An optional header is displayed above the choices.
  std::optional<ChoiceResult> choose(std::span<const std::string> choices,
                                     std::string_view header = "");

private:
  void load_history();
  void save_history();
  void refresh();
  void handle(KeyEvent evt);
  void handle_tab();
  void refresh_menu(std::span<const std::string> choices,
                    std::size_t selected, std::size_t scroll_offset,
                    std::size_t &menu_rows, std::string_view header);

  std::size_t current_line() const;
  std::size_t line_count() const;
  std::size_t line_start(std::size_t n) const;
  std::size_t line_length(std::size_t n) const;
  std::size_t column_width() const;
};

} // namespace ptty
