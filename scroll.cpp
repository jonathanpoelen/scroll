/*
    SPDX-FileCopyrightText: 2022 Jonathan poelen <jonathan.poelen@gmail.com>

    SPDX-License-Identifier: MIT
*/

#include <string_view>
#include <charconv>
#include <utility>

#include <cstdio>
#include <cerrno>
#include <cstdlib>
#include <cstring>

#include <sys/ioctl.h>
#include <termios.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>


static winsize get_window_size() noexcept
{
  winsize w {};
  ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
  return w;
}

static ssize_t write(int fd, std::string_view buffer) noexcept
{
  return ::write(fd, buffer.data(), buffer.size());
}

template<std::size_t N>
ssize_t read(int fd, char (&buf)[N]) noexcept
{
  return ::read(fd, buf, N);
}

static int icanon_mode(int fd, termios term) noexcept
{
  term.c_cc[VMIN] = 1;
  term.c_cc[VTIME] = 0;
  term.c_lflag &= ~unsigned{ECHO | ICANON};
  return ioctl(fd, TCSETS, &term);
}

#define VERIFY_R(expr, ret) do {  \
    if (auto r = (expr); r == -1) \
    {                             \
      return ret;                 \
    }                             \
  } while (0)

#define VERIFY(expr) VERIFY_R(expr, -1)

#define PP_CONCAT(a, b) PP_CONCAT_I(a, b)
#define PP_CONCAT_I(a, b) a ## b

namespace
{
  template<class Fn>
  struct Scoped
  {
    Fn fn;
    bool enabled = true;

    ~Scoped()
    {
      fn();
    }
  };

  #define SCOPED(...)                                 \
    Scoped PP_CONCAT(PP_CONCAT(scoped_, __LINE__), _) \
    {[&]() noexcept __VA_ARGS__ }

  template<class Fn>
  Scoped(Fn) -> Scoped<Fn>;
}

static unsigned get_current_line() noexcept
{
  int fd = ::open("/dev/tty", O_RDWR);
  VERIFY(fd);
  SCOPED({ ::close(fd); });

  termios term;
  VERIFY(ioctl(fd, TCGETS, &term));
  VERIFY(icanon_mode(fd, term));

  SCOPED({ ioctl(fd, TCSETS, &term); });

  // cursor position request
  VERIFY(write(fd, "\x1b[6n"));

  // response format: \e[${line};${column}R
  char buf[64];
  ssize_t n = read(fd, buf, std::size(buf) - 1);
  VERIFY(n);

  if (n >= 6 && buf[0] == '\x1b' && buf[1] == '[') {
    unsigned line;
    auto r = std::from_chars(buf+2, buf+n, line);
    if (!bool(r.ec) && *r.ptr == ';') {
      do {
        ++r.ptr;
      } while (*r.ptr <= '9' && '0' <= *r.ptr);

      if (*r.ptr == 'R' && r.ptr == buf + n - 1) {
        return line;
      }
    }
  }

  errno = EPROTO;
  return 0;
}

static ssize_t insert_new_line(unsigned n) noexcept
{
  constexpr std::string_view newlines
    = "\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n"
      "\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n"
  ;
  while (n > newlines.size()) {
    VERIFY(write(1, newlines));
    n -= newlines.size();
  }
  return write(1, newlines.data(), size_t(n));
}

static char* memcpy(char* dst, std::string_view src) noexcept
{
  memcpy(dst, src.data(), src.size());
  return dst + src.size();
}

static ssize_t set_margin(unsigned current_line, unsigned height) noexcept
{
  // set top and bottom margins (scroll) ; cursor position ; save cursor
  // "\e[${line};${line+height}r\e[${line};1H\e7"
  char buf[100];
  char* it = buf;

  it = memcpy(it, "\x1b[");
  it = std::to_chars(it, it+16, current_line).ptr;
  *it++ = ';';
  it = std::to_chars(it, it+16, current_line + height).ptr;
  it = memcpy(it, "r\x1b[");
  it = std::to_chars(it, it+16, current_line).ptr;
  it = memcpy(it, ";1H\x1b""7");

  return write(1, buf, std::size_t(it - buf));
}

static ssize_t reset_margin(unsigned nb_line) noexcept
{
  // save cursor position ; reset margins ; restore cursor position
  // "\e[s\e[1;${LINES}r\e[u"
  char buf[64];
  char* it = buf;

  it = memcpy(it, "\x1b[s\x1b[1;");
  it = std::to_chars(it, std::end(buf), nb_line).ptr;
  it = memcpy(it, "r\x1b[u");

  return write(1, buf, std::size_t(it - buf));
}

static int prepare_scroll(unsigned nb_line, unsigned current_line, unsigned height) noexcept
{
  // insufficient number of lines available
  if (nb_line < current_line + height) {
    VERIFY_R(insert_new_line(height), 0);
    return int(nb_line) - int(height);
  }

  return int(current_line);
}

namespace
{
  struct WinInfo
  {
    unsigned nb_line;
    unsigned height;
  };
  WinInfo g_info {};
}

static void sigint_action(int /*signum*/) noexcept
{
  reset_margin(g_info.nb_line);
}

static void sigwinch_action(int /*signum*/) noexcept
{
  reset_margin(g_info.nb_line);

  // restore cursor
  write(1, "\x1b""8");

  auto ws = get_window_size();
  unsigned current_line = get_current_line();

  if (current_line <= 0 || ws.ws_row == 0) {
    return ;
  }

  auto height = g_info.height;

  int new_current_line = prepare_scroll(ws.ws_row, current_line, height);
  if (new_current_line >= 0) {
    set_margin(unsigned(new_current_line), height);
  }

  g_info.nb_line = ws.ws_row;
}

static int attach_sigs(unsigned nb_line, unsigned height) noexcept
{
  g_info.nb_line = nb_line;
  g_info.height = height;

  struct sigaction act {};
  act.sa_flags = SA_RESTART;

  act.sa_handler = sigint_action;
  VERIFY(sigaction(SIGINT, &act, nullptr));

  act.sa_handler = sigwinch_action;
  return sigaction(SIGWINCH, &act, nullptr);
}

int main(int ac, char** av)
{
  unsigned height = 0;

  if (ac == 2) {
    std::from_chars(av[1], av[1] + (av[2] - av[1]), height);
  }

  if (height == 0) {
    write(2, "missing or incorrect height parameter\nusage: scroll HEIGHT\n");
    return 1;
  }

  unsigned current_line = get_current_line();
  auto ws = get_window_size();

  if (current_line <= 0 || ws.ws_row == 0) {
    std::string_view msg = strerror(errno);
    write(2, msg.data(), msg.size());
    write(2, "\n", 1);
    return 1;
  }

  unsigned nb_line = ws.ws_row;

  int new_current_line = prepare_scroll(nb_line, current_line, height);
  VERIFY_R(new_current_line, 0);
  VERIFY_R(set_margin(unsigned(new_current_line), height), 0);
  VERIFY_R(attach_sigs(nb_line, height), 0);

  auto write_all = [](char const* buf, ssize_t n){
    ssize_t n2;
    while ((n2 = write(1, buf, size_t(n))) > 0) {
      if (n2 == n) {
        return 1;
      }
      n -= n2;
      buf += n2;
    }
    return -1;
  };

  char buf[1024 * 64];
  ssize_t n;
  while ((n = read(0, buf)) > 0 && write_all(buf, n) > 0) {
  }

  reset_margin(nb_line);

  return 0;
}
