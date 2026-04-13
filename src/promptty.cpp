#include "promptty/promptty.hpp"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <poll.h>
#include <print>
#include <sys/ioctl.h>
#include <unistd.h>
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
#include "widechar_width.h"
#pragma GCC diagnostic pop
#include <utf8proc.h>

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

/// Returns the column width of \p cp, overriding utf8proc for emoji codepoints
/// that modern terminals render at width 2 even though their formal Unicode
/// East_Asian_Width is Neutral. Source: emoji-data.txt's Emoji_Presentation=Yes
/// property, grouped into broad ranges. Keep this list in sync with mdtty.
/// True if \p cp is an Emoji=Yes / Emoji_Presentation=No codepoint that
/// terminals nevertheless render at width 2. Unicode classifies these as
// "default text presentation," but in practice modern terminals (kitty,
/// wezterm, ghostty, foot, GNOME Terminal, iTerm2) ignore that and render
/// them as emoji. This list is the canonical set; keep it in sync with
/// mdtty's copy.
static bool is_text_presentation_wide_emoji(uint32_t cp) {
  // Sorted by codepoint for readability.
  switch (cp) {
  case 0x203C: case 0x2049: case 0x2122: case 0x2139:
  case 0x2194: case 0x2195: case 0x2196: case 0x2197: case 0x2198: case 0x2199:
  case 0x21A9: case 0x21AA:
  case 0x2328: case 0x23CF:
  case 0x24C2:
  case 0x25AA: case 0x25AB: case 0x25B6: case 0x25C0:
  case 0x25FB: case 0x25FC:
  case 0x2600: case 0x2601: case 0x2602: case 0x2603: case 0x2604:
  case 0x260E: case 0x2611:
  case 0x2618: case 0x261D: case 0x2620:
  case 0x2622: case 0x2623: case 0x2626: case 0x262A:
  case 0x262E: case 0x262F:
  case 0x2638: case 0x2639: case 0x263A:
  case 0x2640: case 0x2642:
  case 0x265F: case 0x2660: case 0x2663: case 0x2665: case 0x2666: case 0x2668:
  case 0x267B: case 0x267E:
  case 0x2692: case 0x2694: case 0x2695: case 0x2696: case 0x2697:
  case 0x2699: case 0x269B: case 0x269C:
  case 0x26A0: case 0x26A7:
  case 0x26B0: case 0x26B1:
  case 0x26C8: case 0x26CF: case 0x26D1:
  case 0x26D3: case 0x26E9:
  case 0x26F0: case 0x26F1: case 0x26F4: case 0x26F7: case 0x26F8: case 0x26F9:
  case 0x2702: case 0x2708: case 0x2709:
  case 0x270C: case 0x270D: case 0x270F: case 0x2712:
  case 0x2714: case 0x2716: case 0x271D: case 0x2721:
  case 0x2733: case 0x2734: case 0x2744: case 0x2747:
  case 0x2763: case 0x2764: case 0x27A1:
  case 0x2934: case 0x2935:
  case 0x2B05: case 0x2B06: case 0x2B07:
  case 0x3030: case 0x303D: case 0x3297: case 0x3299:
    return true;
  default:
    return false;
  }
}

/// Returns the column width of \p cp using the widecharwidth table, which is
/// generated from the upstream Unicode data files (UnicodeData.txt,
/// EastAsianWidth.txt, emoji-data.txt). This is the same source modern
/// terminals (kitty, wezterm, ghostty, foot, GNOME Terminal, iTerm2) use.
static int terminal_charwidth(utf8proc_int32_t cp) {
  if (cp < 0)
    return 1;
  // Default-text emoji that terminals render wide anyway. Checked first
  // because widecharwidth would otherwise report these as width 1.
  if (is_text_presentation_wide_emoji(static_cast<uint32_t>(cp)))
    return 2;
  int w = widechar_wcwidth(static_cast<uint32_t>(cp));
  if (w == widechar_combining)
    return 0;
  // widened_in_9: Unicode 9 promoted these to wide because of emoji
  // presentation. Modern terminals render them at width 2.
  if (w == widechar_widened_in_9)
    return 2;
  // Other negative codes (nonprint/ambiguous/private/unassigned) fall back
  // to width 1: safe default for "printable but we don't know."
  if (w < 0)
    return 1;
  return w;
}

int codepoint_display_width(char32_t cp) {
  if (cp == 0)
    return 0;
  int w = terminal_charwidth(static_cast<utf8proc_int32_t>(cp));
  return (w < 0) ? 0 : w;
}

/// Returns the byte offset just past the grapheme cluster starting at \p pos.
std::size_t utf8_next_grapheme(std::string_view s, std::size_t pos) {
  if (pos >= s.size())
    return s.size();
  auto [cp, len] = utf8_decode(s, pos);
  pos += len;
  utf8proc_int32_t state = 0;
  utf8proc_int32_t prev_cp = static_cast<utf8proc_int32_t>(cp);
  while (pos < s.size()) {
    auto [next_cp, next_len] = utf8_decode(s, pos);
    if (utf8proc_grapheme_break_stateful(prev_cp, static_cast<utf8proc_int32_t>(next_cp), &state))
      break;
    prev_cp = static_cast<utf8proc_int32_t>(next_cp);
    pos += next_len;
  }
  return pos;
}

/// Returns the byte offset of the start of the grapheme cluster ending at \p pos.
/// Strategy: step back to a "safe" boundary that's never inside a cluster, then
/// walk forward computing cluster boundaries until the last one before \p pos.
std::size_t utf8_prev_grapheme(std::string_view s, std::size_t pos) {
  if (pos == 0)
    return 0;
  if (pos > s.size())
    pos = s.size();

  // Walk back at least one codepoint, then keep walking back while we're
  // crossing combining marks / extending characters. A safe-enough heuristic:
  // any ASCII byte (< 0x80) is always its own grapheme start, so it's a safe
  // boundary. If we don't find one within ~32 bytes we just take what we have.
  std::size_t safe = pos;
  for (int i = 0; i < 32 && safe > 0; ++i) {
    --safe;
    while (safe > 0 && (static_cast<unsigned char>(s[safe]) & 0xC0) == 0x80)
      --safe;
    if (static_cast<unsigned char>(s[safe]) < 0x80)
      break;
  }

  // Walk forward from safe, tracking the most recent cluster boundary <= pos.
  std::size_t cur = safe;
  std::size_t last_boundary = safe;
  utf8proc_int32_t state = 0;
  utf8proc_int32_t prev_cp = 0;
  while (cur < pos) {
    auto [cp, len] = utf8_decode(s, cur);
    if (len == 0)
      break;
    if (prev_cp != 0 &&
        utf8proc_grapheme_break_stateful(prev_cp, static_cast<utf8proc_int32_t>(cp), &state)) {
      last_boundary = cur;
    }
    prev_cp = static_cast<utf8proc_int32_t>(cp);
    cur += len;
  }
  return last_boundary;
}

std::size_t display_width(std::string_view s) {
  std::size_t width = 0;
  std::size_t pos = 0;
  utf8proc_int32_t state = 0;
  utf8proc_int32_t prev_cp = 0;
  while (pos < s.size()) {
    auto [cp, len] = utf8_decode(s, pos);
    if (len == 0)
      break;
    bool boundary =
        (prev_cp == 0) ||
        utf8proc_grapheme_break_stateful(prev_cp, static_cast<utf8proc_int32_t>(cp), &state);
    if (boundary) {
      int w = codepoint_display_width(cp);
      if (w > 0)
        width += static_cast<std::size_t>(w);
    }
    prev_cp = static_cast<utf8proc_int32_t>(cp);
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
  case '\x07':
    return make_key(key::ctrl_g);
  case '\x0b':
    return make_key(key::ctrl_k);
  case '\x0c':
    return make_key(key::ctrl_l);
  case '\x0e':
    return make_key(key::ctrl_n);
  case '\x0f':
    return make_key(key::ctrl_o);
  case '\x10':
    return make_key(key::ctrl_p);
  case '\x12':
    return make_key(key::ctrl_r);
  case '\x14':
    return make_key(key::ctrl_t);
  case '\x15':
    return make_key(key::ctrl_u);
  case '\x17':
    return make_key(key::ctrl_w);
  case '\x18':
    return make_key(key::ctrl_x);
  case '\x19':
    return make_key(key::ctrl_y);
  default:
    break;
  }

  if (chr == '\x1b') {
    // Distinguish bare ESC from escape sequences via 50ms timeout.
    struct pollfd pfd = {.fd = STDIN_FILENO, .events = POLLIN, .revents = 0};
    if (poll(&pfd, 1, 50) <= 0) {
      return make_key(key::escape);
    }

    char seq0{};
    if (::read(STDIN_FILENO, &seq0, 1) <= 0) {
      return make_key(key::escape);
    }

    if (seq0 == '\n') { // Alt+Enter
      return make_key(key::alt_enter);
    }

    if (seq0 == 'O') { // SS3: F1..F4
      char seq1{};
      if (::read(STDIN_FILENO, &seq1, 1) <= 0)
        return make_key(key::unknown);
      switch (seq1) {
      case 'P': return make_key(key::f1);
      case 'Q': return make_key(key::f2);
      case 'R': return make_key(key::f3);
      case 'S': return make_key(key::f4);
      default:  return make_key(key::unknown);
      }
    }

    if (seq0 == '[') { // CSI sequence
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
      default:
        break;
      }
      // Numeric CSI: ESC [ N (N) ~  -- PageUp/Down, Delete, F5-F12.
      if (seq1 >= '0' && seq1 <= '9') {
        std::string num(1, seq1);
        for (int i = 0; i < 3; ++i) {
          char c{};
          if (::read(STDIN_FILENO, &c, 1) <= 0)
            return make_key(key::unknown);
          if (c == '~')
            break;
          if (c < '0' || c > '9')
            return make_key(key::unknown);
          num += c;
        }
        if (num == "3")  return make_key(key::del);
        if (num == "5")  return make_key(key::page_up);
        if (num == "6")  return make_key(key::page_down);
        if (num == "15") return make_key(key::f5);
        if (num == "17") return make_key(key::f6);
        if (num == "18") return make_key(key::f7);
        if (num == "19") return make_key(key::f8);
        if (num == "20") return make_key(key::f9);
        if (num == "21") return make_key(key::f10);
        if (num == "23") return make_key(key::f11);
        if (num == "24") return make_key(key::f12);
      }
      return make_key(key::unknown);
    }
    return make_key(key::unknown);
  }

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

  if (b >= 0x20 && b < 0x7F) { // ASCII printable
    return {.k = key::character, .ch = std::string(1, chr)};
  }

  return make_key(key::unknown);
}
} // namespace detail

// --- LineEditor ---

static std::optional<bindable_key> to_bindable(key k);

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
      prev_sigint_{other.prev_sigint_}, history_file_{std::move(other.history_file_)},
      completion_cb_{std::move(other.completion_cb_)}, completion_{std::move(other.completion_)},
      bindings_{std::move(other.bindings_)} {
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
    completion_cb_ = std::move(other.completion_cb_);
    completion_ = std::move(other.completion_);
    bindings_ = std::move(other.bindings_);

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
      // Backslash continuation.
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

    // Dispatch user-bound keys before built-in handling.
    if (auto bk = to_bindable(evt.k)) {
      auto it = bindings_.find(*bk);
      if (it != bindings_.end()) {
        it->second();
        refresh();
        continue;
      }
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
      // \x1f encodes newlines within multiline entries.
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
      for (char c : entry) {
        file.put((c == '\n') ? '\x1f' : c);
      }
      file.put('\n');
    }
  }
}

// --- Refresh ---

void LineEditor::refresh() {
  if (display_rows_ > 1) {
    std::print("\x1b[{}A", display_rows_ - 1);
  }
  std::print("\r\x1b[J");

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

    if (ln > 0)
      std::print("\r\n");
    std::print("{}{}", p.text, line_sv);

    std::size_t line_display_width = p.visible_width + detail::display_width(line_sv);
    std::size_t line_rows =
        (line_display_width == 0) ? 1 : ((line_display_width - 1) / term_width + 1);
    rows += line_rows;

    if (cursor_ >= ls && cursor_ <= ls + ll) {
      std::size_t col_in_line =
          detail::display_width(std::string_view(buffer_).substr(ls, cursor_ - ls));
      std::size_t abs_col = p.visible_width + col_in_line;
      std::size_t wrap_row = abs_col / term_width;
      cursor_row = (rows - line_rows) + wrap_row;
      cursor_col = abs_col % term_width;
    }
  }

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

// --- Completion ---

void LineEditor::set_completion(CompletionCallback cb) {
  completion_cb_ = std::move(cb);
}

void LineEditor::bind_key(bindable_key k, KeyCallback cb) {
  if (cb)
    bindings_[k] = std::move(cb);
  else
    bindings_.erase(k);
}

void LineEditor::set_prompt(Prompt prompt) {
  prompt_ = std::move(prompt);
  continuation_ = Prompt(std::string(prompt_.visible_width, ' '));
}

/// Maps a \p key to its bindable counterpart, or returns nullopt if the key
/// is reserved by the editor for navigation/editing.
static std::optional<bindable_key> to_bindable(key k) {
  switch (k) {
  case key::ctrl_g: return bindable_key::ctrl_g;
  case key::ctrl_o: return bindable_key::ctrl_o;
  case key::ctrl_r: return bindable_key::ctrl_r;
  case key::ctrl_t: return bindable_key::ctrl_t;
  case key::ctrl_x: return bindable_key::ctrl_x;
  case key::ctrl_y: return bindable_key::ctrl_y;
  case key::page_up: return bindable_key::page_up;
  case key::page_down: return bindable_key::page_down;
  case key::f1: return bindable_key::f1;
  case key::f2: return bindable_key::f2;
  case key::f3: return bindable_key::f3;
  case key::f4: return bindable_key::f4;
  case key::f5: return bindable_key::f5;
  case key::f6: return bindable_key::f6;
  case key::f7: return bindable_key::f7;
  case key::f8: return bindable_key::f8;
  case key::f9: return bindable_key::f9;
  case key::f10: return bindable_key::f10;
  case key::f11: return bindable_key::f11;
  case key::f12: return bindable_key::f12;
  default: return std::nullopt;
  }
}

void LineEditor::handle_tab() {
  if (!completion_cb_)
    return;

  if (!completion_) {
    auto result = completion_cb_({.buffer = buffer_, .cursor = cursor_});
    if (result.candidates.empty()) {
      std::print("\x07"); // bell
      std::fflush(stdout);
      return;
    }
    if (result.candidates.size() == 1) {
      buffer_.erase(result.replace_start, result.replace_length);
      buffer_.insert(result.replace_start, result.candidates[0]);
      cursor_ = result.replace_start + result.candidates[0].size();
      return;
    }
    std::string original = buffer_.substr(result.replace_start, result.replace_length);
    completion_ = CompletionState{
        .replace_start = result.replace_start,
        .replace_length = result.replace_length,
        .candidates = std::move(result.candidates),
        .index = 0,
        .original = std::move(original),
    };
    buffer_.erase(completion_->replace_start, completion_->replace_length);
    buffer_.insert(completion_->replace_start, completion_->candidates[0]);
    cursor_ = completion_->replace_start + completion_->candidates[0].size();
    completion_->replace_length = completion_->candidates[0].size();
  } else {
    completion_->index = (completion_->index + 1) % completion_->candidates.size();
    auto& candidate = completion_->candidates[completion_->index];
    buffer_.erase(completion_->replace_start, completion_->replace_length);
    buffer_.insert(completion_->replace_start, candidate);
    cursor_ = completion_->replace_start + candidate.size();
    completion_->replace_length = candidate.size();
  }
}

// --- Key handling ---

void LineEditor::handle(KeyEvent evt) {
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
      std::size_t col = column_width();
      std::size_t prev_start = line_start(cur_ln - 1);
      std::size_t prev_len = line_length(cur_ln - 1);
      auto prev_line = std::string_view(buffer_).substr(prev_start, prev_len);
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
    std::size_t cur_ln = current_line();
    std::size_t line_end = line_start(cur_ln) + line_length(cur_ln);
    if (cursor_ == line_end && cursor_ < buffer_.size()) {
      buffer_.erase(cursor_, 1); // join lines
    } else {
      buffer_.erase(cursor_, line_end - cursor_);
    }
    break;
  }
  case key::ctrl_u: {
    std::size_t ls = line_start(current_line());
    buffer_.erase(ls, cursor_ - ls);
    cursor_ = ls;
    break;
  }
  case key::ctrl_w:
    while (cursor_ > 0 && buffer_[cursor_ - 1] == ' ') {
      buffer_.erase(--cursor_, 1);
    }
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
                              std::size_t scroll_offset, std::size_t& menu_rows,
                              std::string_view header) {
  if (menu_rows > 0) {
    std::print("\x1b[{}A", menu_rows);
  }
  std::print("\r\x1b[J");

  std::size_t term_height = detail::get_terminal_height();
  std::size_t max_visible = std::min(choices.size(), term_height - 2);
  std::size_t end = std::min(scroll_offset + max_visible, choices.size());

  std::print("{}{}\r\n", prompt_.text, header);
  std::size_t rows = 1;

  if (scroll_offset > 0) {
    std::print("  \xe2\x86\x91 {} more\r\n", scroll_offset);
    ++rows;
  }

  for (std::size_t i = scroll_offset; i < end; ++i) {
    if (i == selected) {
      std::print("  \xe2\x96\xb8 \x1b[7m{}\x1b[0m\r\n", choices[i]);
    } else {
      std::print("    {}\r\n", choices[i]);
    }
    ++rows;
  }

  if (end < choices.size()) {
    std::print("  \xe2\x86\x93 {} more\r\n", choices.size() - end);
    ++rows;
  }

  menu_rows = rows;
  std::fflush(stdout);
}

std::optional<ChoiceResult> LineEditor::choose(std::span<const std::string> choices,
                                               std::string_view header) {
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

  refresh_menu(choices, selected, scroll_offset, menu_rows, header);

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
      if (menu_rows > 0) {
        std::print("\x1b[{}A", menu_rows);
      }
      std::print("\r\x1b[J");
      std::print("{} {}: {}\r\n", prompt_.text, header, choices[selected]);
      std::fflush(stdout);
      return ChoiceResult{.index = selected, .value = std::string(choices[selected])};
    }
    case key::ctrl_d:
    case key::escape: {
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

    refresh_menu(choices, selected, scroll_offset, menu_rows, header);
  }
}

} // namespace ptty
