#include "array.h"
#include "stdio.h"
#include "stdarg.h"
#include "assert.h"
#include "termios.h"
#include "signal.h"
#include "unistd.h"
#include "stdbool.h"
#include "fcntl.h"
#include "sys/stat.h"

//--------------------------------------------------------------------------------------------------

#define min(x, y) (((int)(x) < (int)(y)) ? x : y)
#define max(x, y) (((int)(x) < (int)(y)) ? y : x)

//--------------------------------------------------------------------------------------------------

typedef struct termios Termios;
typedef struct Line Line;
typedef struct File File;
typedef struct Window Window;

//--------------------------------------------------------------------------------------------------

define_array(char_array, CharArray, char);
define_array(line_array, LineArray, Line* );
define_array(file_array, FileArray, File* );
define_array(window_array, WindowArray, Window* );

//--------------------------------------------------------------------------------------------------

struct Line {
  CharArray chars;

  bool dirty;
};

//--------------------------------------------------------------------------------------------------

struct File {
  CharArray path;
  LineArray lines;

  bool unsaved;
  bool dirty;
};

//--------------------------------------------------------------------------------------------------

struct Window {
  File* file;

  int position_x;
  int position_y;

  int width;
  int height;

  int offset_x;
  int offset_y;

  int cursor_x;
  int cursor_y;

  int ideal_cursor_x;
};

//--------------------------------------------------------------------------------------------------

static Termios saved_terminal;
static CharArray framebuffer;
static WindowArray windows;
static FileArray files;

//--------------------------------------------------------------------------------------------------

// Forward declarations.

static void debug_print(const char* data, ...);

static int print(const char* data, ...);
static void flush();
static void get_cursor(int* x, int* y);
static void set_cursor(int x, int y);
static void get_terminal_size(int* width, int* height);
static void hide_cursor();
static void show_cursor();
static void clear_line(int y);
static void clear_terminal();

static File* new_file(char* path, int path_size);
static void delete_file(File* file);
static File* open_file(char* path, int path_size);
static File* create_file(char* path, int path_size);
static bool save_file(File* file);

static Line* append_line(File* file);
static void delete_line(Line* line);

static void append_chars(Line* line, char* data, int size);

static void window_resize_handler(int signal);
static void terminal_deinit();
static void terminal_init();
static void editor_init();

//--------------------------------------------------------------------------------------------------

static void debug_print(const char* data, ...) {
  static char buffer[1024];

  va_list arguments;
  va_start(arguments, data);
  int size = vsnprintf(buffer, 1024, data, arguments);
  va_end(arguments);

  // Run tty in a remote terminal to get the path.
  FILE* fd = fopen("/dev/pts/1", "w");
  assert(fd > 0);

  fwrite(buffer, 1, size, fd);
  fclose(fd);
}

//--------------------------------------------------------------------------------------------------

static int print(const char* data, ...) {
  char_array_extend(&framebuffer, framebuffer.count + 1024);

  va_list arguments;
  va_start(arguments, data);
  int size = vsnprintf(&framebuffer.items[framebuffer.count], 1024, data, arguments);
  va_end(arguments);

  framebuffer.count += size;
  return size;
}

//--------------------------------------------------------------------------------------------------

static void flush() {
  write(STDOUT_FILENO, framebuffer.items, framebuffer.count);
  framebuffer.count = 0;
}

//--------------------------------------------------------------------------------------------------

static void get_cursor(int* x, int* y) {
  char data[32];
  int size = 0;

  print("\x1b[6n");
  flush();

  while (size < sizeof(data)) {
    read(STDIN_FILENO, &data[size], 1);
    if (data[size] == 'R') break;
    size++;
  }

  data[size] = 0;

  assert(data[0] == '\x1b' && data[1] == '[');
  assert(sscanf(&data[2], "%d;%d", y, x) == 2);
}

//--------------------------------------------------------------------------------------------------

static void set_cursor(int x, int y) {
  print("\x1b[%d;%dH", y + 1, x + 1);
}

//--------------------------------------------------------------------------------------------------

static void get_terminal_size(int* width, int* height) {
  int x, y;
  get_cursor(&x, &y);

  set_cursor(500, 500);
  flush();

  get_cursor(width, height);

  set_cursor(x, y);
  flush();
}

//--------------------------------------------------------------------------------------------------

static void hide_cursor() {
  print("\x1b[?25l");
}

//--------------------------------------------------------------------------------------------------

static void show_cursor() {
  print("\x1b[?25h");
}

//--------------------------------------------------------------------------------------------------

static void clear_line(int y) {
  set_cursor(0, y);
  print("\x1b[2K");
}

//--------------------------------------------------------------------------------------------------

static void clear_terminal() {
  print("\x1b[2J");
}

//--------------------------------------------------------------------------------------------------

static File* new_file(char* path, int path_size) {
  File* file = calloc(1, sizeof(File));
  file_array_append(&files, file);

  char_array_append_multiple(&file->path, path, path_size);
  file->dirty = true;

  return file;
}

//--------------------------------------------------------------------------------------------------

static void delete_file(File* file) {
  for (int i = 0; i < file->lines.count; i++) {
    delete_line(file->lines.items[i]);
  }

  line_array_delete(&file->lines);
}

//--------------------------------------------------------------------------------------------------

static File* open_file(char* path, int path_size) {
  assert(path_size < 64);
  char open_path[64];

  memcpy(open_path, path, path_size);
  open_path[path_size] = 0;

  int fd = open(open_path, O_RDONLY);
  if (fd < 0) return 0;

  struct stat info;
  fstat(fd, &info);

  int size = info.st_size;
  char* data = malloc(info.st_size);

  if (read(fd, data, size) != size) return 0;

  int cr = 0;
  int line_count = 0;

  for (int i = 0; i < size; i++) {
    if (data[i] == '\n') {
      line_count++;
      cr = 0;
    }
    else if (data[i] == '\r') {
      cr = 1;
    }
    else if (cr) {
      return 0;
    }
  }

  File* file = new_file(path, path_size);
  line_array_extend(&file->lines, line_count);

  cr = 0;
  int index = 0;

  for (int i = 0; i < size; i++) {
    if (data[i] == '\n') {
      Line* line = append_line(file);
      append_chars(line, &data[index], i - index - cr);
      index = i + 1;
      cr = 0;
    }
    else if (data[i] == '\r') {
      cr = 1;
    }
  }

  return file;
}

//--------------------------------------------------------------------------------------------------

static File* create_file(char* path, int path_size) {
  File* file = new_file(path, path_size);
  append_line(file);
  return file;
}

//--------------------------------------------------------------------------------------------------

static bool save_file(File* file) {
  // Todo.
  return true;
}

//--------------------------------------------------------------------------------------------------

static Line* append_line(File* file) {
  Line* line = calloc(1, sizeof(Line));
  line_array_append(&file->lines, line);
  file->dirty = true;
  return line;
}

//--------------------------------------------------------------------------------------------------

static void delete_line(Line* line) {
  char_array_delete(&line->chars);
  free(line);
}

//--------------------------------------------------------------------------------------------------

static void append_chars(Line* line, char* data, int size) {
  char_array_append_multiple(&line->chars, data, size);
  line->dirty = true;
}

//--------------------------------------------------------------------------------------------------

static void window_resize_handler(int signal) {
  debug_print("Window resize\r\n");
}

//--------------------------------------------------------------------------------------------------

static void terminal_deinit() {
  tcsetattr(STDIN_FILENO, TCSAFLUSH, &saved_terminal);
}

//--------------------------------------------------------------------------------------------------

static void terminal_init() {
  tcgetattr(STDIN_FILENO, &saved_terminal);
  atexit(terminal_deinit);

  Termios terminal = saved_terminal;

  terminal.c_iflag &= ~(BRKINT | INPCK | ISTRIP | IXON);
  terminal.c_lflag &= ~(ICANON | ECHO | ISIG);
  terminal.c_oflag &= ~(OPOST);
  terminal.c_cc[VMIN] = 0;
  terminal.c_cc[VTIME] = 1;

  tcsetattr(STDIN_FILENO, TCSAFLUSH, &terminal);

  signal(SIGWINCH, window_resize_handler);
}

//--------------------------------------------------------------------------------------------------

static void editor_init() {
  char_array_init(&framebuffer, 64 * 1024);
  window_array_init(&windows, 32);
  file_array_init(&files, 32);
}

//--------------------------------------------------------------------------------------------------

int main() {
  terminal_init();
  editor_init();

  File* file = open_file("array.h", sizeof("array.h") - 1);
  if (!file) {
    debug_print("Cant open file\n");
    return 0;
  }

  for (int i = 0; i < file->lines.count; i++) {
    Line* line = file->lines.items[i];

    print("L: %.*s\r\n", line->chars.count, line->chars.items);
  }
  flush();

  int width, height;
  get_terminal_size(&width, &height);

  debug_print("width = %d height = %d\n", width, height);

  return 0;
}
