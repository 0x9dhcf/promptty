#include "promptty/promptty.hpp"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <poll.h>
#include <print>
#include <sys/ioctl.h>
#include <unistd.h>
#include <wchar.h>

namespace ptty {

namespace detail {
volatile std::sig_atomic_t got_sigint = 0;

void sigint_handler(int /*sig*/) {
  got_sigint = 1;
}

// --- UTF-8 utilities ---

std::size_t utf8_codepoint_length(char lead) {
  auto b = static_cast<unsigned char>(lead);
  if (b < 0x80)
    return 1;
  if ((b & 0xE0) == 0xC0)
    return 2;
  if ((b & 0xF0) == 0xE0)
    return 3;
  if ((b & 0xF8) == 0xF0)
    return 4;
  return 1;
}

std::pair<char32_t, std::size_t> utf8_decode(std::string_view s, std::size_t pos) {
  if (pos >= s.size())
    return {0, 0};
  auto b0 = static_cast<unsigned char>(s[pos]);
  if (b0 < 0x80)
    return {b0, 1};

  std::size_t len = utf8_codepoint_length(s[pos]);
  if (pos + len > s.size())
    return {0xFFFD, 1};

  char32_t cp = 0;
  switch (len) {
  case 2:
    cp = (b0 & 0x1F);
    cp = (cp << 6) | (static_cast<unsigned char>(s[pos + 1]) & 0x3F);
    break;
  case 3:
    cp = (b0 & 0x0F);
    cp = (cp << 6) | (static_cast<unsigned char>(s[pos + 1]) & 0x3F);
    cp = (cp << 6) | (static_cast<unsigned char>(s[pos + 2]) & 0x3F);
    break;
  case 4:
    cp = (b0 & 0x07);
    cp = (cp << 6) | (static_cast<unsigned char>(s[pos + 1]) & 0x3F);
    cp = (cp << 6) | (static_cast<unsigned char>(s[pos + 2]) & 0x3F);
    cp = (cp << 6) | (static_cast<unsigned char>(s[pos + 3]) & 0x3F);
    break;
  default:
    return {0xFFFD, 1};
  }
  return {cp, len};
}

static bool is_combining(char32_t cp) {
  return (cp >= 0x0300 && cp <= 0x036F) || (cp >= 0x0483 && cp <= 0x0489) ||
         (cp >= 0x0591 && cp <= 0x05BD) || (cp >= 0x0610 && cp <= 0x061A) ||
         (cp >= 0x064B && cp <= 0x065F) || (cp == 0x0670) || (cp >= 0x06D6 && cp <= 0x06DC) ||
         (cp >= 0x06DF && cp <= 0x06E4) || (cp >= 0x06E7 && cp <= 0x06E8) ||
         (cp >= 0x06EA && cp <= 0x06ED) || (cp >= 0x0730 && cp <= 0x074A) ||
         (cp >= 0x0900 && cp <= 0x0903) || (cp >= 0x093A && cp <= 0x094F) ||
         (cp >= 0x0951 && cp <= 0x0957) || (cp >= 0x1AB0 && cp <= 0x1AFF) ||
         (cp >= 0x1DC0 && cp <= 0x1DFF) || (cp >= 0x20D0 && cp <= 0x20FF) ||
         (cp >= 0xFE00 && cp <= 0xFE0F) || (cp >= 0xFE20 && cp <= 0xFE2F) ||
         (cp >= 0xE0100 && cp <= 0xE01EF);
}

std::size_t utf8_next_grapheme(std::string_view s, std::size_t pos) {
  if (pos >= s.size())
    return s.size();
  auto [cp, len] = utf8_decode(s, pos);
  pos += len;
  while (pos < s.size()) {
    auto [next_cp, next_len] = utf8_decode(s, pos);
    if (!is_combining(next_cp))
      break;
    pos += next_len;
  }
  return pos;
}

std::size_t utf8_prev_grapheme(std::string_view s, std::size_t pos) {
  if (pos == 0)
    return 0;
  auto step_back = [&]() {
    if (pos == 0)
      return;
    --pos;
    while (pos > 0 && (static_cast<unsigned char>(s[pos]) & 0xC0) == 0x80)
      --pos;
  };
  step_back();
  while (pos > 0) {
    auto [cp, len] = utf8_decode(s, pos);
    if (!is_combining(cp))
      break;
    step_back();
  }
  return pos;
}

int codepoint_display_width(char32_t cp) {
  if (cp == 0)
    return 0;
  if (is_combining(cp))
    return 0;
  int w = wcwidth(static_cast<wchar_t>(cp));
  return (w < 0) ? 1 : w;
}

std::size_t display_width(std::string_view s) {
  std::size_t width = 0;
  std::size_t pos = 0;
  while (pos < s.size()) {
    auto [cp, len] = utf8_decode(s, pos);
    int w = codepoint_display_width(cp);
    if (w > 0)
      width += static_cast<std::size_t>(w);
    pos += len;
  }
  return width;
}

// --- ANSI escape utilities ---

std::size_t ansi_visible_width(std::string_view s) {
  std::string stripped;
  stripped.reserve(s.size());
  std::size_t i = 0;
  while (i < s.size()) {
    if (s[i] == '\x1b' && i + 1 < s.size() && s[i + 1] == '[') {
      i += 2;
      while (i < s.size() && s[i] != 'm')
        ++i;
      if (i < s.size())
        ++i;
    } else {
      stripped += s[i++];
    }
  }
  return display_width(stripped);
}

// --- Terminal utilities ---

std::size_t get_terminal_width() {
  struct winsize ws{};
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0)
    return ws.ws_col;
  return 80;
}

std::size_t get_terminal_height() {
  struct winsize ws{};
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_row > 0)
    return ws.ws_row;
  return 24;
}

} // namespace detail

// --- Prompt ---

Prompt::Prompt(std::string plain)
    : text(std::move(plain)), visible_width(detail::ansi_visible_width(text)) {
}

Prompt::Prompt(const char* plain) : text(plain), visible_width(detail::ansi_visible_width(text)) {
}

Prompt::Prompt(std::string raw, std::size_t vis_width)
    : text(std::move(raw)), visible_width(vis_width) {
}

namespace detail {

// --- read_key ---

static KeyEvent make_key(key k) {
  return {.k = k, .ch = {}};
}

KeyEvent read_key() {
  if (got_sigint != 0) {
    return make_key(key::ctrl_d);
  }
  char chr{};
  if (::read(STDIN_FILENO, &chr, 1) <= 0) {
    if (got_sigint != 0)
      return make_key(key::ctrl_d);
    return make_key(key::unknown);
  }

  switch (chr) {
  case '\n':
    return make_key(key::enter);
  case '\t':
    return make_key(key::tab);
  case '\x7f':
  case '\x08':
    return make_key(key::backspace);
  case '\x01':
    return make_key(key::ctrl_a);
  case '\x04':
    return make_key(key::ctrl_d);
  case '\x05':
    return make_key(key::ctrl_e);
  case '\x0b':
    return make_key(key::ctrl_k);
  case '\x0c':
    return make_key(key::ctrl_l);
  case '\x0e':
    return make_key(key::ctrl_n);
  case '\x10':
    return make_key(key::ctrl_p);
  case '\x15':
    return make_key(key::ctrl_u);
  case '\x17':
    return make_key(key::ctrl_w);
  default:
    break;
  }

  if (chr == '\x1b') {
    // Check if more bytes are available (escape sequence vs bare ESC)
    struct pollfd pfd = {.fd = STDIN_FILENO, .events = POLLIN, .revents = 0};
    if (poll(&pfd, 1, 50) <= 0) {
      // No data within 50ms — bare ESC
      return make_key(key::escape);
    }

    char seq0{};
    if (::read(STDIN_FILENO, &seq0, 1) <= 0) {
      return make_key(key::escape);
    }

    // Alt+Enter: ESC followed by newline
    if (seq0 == '\n') {
      return make_key(key::alt_enter);
    }

    // CSI sequences: ESC [
    if (seq0 == '[') {
      char seq1{};
      if (::read(STDIN_FILENO, &seq1, 1) <= 0) {
        return make_key(key::unknown);
      }
      switch (seq1) {
      case 'A':
        return make_key(key::up);
      case 'B':
        return make_key(key::down);
      case 'C':
        return make_key(key::right);
      case 'D':
        return make_key(key::left);
      case 'H':
        return make_key(key::home);
      case 'F':
        return make_key(key::end);
      case '3': {
        char tilde{};
        ::read(STDIN_FILENO, &tilde, 1);
        return make_key(key::del);
      }
      default:
        return make_key(key::unknown);
      }
    }
    return make_key(key::unknown);
  }

  // UTF-8 multi-byte sequence
  auto b = static_cast<unsigned char>(chr);
  if (b >= 0x80) {
    std::size_t expected = utf8_codepoint_length(chr);
    std::string seq(1, chr);
    for (std::size_t i = 1; i < expected; ++i) {
      char cont{};
      if (::read(STDIN_FILENO, &cont, 1) <= 0)
        break;
      seq += cont;
    }
    return {.k = key::character, .ch = std::move(seq)};
  }

  // ASCII printable
  if (b >= 0x20 && b < 0x7F) {
    return {.k = key::character, .ch = std::string(1, chr)};
  }

  return make_key(key::unknown);
}
} // namespace detail

// --- LineEditor ---

LineEditor::LineEditor(Prompt prompt, std::optional<std::filesystem::path> history_file)
    : prompt_(std::move(prompt)), continuation_(std::string(prompt_.visible_width, ' ')),
      owns_terminal_(true), prev_sigint_(std::signal(SIGINT, detail::sigint_handler)),
      history_file_(std::move(history_file)) {
  ::tcgetattr(STDIN_FILENO, &original_);
  struct termios raw = original_;
  raw.c_lflag &= static_cast<tcflag_t>(~(ICANON | ECHO));
  raw.c_cc[VMIN] = 1;
  raw.c_cc[VTIME] = 0;
  ::tcsetattr(STDIN_FILENO, TCSANOW, &raw);
  load_history();
}

LineEditor::~LineEditor() {
  save_history();
  if (owns_terminal_)
    ::tcsetattr(STDIN_FILENO, TCSAFLUSH, &original_);
  if (detail::got_sigint != 0) {
    std::signal(SIGINT, SIG_DFL);
    std::raise(SIGINT);
  } else {
    std::signal(SIGINT, prev_sigint_);
  }
}

LineEditor::LineEditor(LineEditor&& other) noexcept
    : original_{other.original_}, buffer_{std::move(other.buffer_)},
      saved_buffer_{std::move(other.saved_buffer_)}, prompt_{std::move(other.prompt_)},
      continuation_{std::move(other.continuation_)}, cursor_{other.cursor_},
      history_{std::move(other.history_)}, hindex_{other.hindex_},
      display_rows_{other.display_rows_}, owns_terminal_{other.owns_terminal_},
      prev_sigint_{other.prev_sigint_}, history_file_{std::move(other.history_file_)} {
  other.owns_terminal_ = false;
}

LineEditor& LineEditor::operator=(LineEditor&& other) noexcept {
  if (this != &other) {
    if (owns_terminal_)
      ::tcsetattr(STDIN_FILENO, TCSAFLUSH, &original_);

    original_ = other.original_;
    buffer_ = std::move(other.buffer_);
    saved_buffer_ = std::move(other.saved_buffer_);
    prompt_ = std::move(other.prompt_);
    continuation_ = std::move(other.continuation_);
    history_file_ = std::move(other.history_file_);
    cursor_ = other.cursor_;
    history_ = std::move(other.history_);
    hindex_ = other.hindex_;
    display_rows_ = other.display_rows_;
    owns_terminal_ = other.owns_terminal_;
    prev_sigint_ = other.prev_sigint_;

    other.owns_terminal_ = false;
  }
  return *this;
}

// --- Multiline buffer helpers ---

std::size_t LineEditor::current_line() const {
  std::size_t line = 0;
  for (std::size_t i = 0; i < cursor_ && i < buffer_.size(); ++i) {
    if (buffer_[i] == '\n')
      ++line;
  }
  return line;
}

std::size_t LineEditor::line_count() const {
  std::size_t count = 1;
  for (char c : buffer_) {
    if (c == '\n')
      ++count;
  }
  return count;
}

std::size_t LineEditor::line_start(std::size_t n) const {
  if (n == 0)
    return 0;
  std::size_t line = 0;
  for (std::size_t i = 0; i < buffer_.size(); ++i) {
    if (buffer_[i] == '\n') {
      ++line;
      if (line == n)
        return i + 1;
    }
  }
  return buffer_.size();
}

std::size_t LineEditor::line_length(std::size_t n) const {
  std::size_t start = line_start(n);
  auto nl = buffer_.find('\n', start);
  if (nl == std::string::npos)
    return buffer_.size() - start;
  return nl - start;
}

std::size_t LineEditor::column_width() const {
  std::size_t ls = line_start(current_line());
  return detail::display_width(std::string_view(buffer_).substr(ls, cursor_ - ls));
}

// --- get_line ---

std::optional<std::string> LineEditor::get_line() {
  buffer_.clear();
  cursor_ = 0;
  hindex_ = 0;
  display_rows_ = 1;
  std::print("{}", prompt_.text);
  std::fflush(stdout);
  for (;;) {
    auto evt = detail::read_key();

    if (evt.k == key::ctrl_d) {
      std::print("\r\n");
      return std::nullopt;
    }
    if (evt.k == key::enter) {
      // Backslash-continuation: if buffer ends with '\', replace with newline
      if (!buffer_.empty() && buffer_.back() == '\\') {
        buffer_.pop_back();
        buffer_ += '\n';
        cursor_ = buffer_.size();
        refresh();
        continue;
      }
      std::print("\r\n");
      saved_buffer_.clear();
      history_.push_front(buffer_);
      if (hindex_ > 0)
        hindex_--;
      return buffer_;
    }

    handle(evt);
  }
}

// --- History ---

void LineEditor::load_history() {
  if (history_file_) {
    std::ifstream file(*history_file_);
    if (!file)
      return;
    std::string line;
    while (std::getline(file, line)) {
      // Decode \x1f back to \n for multiline entries
      std::string decoded;
      decoded.reserve(line.size());
      for (char c : line) {
        decoded += (c == '\x1f') ? '\n' : c;
      }
      history_.push_back(std::move(decoded));
    }
  }
}

void LineEditor::save_history() {
  if (history_file_) {
    std::ofstream file(*history_file_, std::ios::trunc);
    if (!file)
      return;
    for (const auto& entry : history_) {
      // Encode \n as \x1f to keep one entry per file line
      for (char c : entry) {
        file.put((c == '\n') ? '\x1f' : c);
      }
      file.put('\n');
    }
  }
}

// --- Multiline refresh ---

void LineEditor::refresh() {
  // Move cursor to start of our display region
  if (display_rows_ > 1) {
    std::print("\x1b[{}A", display_rows_ - 1);
  }
  std::print("\r\x1b[J"); // carriage return + clear to end of screen

  // Split buffer into lines
  std::size_t term_width = detail::get_terminal_width();
  std::size_t rows = 0;
  std::size_t cursor_row = 0;
  std::size_t cursor_col = 0;
  std::size_t num_lines = line_count();

  for (std::size_t ln = 0; ln < num_lines; ++ln) {
    const auto& p = (ln == 0) ? prompt_ : continuation_;
    std::size_t ls = line_start(ln);
    std::size_t ll = line_length(ln);
    auto line_sv = std::string_view(buffer_).substr(ls, ll);

    // Print prompt/continuation + line content
    if (ln > 0)
      std::print("\r\n");
    std::print("{}{}", p.text, line_sv);

    // Calculate how many terminal rows this line occupies
    std::size_t line_display_width = p.visible_width + detail::display_width(line_sv);
    std::size_t line_rows =
        (line_display_width == 0) ? 1 : ((line_display_width - 1) / term_width + 1);
    rows += line_rows;

    // Track cursor position if cursor is on this line
    if (cursor_ >= ls && cursor_ <= ls + ll) {
      std::size_t col_in_line =
          detail::display_width(std::string_view(buffer_).substr(ls, cursor_ - ls));
      std::size_t abs_col = p.visible_width + col_in_line;
      // Which row within this line (due to wrapping)
      std::size_t wrap_row = abs_col / term_width;
      cursor_row = (rows - line_rows) + wrap_row;
      cursor_col = abs_col % term_width;
    }
  }

  // Position cursor: move from bottom to cursor_row, then set column
  std::size_t rows_from_bottom = rows - 1 - cursor_row;
  if (rows_from_bottom > 0) {
    std::print("\x1b[{}A", rows_from_bottom);
  }
  std::print("\r");
  if (cursor_col > 0) {
    std::print("\x1b[{}C", cursor_col);
  }

  display_rows_ = rows;
  std::fflush(stdout);
}

// --- Key handling ---

void LineEditor::set_completion(CompletionCallback cb) {
  completion_cb_ = std::move(cb);
}

void LineEditor::handle_tab() {
  if (!completion_cb_)
    return;

  if (!completion_) {
    // First tab press — request completions
    auto result = completion_cb_({.buffer = buffer_, .cursor = cursor_});
    if (result.candidates.empty()) {
      std::print("\x07"); // bell
      std::fflush(stdout);
      return;
    }
    if (result.candidates.size() == 1) {
      // Single candidate — auto-insert
      buffer_.erase(result.replace_start, result.replace_length);
      buffer_.insert(result.replace_start, result.candidates[0]);
      cursor_ = result.replace_start + result.candidates[0].size();
      return;
    }
    // Multiple candidates — start cycling
    std::string original = buffer_.substr(result.replace_start, result.replace_length);
    completion_ = CompletionState{
        .replace_start = result.replace_start,
        .replace_length = result.replace_length,
        .candidates = std::move(result.candidates),
        .index = 0,
        .original = std::move(original),
    };
    // Insert first candidate
    buffer_.erase(completion_->replace_start, completion_->replace_length);
    buffer_.insert(completion_->replace_start, completion_->candidates[0]);
    cursor_ = completion_->replace_start + completion_->candidates[0].size();
    completion_->replace_length = completion_->candidates[0].size();
  } else {
    // Subsequent tab — cycle to next candidate
    completion_->index = (completion_->index + 1) % completion_->candidates.size();
    auto& candidate = completion_->candidates[completion_->index];
    buffer_.erase(completion_->replace_start, completion_->replace_length);
    buffer_.insert(completion_->replace_start, candidate);
    cursor_ = completion_->replace_start + candidate.size();
    completion_->replace_length = candidate.size();
  }
}

void LineEditor::handle(KeyEvent evt) {
  // Clear completion state on any non-tab key
  if (evt.k != key::tab) {
    completion_.reset();
  }

  switch (evt.k) {
  case key::character:
    buffer_.insert(cursor_, evt.ch);
    cursor_ += evt.ch.size();
    break;
  case key::alt_enter:
    buffer_.insert(cursor_, 1, '\n');
    ++cursor_;
    break;
  case key::backspace:
    if (cursor_ > 0) {
      // If at start of a line and prev char is \n, just delete the newline
      if (buffer_[cursor_ - 1] == '\n') {
        buffer_.erase(--cursor_, 1);
      } else {
        auto prev = detail::utf8_prev_grapheme(buffer_, cursor_);
        buffer_.erase(prev, cursor_ - prev);
        cursor_ = prev;
      }
    }
    break;
  case key::del:
    if (cursor_ < buffer_.size()) {
      if (buffer_[cursor_] == '\n') {
        buffer_.erase(cursor_, 1);
      } else {
        auto next = detail::utf8_next_grapheme(buffer_, cursor_);
        buffer_.erase(cursor_, next - cursor_);
      }
    }
    break;
  case key::left:
    if (cursor_ > 0) {
      if (buffer_[cursor_ - 1] == '\n') {
        --cursor_;
      } else {
        cursor_ = detail::utf8_prev_grapheme(buffer_, cursor_);
      }
    }
    break;
  case key::right:
    if (cursor_ < buffer_.size()) {
      if (buffer_[cursor_] == '\n') {
        ++cursor_;
      } else {
        cursor_ = detail::utf8_next_grapheme(buffer_, cursor_);
      }
    }
    break;
  case key::up: {
    std::size_t cur_ln = current_line();
    if (cur_ln > 0) {
      // Move to same column on previous line
      std::size_t col = column_width();
      std::size_t prev_start = line_start(cur_ln - 1);
      std::size_t prev_len = line_length(cur_ln - 1);
      auto prev_line = std::string_view(buffer_).substr(prev_start, prev_len);
      // Find byte offset that gives us the target column
      std::size_t w = 0;
      std::size_t pos = 0;
      while (pos < prev_line.size()) {
        auto [cp, len] = detail::utf8_decode(prev_line, pos);
        int cw = detail::codepoint_display_width(cp);
        if (w + static_cast<std::size_t>(std::max(cw, 0)) > col)
          break;
        w += static_cast<std::size_t>(std::max(cw, 0));
        pos += len;
      }
      cursor_ = prev_start + pos;
    } else {
      // First line — navigate history
      if (hindex_ == 0)
        saved_buffer_ = buffer_;
      if (hindex_ < history_.size())
        buffer_ = history_[hindex_++];
      cursor_ = buffer_.size();
    }
    break;
  }
  case key::down: {
    std::size_t cur_ln = current_line();
    if (cur_ln + 1 < line_count()) {
      // Move to same column on next line
      std::size_t col = column_width();
      std::size_t next_start = line_start(cur_ln + 1);
      std::size_t next_len = line_length(cur_ln + 1);
      auto next_line = std::string_view(buffer_).substr(next_start, next_len);
      std::size_t w = 0;
      std::size_t pos = 0;
      while (pos < next_line.size()) {
        auto [cp, len] = detail::utf8_decode(next_line, pos);
        int cw = detail::codepoint_display_width(cp);
        if (w + static_cast<std::size_t>(std::max(cw, 0)) > col)
          break;
        w += static_cast<std::size_t>(std::max(cw, 0));
        pos += len;
      }
      cursor_ = next_start + pos;
    } else {
      // Last line — navigate history
      if (hindex_ > 0) {
        --hindex_;
        buffer_ = (hindex_ == 0) ? saved_buffer_ : history_[hindex_ - 1];
      }
      cursor_ = buffer_.size();
    }
    break;
  }
  case key::home:
  case key::ctrl_a:
    cursor_ = line_start(current_line());
    break;
  case key::end:
  case key::ctrl_e: {
    std::size_t cur_ln = current_line();
    cursor_ = line_start(cur_ln) + line_length(cur_ln);
    break;
  }
  case key::ctrl_k: {
    // Kill to end of current line (not end of buffer)
    std::size_t cur_ln = current_line();
    std::size_t line_end = line_start(cur_ln) + line_length(cur_ln);
    if (cursor_ == line_end && cursor_ < buffer_.size()) {
      // At end of line, delete the newline to join lines
      buffer_.erase(cursor_, 1);
    } else {
      buffer_.erase(cursor_, line_end - cursor_);
    }
    break;
  }
  case key::ctrl_u: {
    // Kill to start of current line
    std::size_t ls = line_start(current_line());
    buffer_.erase(ls, cursor_ - ls);
    cursor_ = ls;
    break;
  }
  case key::ctrl_w:
    // Skip spaces backward (byte-safe, space is always 0x20)
    while (cursor_ > 0 && buffer_[cursor_ - 1] == ' ') {
      buffer_.erase(--cursor_, 1);
    }
    // Skip non-space graphemes backward (stop at newline)
    while (cursor_ > 0 && buffer_[cursor_ - 1] != ' ' && buffer_[cursor_ - 1] != '\n') {
      auto prev = detail::utf8_prev_grapheme(buffer_, cursor_);
      buffer_.erase(prev, cursor_ - prev);
      cursor_ = prev;
    }
    break;
  case key::tab:
    handle_tab();
    break;
  case key::ctrl_l:
    std::print("\x1b[2J\x1b[H");
    break;
  default:
    break;
  }

  refresh();
}

// --- Choice menu ---

void LineEditor::refresh_menu(std::span<const std::string> choices, std::size_t selected,
                              std::size_t scroll_offset, std::size_t& menu_rows) {
  // Move to start of menu display
  if (menu_rows > 0) {
    std::print("\x1b[{}A", menu_rows);
  }
  std::print("\r\x1b[J"); // clear to end of screen

  std::size_t term_height = detail::get_terminal_height();
  std::size_t max_visible = std::min(choices.size(), term_height - 2);
  std::size_t end = std::min(scroll_offset + max_visible, choices.size());

  // Print prompt line
  std::print("{}\r\n", prompt_.text);
  std::size_t rows = 1;

  // Scroll-up indicator
  if (scroll_offset > 0) {
    std::print("  \xe2\x86\x91 {} more\r\n", scroll_offset); // ↑
    ++rows;
  }

  // Print visible choices
  for (std::size_t i = scroll_offset; i < end; ++i) {
    if (i == selected) {
      std::print("  \xe2\x96\xb8 \x1b[7m{}\x1b[0m\r\n", choices[i]); // ▸ + reverse video
    } else {
      std::print("    {}\r\n", choices[i]);
    }
    ++rows;
  }

  // Scroll-down indicator
  if (end < choices.size()) {
    std::print("  \xe2\x86\x93 {} more\r\n", choices.size() - end); // ↓
    ++rows;
  }

  menu_rows = rows;
  std::fflush(stdout);
}

std::optional<ChoiceResult> LineEditor::choose(std::span<const std::string> choices) {
  if (choices.empty())
    return std::nullopt;

  std::size_t selected = 0;
  std::size_t scroll_offset = 0;
  std::size_t menu_rows = 0;

  std::size_t term_height = detail::get_terminal_height();
  std::size_t max_visible = std::min(choices.size(), term_height - 2);

  auto adjust_scroll = [&]() {
    if (selected < scroll_offset) {
      scroll_offset = selected;
    } else if (selected >= scroll_offset + max_visible) {
      scroll_offset = selected - max_visible + 1;
    }
  };

  refresh_menu(choices, selected, scroll_offset, menu_rows);

  for (;;) {
    auto evt = detail::read_key();

    switch (evt.k) {
    case key::up:
    case key::ctrl_p:
      if (selected > 0)
        --selected;
      adjust_scroll();
      break;
    case key::down:
    case key::ctrl_n:
      if (selected + 1 < choices.size())
        ++selected;
      adjust_scroll();
      break;
    case key::home:
    case key::ctrl_a:
      selected = 0;
      scroll_offset = 0;
      break;
    case key::end:
    case key::ctrl_e:
      selected = choices.size() - 1;
      adjust_scroll();
      break;
    case key::enter: {
      // Collapse menu: move to start, clear, show selection inline
      if (menu_rows > 0) {
        std::print("\x1b[{}A", menu_rows);
      }
      std::print("\r\x1b[J");
      std::print("{}{}\r\n", prompt_.text, choices[selected]);
      std::fflush(stdout);
      return ChoiceResult{.index = selected, .value = std::string(choices[selected])};
    }
    case key::ctrl_d:
    case key::escape: {
      // Cancel: clear menu
      if (menu_rows > 0) {
        std::print("\x1b[{}A", menu_rows);
      }
      std::print("\r\x1b[J");
      std::fflush(stdout);
      return std::nullopt;
    }
    default:
      continue; // ignore other keys, skip redraw
    }

    refresh_menu(choices, selected, scroll_offset, menu_rows);
  }
}

} // namespace ptty
