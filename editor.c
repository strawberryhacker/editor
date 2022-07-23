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
#include "sys/time.h"
#include "sys/select.h"

//--------------------------------------------------------------------------------------------------

#define SpacesPerTab 2

#define WindowCursorMarginTop    6
#define WindowCursorMarginBottom 6
#define WindowCursorMarginLeft   6
#define WindowCursorMarginRight  6


#define LinenumberMargin 2
#define StatusBarCount   1

#define ErrorShowTime 1500

#define MinimumWindowWidth  40
#define MinimumWindowHeight 10


// Status bar display settings.
#define MaxPathWidth 20
#define StatusBarCommandPadding 1
#define StatusBarLeftPadding 1
#define StatusBarRightPadding 1
#define BarCursorMarginLeft  5
#define BarCursorMarginRight 5

//--------------------------------------------------------------------------------------------------

#define min(x, y) (((int)(x) < (int)(y)) ? x : y)
#define max(x, y) (((int)(x) < (int)(y)) ? y : x)
#define limit(x, lower, upper) max(min(x, upper), lower)

//--------------------------------------------------------------------------------------------------

typedef struct termios Termios;
typedef struct timeval Time;
typedef struct Line Line;
typedef struct File File;
typedef struct Window Window;
typedef struct Region Region;

//--------------------------------------------------------------------------------------------------

define_array(char_array, CharArray, char);
define_array(line_array, LineArray, Line* );
define_array(file_array, FileArray, File* );
define_array(window_array, WindowArray, Window* );

//--------------------------------------------------------------------------------------------------

enum {
  ColorSolarizedBase03 = 0x002b36,
  ColorSolarizedBase02 = 0x073642,
  ColorSolarizedBase01 = 0x586e75,
  ColorSolarizedBase00 = 0x657b83,
  ColorSolarizedBase0  = 0x839496,
  ColorSolarizedBase1  = 0x93a1a1,
  ColorSolarizedBase2  = 0xeee8d5,
  ColorSolarizedBase3  = 0xfdf6e3,
  ColorSolarizedRed    = 0xdc322f,
  ColorSolarizedViolet = 0x6c71c4,
};

//--------------------------------------------------------------------------------------------------

enum {
  ColorTypeEditorCursor,
  ColorTypeEditorForeground,
  ColorTypeEditorBackground,
  ColorTypeStatusBarCursor,
  ColorTypeStatusBarForeground,
  ColorTypeStatusBarBackground,
  ColorTypeStatusBarError,
  ColorTypeSelectedMatchBackground,
  ColorTypeMatchBackground,
  ColorTypeMatchForeground,
  ColorTypeCount,
};

//--------------------------------------------------------------------------------------------------

enum {
  ColorSchemeDefault,
  ColorSchemeLight,
  ColorSchemeCount,
};

//--------------------------------------------------------------------------------------------------

static const struct { char* name; int colors[ColorTypeCount]; } schemes[ColorSchemeCount] = {
  [ColorSchemeDefault] = {
    .name = "default",
    .colors = {
      [ColorTypeEditorCursor]            = ColorSolarizedBase0,
      [ColorTypeEditorForeground]        = ColorSolarizedBase0,
      [ColorTypeEditorBackground]        = ColorSolarizedBase03,
      [ColorTypeStatusBarCursor]         = ColorSolarizedBase01,
      [ColorTypeStatusBarForeground]     = ColorSolarizedBase01,
      [ColorTypeStatusBarBackground]     = ColorSolarizedBase3,
      [ColorTypeStatusBarError]          = ColorSolarizedRed,
      [ColorTypeSelectedMatchBackground] = ColorSolarizedViolet,
      [ColorTypeMatchBackground]         = ColorSolarizedViolet,
      [ColorTypeMatchForeground]         = ColorSolarizedViolet,
    },
  },

  [ColorSchemeLight] = {
    .name = "light",
    .colors = {
      [ColorTypeEditorCursor]            = ColorSolarizedBase3,
      [ColorTypeEditorForeground]        = ColorSolarizedBase3,
      [ColorTypeEditorBackground]        = ColorSolarizedBase01,
      [ColorTypeStatusBarCursor]         = ColorSolarizedBase01,
      [ColorTypeStatusBarForeground]     = ColorSolarizedBase01,
      [ColorTypeStatusBarBackground]     = ColorSolarizedBase2,
      [ColorTypeStatusBarError]          = ColorSolarizedRed,
      [ColorTypeSelectedMatchBackground] = ColorSolarizedViolet,
      [ColorTypeMatchBackground]         = ColorSolarizedViolet,
      [ColorTypeMatchForeground]         = ColorSolarizedViolet,
    },
  },
};

//--------------------------------------------------------------------------------------------------

enum {
  ColorCodeRed   = 0xff0000,
  ColorCodePink  = 0xfc03c2,
  ColorCodeGreen = 0x03a309,
};

enum {
  ColorForeground    = 0xfdf6e3,
  ColorBackground    = 0x93a1a1,
  ColorError         = 0xdc322f,
  ColorBarBackground = 0xfdf6e3,
  ColorBarForeground = 0x93a1a1,
  ColorBarCursor     = 0xfdf6e3,
  ColorNormalCursor  = 0x657b83,
};

//--------------------------------------------------------------------------------------------------

enum {
  KeyCodeTab            = 9,
  KeyCodeEnter          = 10,
  KeyCodeEscape         = 27,
  KeyCodeDelete         = 127,
  KeyCodeCtrlDelete     = 8,

  KeyCodeCtrlC          = 3,
  KeyCodeCtrlG          = 7,
  KeyCodeCtrlN          = 14,
  KeyCodeCtrlQ          = 17,
  KeyCodeCtrlS          = 19,
  KeyCodeCtrlX          = 24,
  KeyCodeCtrlV          = 22,
  KeyCodeCtrlR          = 18,
  KeyCodeCtrlD          = 4,
  KeyCodeCtrlB          = 2,
  KeyCodeCtrlO          = 15,
  KeyCodeCtrlE          = 5,
  KeyCodeCtrlU          = 21,

  KeyCodePrintableStart = 32,
  KeyCodePrintableEnd   = 126,

  KeyCodeAsciiEnd       = 255,

  KeyCodeUnknown,
  KeyCodeNone,

  KeyCodeUp,
  KeyCodeDown,
  KeyCodeLeft,
  KeyCodeRight,
  KeyCodeEnd,
  KeyCodeHome,

  KeyCodeShiftUp,
  KeyCodeShiftDown,
  KeyCodeShiftLeft,
  KeyCodeShiftRight,
  KeyCodeShiftEnd,
  KeyCodeShiftHome,

  KeyCodeCtrlUp,
  KeyCodeCtrlDown,
  KeyCodeCtrlLeft,
  KeyCodeCtrlRight,
};

//--------------------------------------------------------------------------------------------------

enum {
  BarTypeOpen,
  BarTypeNew,
  BarTypeCommand,
  BarTypeCount,
};

//--------------------------------------------------------------------------------------------------

enum {
  ModeNormal,
  ModeBar,
};

//--------------------------------------------------------------------------------------------------

enum {
  UserKeyFocusNext     = KeyCodeShiftRight,
  UserKeyFocusPrevious = KeyCodeShiftLeft,
  UserKeyPageUp        = KeyCodeShiftUp,
  UserKeyPageDown      = KeyCodeShiftDown,
  UserKeyExit          = KeyCodeCtrlQ,
  UserKeyOpen          = KeyCodeCtrlG,
  UserKeyNew           = KeyCodeCtrlN,
  UserKeySave          = KeyCodeCtrlS,
  UserKeyCommand       = KeyCodeCtrlR,
  UserKeyCompile       = KeyCodeCtrlD,
  UserKeyMark          = KeyCodeCtrlB,
  UserKeyCopy          = KeyCodeCtrlC,
  UserKeyPaste         = KeyCodeCtrlV,
  UserKeyCut           = KeyCodeCtrlX,
};

//--------------------------------------------------------------------------------------------------

struct Line {
  CharArray chars;
  CharArray colors;

  bool redraw;
};

//--------------------------------------------------------------------------------------------------

struct File {
  CharArray path;
  LineArray lines;

  bool redraw;
  bool saved;
  bool issues;
};

//--------------------------------------------------------------------------------------------------

struct Region {
  int x;
  int y;

  int width;
  int height;

  float split;
  bool stacked;

  Region* parent;
  Region* childs[2];
  Window* window;
};

//--------------------------------------------------------------------------------------------------

struct Window {
  File* file;
  Region* region;

  bool redraw;

  int offset_x;
  int offset_y;

  int cursor_x;
  int cursor_y;
  int cursor_x_ideal;

  bool mark_valid;
  int mark_x;
  int mark_y;


  // Cleanup below this point.
  int bar_type;
  int bar_cursor;
  int bar_offset;
  bool bar_focused;
  CharArray bar_chars;

  Time error_time;
  bool error_active;
  CharArray error_message;

 // Remove the error timeout. Escape or any new command/character should do!
};

//--------------------------------------------------------------------------------------------------

static char* bar_message[BarTypeCount] = {
  [BarTypeOpen]    = "open: ",
  [BarTypeNew]     = "new: ",
  [BarTypeCommand] = "command: "
};

//--------------------------------------------------------------------------------------------------

static Termios saved_terminal;
static CharArray framebuffer;
static Window* focused_window;
static Region master_region;
static WindowArray windows;
static FileArray files;
static CharArray clipboard;

static bool redraw[1024];
static bool running = true;

static int previous_keycode;
static int current_scheme;

//--------------------------------------------------------------------------------------------------

static void delete_line(Line* line);
static Line* append_line(File* file);
static void append_chars(Line* line, char* data, int size);
static Window* split_window(Window* window, bool vertical);
static void child_resized(Region* region);
static void resize_child_regions(Region* region);
static void resize_window(Window* window, int amount);
static void remove_window(Window* window);
static void swap_windows(Window* window);
static void focus_next();
static void render_line(Line* line);
static void focus_previous();
static void compile_and_parse_issues();

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

static void set_window_cursor(Window* window, int x, int y) {
  set_cursor(window->region->x + x, window->region->y + y);
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

static void invert() {
  print("\x1b[7m");
}

//--------------------------------------------------------------------------------------------------

static int invert_color(int color) {
  int r = 255 - ((color >> 16) & 0xFF);
  int g = 255 - ((color >> 8 ) & 0xFF);
  int b = 255 - ((color >> 0 ) & 0xFF);

  return r << 16 | g << 8 | b;
}

//--------------------------------------------------------------------------------------------------

static void set_terminal_background_color() {
  int color = schemes[current_scheme].colors[ColorTypeEditorBackground];
  print("\x1b]11;rgb:%x/%x/%x\x7", (color >> 16) & 0xFF, (color >> 8) & 0xFF, color & 0xFF);
}

//--------------------------------------------------------------------------------------------------

static void set_cursor_color(int type) {
  int color = schemes[current_scheme].colors[type];
  print("\x1b]12;rgb:%x/%x/%x\x7", (color >> 16) & 0xFF, (color >> 8) & 0xFF, color & 0xFF);
}

//--------------------------------------------------------------------------------------------------

static void reset_cursor_color() {
  print("\x1b]104;258\x7");
}

//--------------------------------------------------------------------------------------------------

static void reset_terminal_colors() {
  print("\x1b]104;256\x7");
  print("\x1b]104;257\x7");
  print("\x1b]104;258\x7");
}

//--------------------------------------------------------------------------------------------------

static void set_background_color(int type) {
  int color = schemes[current_scheme].colors[type];
  print("\x1b[48;2;%d;%d;%dm", (color >> 16) & 0xFF, (color >> 8) & 0xFF, color & 0xFF);
}

//--------------------------------------------------------------------------------------------------

static void set_foreground_color(int type) {
  int color = schemes[current_scheme].colors[type];
  print("\x1b[38;2;%d;%d;%dm", (color >> 16) & 0xFF, (color >> 8) & 0xFF, color & 0xFF);
}

//--------------------------------------------------------------------------------------------------

static void bold() {
  print("\x1b[1m");
}

//--------------------------------------------------------------------------------------------------

static void clear_formatting() {
  print("\x1b[0m");
}

//--------------------------------------------------------------------------------------------------

static int get_input() {
  char keys[64];
  int size = read(STDIN_FILENO, keys, sizeof(keys));
  if (!size) return KeyCodeNone;

  for (int i = 0; i < size; i++) {
    debug_print("%d, ", keys[i]);
  }
  debug_print("\n");

  int code = keys[0];

  if (code == '\x1b' && size > 2 && keys[1] == '[') {
    if (size == 3) {
      switch (keys[2]) {
        case 'A': code = KeyCodeUp;       break;
        case 'B': code = KeyCodeDown;     break;
        case 'D': code = KeyCodeLeft;     break;
        case 'C': code = KeyCodeRight;    break;
        case 'H': code = KeyCodeHome;     break;
        case 'K': code = KeyCodeShiftEnd; break;
      }
    }
    else if (size == 4) {
      if (keys[2] == '4' && keys[3] == '~') {
        code = KeyCodeEnd;
      }
      else if (keys[2] == '2' && keys[3] == 'J') {
        code = KeyCodeShiftHome;
      }
    }
    else if (size == 6 && keys[2] == '1' && keys[3] == ';') {
      if (keys[4] == '2') {
        switch (keys[5]) {
          case 'A': code = KeyCodeShiftUp;    break;
          case 'B': code = KeyCodeShiftDown;  break;
          case 'D': code = KeyCodeShiftLeft;  break;
          case 'C': code = KeyCodeShiftRight; break;
        }
      }
      else if (keys[4] == '5') {
        switch (keys[5]) {
          case 'A': code = KeyCodeCtrlUp;    break;
          case 'B': code = KeyCodeCtrlDown;  break;
          case 'D': code = KeyCodeCtrlLeft;  break;
          case 'C': code = KeyCodeCtrlRight; break;
        }
      }
    }
  }
  else if (code == '\x1b' && size > 1) {
    code = KeyCodeNone;
  }

  return code;
}

//--------------------------------------------------------------------------------------------------

static void get_time(Time* time) {
  gettimeofday(time, NULL);
}

//--------------------------------------------------------------------------------------------------

static int get_elapsed(Time* start, Time* end) {
  return (end->tv_sec * 1000LL + end->tv_usec / 1000) - (start->tv_sec * 1000LL + start->tv_usec / 1000);
}

//--------------------------------------------------------------------------------------------------

static void error(Window* window, char* message, ...) {
  char_array_clear(&window->error_message);
  char_array_extend(&window->error_message, 1024);

  va_list arguments;
  va_start(arguments, message);
  int size = vsnprintf(window->error_message.items, 1024, message, arguments);
  va_end(arguments);

  window->error_message.count = size;

  window->error_active = true;
  get_time(&window->error_time);
}

//--------------------------------------------------------------------------------------------------

static File* new_file(char* path, int path_size) {
  File* file = calloc(1, sizeof(File));
  file_array_append(&files, file);
  file->saved = true;
  char_array_append_multiple(&file->path, path, path_size);
  file->redraw = true;

  return file;
}

//--------------------------------------------------------------------------------------------------

static Window* allocate_window() {
  Window* window = calloc(1, sizeof(Window));
  window->redraw = true;
  window_array_append(&windows, window);
  return window;
}

//--------------------------------------------------------------------------------------------------

static void free_window(Window* window) {
  for (int i = 0; i < windows.count; i++) {
    if (window == windows.items[i]) {
      window_array_remove(&windows, i);
      free(window);
      return;
    }
  }

  assert(0);
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
  for (int i = 0; i < files.count; i++) {

    File* file = files.items[i];
    if (path_size == file->path.count && !memcmp(path, file->path.items, path_size)) {
      debug_print("Returnig existing file: %d\n", file->lines.count);
      return file;
    }
  }

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

  if (index < size) {
    Line* line = append_line(file);
    append_chars(line, &data[index], size - index - cr);
  }

  debug_print("Returnig new file: %d\n", file->lines.count);
  return file;
}

//--------------------------------------------------------------------------------------------------

static File* create_file(char* path, int path_size) {
  File* file = new_file(path, path_size);
  file->saved = false;
  append_line(file);
  return file;
}

//--------------------------------------------------------------------------------------------------

static bool save_file(File* file) {
  LineArray* lines = &file->lines;

  int fd = open(file->path.items, O_WRONLY | O_CREAT | O_TRUNC, 0666); // Do we want r/w for all users?
  if (fd < 0) return false;

  for (int i = 0; i < lines->count; i++) {
    if (i) write(fd, "\r\n", 2);

    Line* line = lines->items[i];
    write(fd, line->chars.items, line->chars.count);
  }

  close(fd);

  file->saved = true;
  return true;
}

//--------------------------------------------------------------------------------------------------

static Line* append_line(File* file) {
  Line* line = calloc(1, sizeof(Line));
  line_array_append(&file->lines, line);
  file->redraw = true;
  return line;
}

//--------------------------------------------------------------------------------------------------

static Line* insert_line(File* file, int offset) {
  Line* line = calloc(1, sizeof(Line));
  line_array_insert(&file->lines, line, offset);
  file->redraw = true;
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
  line->redraw = true;
  render_line(line);
}

//--------------------------------------------------------------------------------------------------

static void append_char(Line* line, char c) {
  char_array_append(&line->chars, c);
  line->redraw = true;
  render_line(line);
}

//--------------------------------------------------------------------------------------------------

static void delete_char(Line* line, int index) {
  char_array_remove(&line->chars, index);
  line->redraw = true;
  render_line(line);
}

//--------------------------------------------------------------------------------------------------

static void render_line(Line* line) {
  char_array_extend(&line->colors, line->chars.count);

  char* data = line->chars.items;
  int size = line->chars.count;
  char prev = 0;
  bool comment = false;

  for (int i = 0; i < size; i++) {
    if (data[i] == '/' && prev == '/') {
      line->colors.items[i - 1] = 31;
      comment = true;
    }

    if (comment) {
      line->colors.items[i] = 31;
    }
    else {
      line->colors.items[i] = 0;
    }

    prev = data[i];
  }
}

//--------------------------------------------------------------------------------------------------

static int count_digits(int number) {
  int digits = 0;

  do {
    number /= 10;
    digits++;
  } while (number);

  return digits;
}

//--------------------------------------------------------------------------------------------------

static int get_left_padding(Window* window) {
  if (!window->file) {
    return LinenumberMargin + 1;
  }
  else {
    return (window->region->x ? 2 : 0) + count_digits(window->file->lines.count) + LinenumberMargin + (window->file->issues == true);
  }
}

//--------------------------------------------------------------------------------------------------

static int get_left_bar_padding(Window* window) {
  return StatusBarLeftPadding + strlen(bar_message[window->bar_type]);
}

//--------------------------------------------------------------------------------------------------

static void get_active_size(Window* window, int* width, int* height) {
  int x = window->region->x ? 2 : 0;
  *width  = window->region->width - get_left_padding(window) - x;
  *height = window->region->height - StatusBarCount;
}

//--------------------------------------------------------------------------------------------------

static int get_visible_line_count(Window* window) {
  int width, height;
  get_active_size(window, &width, &height);
  return min(height, window->file->lines.count - window->offset_y);
}

//--------------------------------------------------------------------------------------------------

static int get_updated_offset(int cursor, int offset, int width, int left_margin, int right_margin) {
  int adjust = offset + left_margin - cursor;

  if (adjust > 0) {
    offset = max(offset - adjust, 0);
  }

  adjust = cursor - (offset + width - right_margin);
  
  if (adjust > 0) {
    offset += adjust;
  }

  return offset;
}

//--------------------------------------------------------------------------------------------------

static void update_window_offsets(Window* window) {
  int prev_offset_x = window->offset_x;
  int prev_offset_y = window->offset_y;

  int width, height;
  get_active_size(window, &width, &height);

  window->offset_x = get_updated_offset(window->cursor_x, window->offset_x, width, WindowCursorMarginLeft, WindowCursorMarginRight);
  window->offset_y = get_updated_offset(window->cursor_y, window->offset_y, height, WindowCursorMarginTop, WindowCursorMarginBottom);

  if (window->offset_x != prev_offset_x || window->offset_y != prev_offset_y) {
    window->redraw = true;
  }
}

//--------------------------------------------------------------------------------------------------

static void limit_window_cursor(Window* window) {
  window->cursor_x = max(window->cursor_x, 0);
  window->cursor_y = max(window->cursor_y, 0);
  window->cursor_y = min(window->cursor_y, window->file->lines.count - 1);
  window->cursor_x = min(window->cursor_x, window->file->lines.items[window->cursor_y]->chars.count);
}

//--------------------------------------------------------------------------------------------------

static void update_window_cursor_x(Window* window, int x) {
  window->cursor_x = x;
  window->cursor_x_ideal = x;
  limit_window_cursor(window);
}

//--------------------------------------------------------------------------------------------------

static void update_window_cursor_y(Window* window, int y) {
  window->cursor_y = y;
  window->cursor_x = window->cursor_x_ideal;
  limit_window_cursor(window);
}

//--------------------------------------------------------------------------------------------------

static void update_window_offset_y(Window* window, int offset) {
  window->offset_y = limit(offset, 0, window->file->lines.count);
  window->redraw = true;
}

//--------------------------------------------------------------------------------------------------

static int get_leading_spaces(Line* line) {
  int i;
  for (i = 0; i < line->chars.count && line->chars.items[i] == ' '; i++);
  return i;
}

//--------------------------------------------------------------------------------------------------

static char get_last_char(Line* line) {
  return line->chars.count ? line->chars.items[line->chars.count - 1] : 0;
}

//--------------------------------------------------------------------------------------------------

static void append_spaces(Line* line, int count) {
  while (count--) {
    append_char(line, ' ');
  }
}

//--------------------------------------------------------------------------------------------------

static void file_insert_chars(Window* window, char* data, int size, bool smart) {
  static CharArray buffer = {0};

  Line* line = window->file->lines.items[window->cursor_y];

  char_array_clear(&buffer);
  char_array_append_multiple(&buffer, &line->chars.items[window->cursor_x], line->chars.count - window->cursor_x);
  line->chars.count = window->cursor_x;

  for (int i = 0; i < size; i++) {
    if (data[i] == '\n') {
      window->cursor_x = 0;
      window->cursor_y++;
      render_line(line);
      line = insert_line(window->file, window->cursor_y);
    }
    else {
      window->cursor_x++;
      append_char(line, data[i]);
    }
  }

  render_line(line);

  // Smart indentation.
  if (smart && size == 1 && data[0] == '\n') {
    Line* previous_line = window->file->lines.items[window->cursor_y - 1];
    int spaces = get_leading_spaces(previous_line);

    if (get_last_char(previous_line) == '{') {
      if (previous_keycode == '{') {
        Line* next_line = insert_line(window->file, window->cursor_y + 1);
        append_spaces(next_line, spaces);
        append_char(next_line, '}');
      }

      spaces += SpacesPerTab;
    }

    append_spaces(line, spaces);
    window->cursor_x = line->chars.count;
  }

  append_chars(line, buffer.items, buffer.count);
}

//--------------------------------------------------------------------------------------------------

static void file_insert_char(Window* window, char c, bool smart) {
  file_insert_chars(window, &c, 1, smart);
}

//--------------------------------------------------------------------------------------------------

static void file_delete_char(Window* window) {
  Line* current = window->file->lines.items[window->cursor_y];

  if (window->cursor_x) {
    delete_char(current, window->cursor_x - 1);
    update_window_cursor_x(window, window->cursor_x - 1);
  }
  else if (window->cursor_y) {
    Line* previous = window->file->lines.items[window->cursor_y - 1];

    update_window_cursor_x(window, previous->chars.count);
    append_chars(previous, current->chars.items, current->chars.count);

    line_array_remove(&window->file->lines, window->cursor_y);
    update_window_cursor_y(window, window->cursor_y - 1);

    window->file->redraw = true;
    window->redraw = true;
  }
}

//--------------------------------------------------------------------------------------------------

static void enter_bar_mode(Window* window, int type) {
  window->bar_focused = true;
  window->bar_type = type;
  window->error_active = false;
}

//--------------------------------------------------------------------------------------------------

static bool is_name_letter(char c) {
  return ('0' <= c && c <= '9') || ('A' <= c && c <= 'Z') || ('a' <= c && c <= 'z') || (c == '_');
}

//--------------------------------------------------------------------------------------------------

static int get_delete_count(Window* window, bool delete_word) {
  Line* line = window->file->lines.items[window->cursor_y];
  char* data = line->chars.items;
  int size = window->cursor_y;

  int space_count = 0;
  int char_count = 0;
  bool leading = true;

  debug_print("Line: %.*s\n", size, data);

  for (int i = 0; i < window->cursor_x; i++) {
    char c = data[i];

    if (c != ' ') {
      leading = false;
      space_count = 0;
    }
    else {
      space_count++;
    }

    if (is_name_letter(c)) {
      char_count++;
    }
    else {
      char_count = 0;
    }
  }

  if (delete_word) {
    return max(1, max(space_count, char_count));
  }

  if (leading) {
    return ((space_count % SpacesPerTab) == 0) ? SpacesPerTab : 1;
  }

  return 1;
}

//--------------------------------------------------------------------------------------------------

// Makes sure the start mark comes before the end mark.
static void normalize_block(int* start_x, int* start_y, int* end_x, int* end_y) {
  if (*start_y > *end_y || (*start_y == *end_y && *start_x > *end_x)) {
    int tmp_x = *start_x;
    int tmp_y = *start_y;

    *start_x = *end_x;
    *start_y = *end_y;

    *end_x = tmp_x;
    *end_y = tmp_y;
  }
}

//--------------------------------------------------------------------------------------------------

static void handle_delete(Window* window, bool delete_word) {
  int delete_count = get_delete_count(window, delete_word);
  while (delete_count--) {
    file_delete_char(window);
  }
}

//--------------------------------------------------------------------------------------------------

static void copy_region(Window* window, int start_x, int start_y, int end_x, int end_y) {
  debug_print("Copying\n");

  clipboard.count = 0;

  while (start_y != end_y) {
    Line* line = window->file->lines.items[start_y];
    for (int i = start_x; i < line->chars.count; i++) {
      char_array_append(&clipboard, line->chars.items[i]);
    }

    char_array_append(&clipboard, '\n');
    start_x = 0;
    start_y++;
  }

  Line* line = window->file->lines.items[start_y];
  debug_print("startx = %d endx = %d\n", start_x, end_x);
  for (int i = start_x; i < end_x; i++) {
    char_array_append(&clipboard, line->chars.items[i]);
  }

  for (int i = 0; i < clipboard.count; i++) {
    if (clipboard.items[i] == '\n') {
      debug_print("\\n\n");
    }
    else {
      debug_print("%c", clipboard.items[i]);
    }
  }

  debug_print("\n");
}

//--------------------------------------------------------------------------------------------------

static void delete_region(Window* window, int start_x, int start_y, int end_x, int end_y) {
  window->file->redraw = true;

  if (start_y == end_y) {
    debug_print("Same line\n");
    Line* start = window->file->lines.items[start_y];
    int count = end_x - start_x;
    while (count--) {
      char_array_remove(&start->chars, start_x);
    }
    return;
  }

  int index = start_y;

  for (int i = start_y + 1; i < end_y; i++) {
    delete_line(window->file->lines.items[start_y + 1]);
    line_array_remove(&window->file->lines, start_y + 1);
  }

  Line* start = window->file->lines.items[start_y];
  Line* end = window->file->lines.items[start_y + 1];

  start->chars.count = start_x;
  char_array_append_multiple(&start->chars, end->chars.items + end_x, end->chars.count - end_x);
  
  while (end_x--) {
    char_array_remove(&end->chars, 0);
  }
  end->redraw = true;
  window->redraw = true;
}

//--------------------------------------------------------------------------------------------------

static void handle_copy(Window* window) {
  if (!window->file || !window->mark_valid) {
    error(window, "copy error");
    return;
  }

  int start_x = window->mark_x;
  int start_y = window->mark_y;
  int end_x = window->cursor_x;
  int end_y = window->cursor_y;

  normalize_block(&start_x, &start_y, &end_x, &end_y);

  debug_print("Copying from [%d, %d] to [%d, %d]\n", start_x, start_y, end_x, end_y);
  
  copy_region(window, start_x, start_y, end_x, end_y);
}

//--------------------------------------------------------------------------------------------------

static void handle_cut(Window* window) {
  if (!window->file || !window->mark_valid) {
    error(window, "cut error");
    return;
  }

  int start_x = window->mark_x;
  int start_y = window->mark_y;
  int end_x = window->cursor_x;
  int end_y = window->cursor_y;

  normalize_block(&start_x, &start_y, &end_x, &end_y);

  debug_print("Cutting from [%d, %d] to [%d, %d]\n", start_x, start_y, end_x, end_y);
  
  copy_region(window, start_x, start_y, end_x, end_y);
  delete_region(window, start_x, start_y, end_x, end_y);

  window->cursor_x = start_x;
  window->cursor_y = start_y;
}

//--------------------------------------------------------------------------------------------------

static void handle_paste(Window* window) {
  if (!window->file || clipboard.count == 0) {
    error(window, "paste error");
    return;
  }

  for (int i = 0; i < clipboard.count; i++) {
    file_insert_char(window, clipboard.items[i], false);
  }
}

//--------------------------------------------------------------------------------------------------

static void editor_handle_keypress(Window* window, int keycode) {
  if (window->file && KeyCodePrintableStart <= keycode && keycode <= KeyCodePrintableEnd) {
    file_insert_char(window, keycode, true);
    window->file->saved = false;
  }
  else if (keycode == UserKeyOpen) {
    enter_bar_mode(window, BarTypeOpen);
  }
  else if (keycode == UserKeyFocusNext) {
    focus_next();
  }
  else if (keycode == UserKeyFocusPrevious) {
    focus_previous();
  }
  else if (keycode == UserKeyNew) {
    enter_bar_mode(window, BarTypeNew);
  }
  else if (keycode == UserKeyCommand) {
    enter_bar_mode(window, BarTypeCommand);
  }
  else if (window->file) switch (keycode) {
    case KeyCodeUp:
      update_window_cursor_y(window, window->cursor_y - 1);
      break;

    case KeyCodeDown:
      update_window_cursor_y(window, window->cursor_y + 1);
      break;

    case UserKeyPageUp:
      update_window_cursor_y(window, window->cursor_y - window->region->height / 2);
      update_window_offset_y(window, window->offset_y - window->region->height / 2);
      break;

    case UserKeyPageDown:
      update_window_cursor_y(window, window->cursor_y + window->region->height / 2);
      update_window_offset_y(window, window->offset_y + window->region->height / 2);
      break;

    case KeyCodeShiftHome:
      update_window_cursor_x(window, 0);
      update_window_cursor_y(window, 0);
      break;

    case KeyCodeShiftEnd:
      update_window_cursor_x(window, window->file->lines.items[window->file->lines.count - 1]->chars.count);
      update_window_cursor_y(window, window->file->lines.count - 1);
      break;

    case KeyCodeLeft:
      update_window_cursor_x(window, window->cursor_x - 1);
      break;

    case KeyCodeRight:
      update_window_cursor_x(window, window->cursor_x + 1);
      break;

    case KeyCodeHome: {
      int spaces = get_leading_spaces(window->file->lines.items[window->cursor_y]);
      if (window->cursor_x > spaces) {
        update_window_cursor_x(window, spaces);
      }
      else {
        update_window_cursor_x(window, 0);
      }
      break;
    }

    case KeyCodeEnd:
      update_window_cursor_x(window, window->file->lines.items[window->cursor_y]->chars.count);
      break;

    case KeyCodeCtrlUp:
      resize_window(window, 1);
      break;

    case KeyCodeCtrlDown:
      resize_window(window, -1);
      break;

    case KeyCodeCtrlLeft:
      remove_window(window);
      break;

    case KeyCodeCtrlRight:
      swap_windows(window);
      break;
    
    case KeyCodeCtrlDelete:
      handle_delete(window, true);
      break;

    case KeyCodeDelete:
      handle_delete(window, false);
      break;

    case KeyCodeTab:
      window->file->saved = false;
      for (int i = 0; i < SpacesPerTab; i++) {
        file_insert_char(window, ' ', true);
      }
      break;

    case KeyCodeEnter:
      window->file->saved = false;
      file_insert_char(window, '\n', true);
      break;
    
    case UserKeyFocusNext:
      focus_next();
      break;

    case UserKeyFocusPrevious:
      focus_previous();
      break;

    case UserKeyNew:
      enter_bar_mode(window, BarTypeNew);
      break;

    case UserKeyCommand:
      enter_bar_mode(window, BarTypeCommand);
      break;

    case KeyCodeEscape:
      window->error_active = false;
      break;
    
    case UserKeyMark:
      window->mark_valid = true;
      window->mark_x = window->cursor_x;
      window->mark_y = window->cursor_y;
      break;

    case UserKeyCut:
      handle_cut(window);
      break;

    case UserKeyCopy:
      handle_copy(window);
      break;

    case UserKeyPaste:
      handle_paste(window);
      break;
    
    case UserKeySave:
      if (!save_file(window->file)) {
        error(window, "can not save file `%.*s`", window->file->path.count, window->file->path.items);
      }

      break;

    default:
      debug_print("Unhandled window keycode: %d\n", keycode);
  }
}

//--------------------------------------------------------------------------------------------------

static void change_file(Window* window, File* file) {
  // Todo: save information in a separate structure per window per file for later reuse.
  window->file = file;  
  window->redraw = true;
  window->cursor_x = 0;
  window->cursor_y = 0;
  window->offset_x = 0;
  window->offset_y = 0;
  window->cursor_x_ideal = 0;
  window->mark_valid = false;
}

//--------------------------------------------------------------------------------------------------

static void skip_spaces(char** data) {
  char* tmp = *data;

  while (*tmp == ' ') {
    tmp++;
  }

  *data = tmp;
}

//--------------------------------------------------------------------------------------------------

static void skip_to_start_of_line(char** data) {
  char* tmp = *data;

  while (*tmp && *tmp != '\n') {
    tmp++;
  }

  *data = ++tmp;
}

//--------------------------------------------------------------------------------------------------

static bool skip_keyword(char** data, char* keyword) {
  skip_spaces(data);

  int size = strlen(keyword);

  if (!strncmp(*data, keyword, size)) {
    *data += size;
    return true;
  }

  return false;
}

//--------------------------------------------------------------------------------------------------

static char* get_name(char** data, int* size) {
  skip_spaces(data);

  char* tmp = *data;
  char* start = tmp;

  while (*tmp && is_name_letter(*tmp)) {  // Note: will accept names which start with numbers.
    tmp++;
  }

  *size = tmp - start;
  *data = tmp;

  return start;
}

//--------------------------------------------------------------------------------------------------

static bool skip_char(char** data, char c) {
  skip_spaces(data);
  
  if (**data == c) {
    *data += 1;
    return true;
  }
  
  return false;
}

//--------------------------------------------------------------------------------------------------

static bool read_number(char** data, int* number) {
  skip_spaces(data);

  char* end;
  int value = strtol(*data, &end, 10);

  int size = end - *data;
  *data += size;

  if (size) {
    *number = value;
    return true;
  }

  return false;
}

//--------------------------------------------------------------------------------------------------

static void handle_command(Window* window) {
  // Add null termination.
  char_array_append(&window->bar_chars, 0);
  window->bar_chars.count--;

  char* command = window->bar_chars.items;

  if (skip_keyword(&command, "split")) {
    if (skip_char(&command, '-')) {
      split_window(window, true);
    }
    else if (skip_char(&command, '|')) {
      split_window(window, false);
    }
    else {
      error(window, "cant split");
    }
  }
  else if (skip_keyword(&command, "scheme")) {
    int scheme = -1;
    if (!read_number(&command, &scheme)) {
      int size;
      char* name = get_name(&command, &size);
      if (size) {
        for (int i = 0; i < ColorSchemeCount; i++) {
          if (!strncmp(schemes[i].name, name, size)) {
            scheme = i;
            break;
          }
        }
      }
    }
    if (0 <= scheme && scheme < ColorSchemeCount) {
      current_scheme = scheme;
      set_terminal_background_color();

      for (int i = 0; i < windows.count; i++) {
        windows.items[i]->redraw = true;
      }
    }
  }
  else if (skip_keyword(&command, "close")) {
    remove_window(window);
  }
  else {
    error(window, "unknow command `%.*s`", window->bar_chars.count, window->bar_chars.items);
  }
}

//--------------------------------------------------------------------------------------------------

static void bar_handle_enter(Window* window) {
  char* data = window->bar_chars.items;
  int size = window->bar_chars.count;

  switch (window->bar_type) {
    case BarTypeOpen: {
      File* file = open_file(window->bar_chars.items, window->bar_chars.count);
      
      if (file) {
        change_file(window, file);
      }
      else {
        error(window, "can not open file `%.*s`", size, data);
      }
      break;
    }

    case BarTypeNew:
      File* file = create_file(window->bar_chars.items, window->bar_chars.count);
      change_file(window, file);
      break;

    case BarTypeCommand:
      handle_command(window);
      break;

    default:
      debug_print("Unhandled bar type\n");
  }

  // More error handling is needed.
  char_array_clear(&window->bar_chars);
}

//--------------------------------------------------------------------------------------------------

static void exit_bar_view(Window* window) {
  window->bar_focused = 0;
  window->bar_cursor = 0;
  window->bar_chars.count = 0;
  window->bar_offset = 0;
}

//--------------------------------------------------------------------------------------------------

static void bar_handle_keypress(Window* window, int keycode) {
  if (KeyCodePrintableStart <= keycode && keycode <= KeyCodePrintableEnd) {
    char_array_insert(&window->bar_chars, keycode, window->bar_cursor++);
  }
  else switch (keycode) {
    case KeyCodeEscape:
      exit_bar_view(window);
      break;

    case KeyCodeLeft:
      window->bar_cursor = max(window->bar_cursor - 1, 0);
      break;

    case KeyCodeRight:
      window->bar_cursor = min(window->bar_cursor + 1, window->bar_chars.count);
      break;

    case KeyCodeHome:
      window->bar_cursor = 0;
      break;

    case KeyCodeEnd:
      window->bar_cursor = window->bar_chars.count;
      break;
    
    case KeyCodeCtrlDelete:
    case KeyCodeDelete:
      if (window->bar_cursor) {
        char_array_remove(&window->bar_chars, window->bar_cursor - 1);
        window->bar_cursor--;
      }
      break;

    case KeyCodeEnter:
      bar_handle_enter(window);
      exit_bar_view(window);
      break;
    
    default:
      debug_print("Unhandled bar keycode: %d\n", keycode);
  }
}

//--------------------------------------------------------------------------------------------------

static void update() {
  while (1) {
    bool done = false;
    // Process expired error messages.
    for (int i = 0; i < windows.count; i++) {
      Window* window = windows.items[i];
      if (window->error_active) {
        Time now;
        get_time(&now);
        if (get_elapsed(&window->error_time, &now) > ErrorShowTime) {
          window->error_active = false;
          done = true;
        }
      }
    }

    int keycode = get_input();

    if (keycode == UserKeyExit) {
      running = false;
      return;
    }

    if (keycode != KeyCodeNone) {
      Window* window = focused_window;

      if (window->bar_focused) {
        bar_handle_keypress(window, keycode);
      }
      else {
        editor_handle_keypress(window, keycode);
        previous_keycode = keycode;
      }

      break;
    }

    if (done) {
      break;
    }
  }
}

//--------------------------------------------------------------------------------------------------

static void render_status_bar(Window* window) {
  static const char unsaved_string[] = "*";
  static const char marked_string[] = "[] ";
  static const char nofile_string[] = "no file";

  int width = window->region->width - StatusBarLeftPadding - StatusBarRightPadding;
  int percent;

  if (window->file) {
    assert(window->file->lines.count);
    percent = 100 * window->cursor_y / window->file->lines.count;

    int path_width = min(window->file->path.size, MaxPathWidth) + 1;
    int unsaved_width = !window->file->saved ? sizeof(unsaved_string) - 1 : 0;
    int marked_width = window->mark_valid ? sizeof(marked_string) - 1 : 0;
    int percent_width = count_digits(percent) + sizeof((char)'%');

    width -= path_width + unsaved_width + marked_width + percent_width;
  }
  else {
    width -= sizeof(nofile_string) - 1;
  }

  //invert();
  set_background_color(ColorTypeStatusBarBackground);
  set_foreground_color(ColorTypeStatusBarForeground);

  set_window_cursor(window, 0, window->region->height - 1);

  print("%*c", StatusBarLeftPadding, ' ');

  if (window->error_active) {
    set_foreground_color(ColorTypeStatusBarError);
    width -= print("error: ");
    width -= print("%.*s", min(window->error_message.count, width), window->error_message.items);
    set_foreground_color(ColorTypeStatusBarForeground);
  }
  else if (window->bar_focused) {
    width -= print("%s", bar_message[window->bar_type]);

    window->bar_offset = get_updated_offset(window->bar_cursor, window->bar_offset, width, BarCursorMarginLeft, BarCursorMarginRight);
    int bar_size = min(window->bar_chars.count - window->bar_offset, width - StatusBarCommandPadding);

    width -= print("%.*s", bar_size, &window->bar_chars.items[window->bar_offset]);
  }
  
  if (width) {
    print("%*c", width, ' ');
  }

  if (window == focused_window) {
    bold();
  }
  
  if (window->file) {
    if (window->mark_valid) {
      print("%s", marked_string);
    }

    print("%.*s", window->file->path.count, window->file->path.items);

    if (!window->file->saved) {
      print("%s", unsaved_string);
    }

    print(" %d%%", percent);
  }
  else {
    print("%s", nofile_string);
  }

  print("%*c", StatusBarRightPadding, ' ');
  
  clear_formatting();
}

//--------------------------------------------------------------------------------------------------

static void render_window(Window* window) {
  if (window->file) {
    int width, height;
    get_active_size(window, &width, &height);

    int number_width = window->file ? count_digits(window->file->lines.count) : 1;

    int color = 0;

    set_background_color(ColorTypeEditorBackground);
    set_foreground_color(ColorTypeEditorForeground);

    for (int j = 0; j < get_visible_line_count(window); j++) {
      if (!redraw[window->region->y + j]) continue;

      Line* line = window->file->lines.items[window->offset_y + j];

      set_window_cursor(window, 0, j);

      if (window->region->x) {
        set_background_color(ColorTypeStatusBarBackground);
        print(" ");
        set_background_color(ColorTypeEditorBackground);
        print(" ");
      }

      print("%*d", number_width, window->offset_y + j);
      print("%*c", LinenumberMargin, ' ');

      int size = max(min(line->chars.count - window->offset_x, width), 0);

      for (int i = 0; i < size; i++) {
        int index = window->offset_x + i;

        if (line->colors.items[index] != color) {
          color = line->colors.items[index];

          if (color) {
            set_foreground_color(ColorTypeMatchBackground);
          }
          else {
            set_foreground_color(ColorTypeEditorForeground);
          }
        }

        print("%c", line->chars.items[index]);
      }

      if (color) {
        set_foreground_color(ColorTypeEditorForeground);
      }
    }

    for (int j = get_visible_line_count(window); j < height; j++) {
      if (!redraw[window->region->y + j]) continue;
      set_window_cursor(window, 0, j);
      if (window->region->x) {
        set_background_color(ColorTypeStatusBarBackground);
        print(" ");
        set_background_color(ColorTypeEditorBackground);
        print(" ");
      }
    }
  }

  render_status_bar(window);
}

//--------------------------------------------------------------------------------------------------

static void mark_lines_for_redraw() {
  memset(redraw, 0, master_region.height);

  for (int i = 0; i < windows.count; i++) {
    Window* window = windows.items[i];

    // Redraw entire occupied region if either the window is moved or the file is dirty.
    if (window->redraw || (window->file && window->file->redraw)) {
      window->redraw = false;

      for (int j = 0; j < window->region->height; j++) {
        redraw[window->region->y + j] = true;
      }
    }

    // Redraw dirty visible lines.
    if (window->file) {
      for (int j = 0; j < get_visible_line_count(window); j++) {
        if (window->file->lines.items[window->offset_y + j]->redraw) {
          redraw[window->region->y + j] = true;
        }
      }
    }

    // Redraw status bars.
    for (int j = 0; j < StatusBarCount; j++) {
      redraw[window->region->y + window->region->height - j - 1] = true;
    }
  }

  // Files are shared and must be marked clean after all checks are done.
  for (int i = 0; i < windows.count; i++) {
    Window* window = windows.items[i];

    if (window->file) {
      window->file->redraw = false;

      for (int j = 0; j < get_visible_line_count(window); j++) {
        window->file->lines.items[window->offset_y + j]->redraw = false;
      }
    }
  }
}

//--------------------------------------------------------------------------------------------------

static void render() {
  for (int i = 0; i < windows.count; i++) {
    update_window_offsets(windows.items[i]);
  }

  mark_lines_for_redraw();

  set_background_color(ColorTypeEditorBackground);

  for (int i = 0; i < master_region.height; i++) {
    if (redraw[i]) {
      clear_line(i);
    }
  }

  hide_cursor();
  
  for (int i = 0; i < windows.count; i++) {
    render_window(windows.items[i]);
  }

  Window* window = focused_window;

  int cursor_x;
  int cursor_y;

  if (window->bar_focused) {
    set_cursor_color(ColorTypeStatusBarCursor);
    cursor_x = window->bar_cursor - window->bar_offset + get_left_bar_padding(window);
    cursor_y = window->region->height - 1;
  }
  else {
    set_cursor_color(ColorTypeEditorCursor);  // Todo: do this only when the mode changes.
    cursor_x = window->cursor_x - window->offset_x + get_left_padding(window);
    cursor_y = window->cursor_y - window->offset_y;
  }

  set_window_cursor(window, cursor_x, cursor_y);
  show_cursor();
  flush();
}

//--------------------------------------------------------------------------------------------------

static void resize_window(Window* window, int amount) {
  Region* parent = window->region->parent;

  if (!parent) return;

  int total = parent->stacked ? parent->height : parent->width;

  if (!window->region->stacked) {
    amount = amount * 2;
  }

  parent->split += (float)amount / total;

  resize_child_regions(parent);
}

//--------------------------------------------------------------------------------------------------

static void resize_child_regions(Region* region) {
  if (region->window) {
    region->window->region = region;
    region->window->redraw = true;
    return;
  }

  region->childs[0]->x = region->x;
  region->childs[0]->y = region->y;

  if (region->stacked) {
    int height = limit(region->height * region->split, MinimumWindowHeight, region->height - MinimumWindowHeight);

    region->split = (float)height / region->height;

    region->childs[0]->width = region->width;
    region->childs[1]->width = region->width;

    region->childs[0]->height = height;
    region->childs[1]->height = region->height - height;

    region->childs[1]->x = region->x;
    region->childs[1]->y = region->y + height;
  }
  else {
    int width = limit(region->width * region->split, MinimumWindowWidth, region->width - MinimumWindowWidth - 1);

    region->split = (float)width / region->width;

    region->childs[0]->height = region->height;
    region->childs[1]->height = region->height;

    region->childs[0]->width = width;
    region->childs[1]->width = region->width - width - 1; // Split line.

    region->childs[1]->x = region->x + width;
    region->childs[1]->y = region->y;
  }

  resize_child_regions(region->childs[0]);
  resize_child_regions(region->childs[1]);
}

//--------------------------------------------------------------------------------------------------

static Region* recurse_left(Region* region) {
  return region->childs[0] ? recurse_left(region->childs[0]) : region;
}

//--------------------------------------------------------------------------------------------------

static Region* recurse_right(Region* region) {
  return region->childs[1] ? recurse_right(region->childs[1]) : region;
}

//--------------------------------------------------------------------------------------------------

static Region* get_next_region(Region* region) {
  if (region->parent == 0) {
    return recurse_left(region);
  }
  else if (region->parent->childs[0] == region) {
    return recurse_left(region->parent->childs[1]);
  }
  else {
    return get_next_region(region->parent);
  }
}

//--------------------------------------------------------------------------------------------------

static Region* get_previous_region(Region* region) {
  if (region->parent == 0) {
    return recurse_right(region);
  }
  else if (region->parent->childs[1] == region) {
    return recurse_right(region->parent->childs[0]);
  }
  else {
    return get_previous_region(region->parent);
  }
}

//--------------------------------------------------------------------------------------------------

static void focus_next() {
  focused_window = get_next_region(focused_window->region)->window;
}

//--------------------------------------------------------------------------------------------------

static void focus_previous() {
  focused_window = get_previous_region(focused_window->region)->window;
}

//--------------------------------------------------------------------------------------------------

static void remove_window(Window* window) {
  if (!window->region->parent) return;

  focus_next();

  Region* parent = window->region->parent;
  Region* region = (parent->childs[0] == window->region) ? parent->childs[1] : parent->childs[0];

  parent->childs[0] = region->childs[0];
  parent->childs[1] = region->childs[1];

  if (parent->childs[0]) {
    parent->childs[0]->parent = parent;
    parent->childs[1]->parent = parent;
  }

  parent->window = region->window;

  resize_child_regions(parent);

  free(region);
  free(window->region);
  free_window(window);
}

//--------------------------------------------------------------------------------------------------

static void swap_windows(Window* window) {
  Region* parent = window->region->parent;

  if (!parent) return;

  Region* copy = parent->childs[0];
  parent->childs[0] = parent->childs[1];
  parent->childs[1] = copy;

  resize_child_regions(parent);
}

//--------------------------------------------------------------------------------------------------

static Window* split_window(Window* window, bool vertical) {
  Region* region = window->region;

  region->window = 0;

  region->childs[0] = calloc(1, sizeof(Region));
  region->childs[1] = calloc(1, sizeof(Region));

  region->childs[0]->window = window;
  region->childs[1]->window = allocate_window();

  region->childs[0]->parent = region;
  region->childs[1]->parent = region;

  region->split = 0.5;
  region->stacked = vertical;

  resize_child_regions(region);
  return region->childs[1]->window;
}

//--------------------------------------------------------------------------------------------------

static int char_lookup[256];
static int index_lookup[1024];

//--------------------------------------------------------------------------------------------------

static void make_search_lookup(char* data, int size) {
  for (int i = 0; i < 256; i++) {
    char_lookup[i] = size;
  }

  for (int i = 0; i < size; i++) {
    char_lookup[(int)data[i]] = size - i - 1;
  }

  for (int i = size - 1; i > 0; i--) {
    int shift = 0;
    for (int j = i - 1; j >= 0; j--) {
      if (!strncmp(data + j, data + i, size - i)) {
        if ((j && data[j] != data[i - 1]) || !shift) {
          shift = i - j;
        }
      }
    }

    index_lookup[i] = shift ? shift : 1;
  }
}

//--------------------------------------------------------------------------------------------------

static void search(char* word, char* data) {
  int word_size = strlen(word);
  int data_size = strlen(data);
  int data_index = word_size - 1;

  int count = 0;
  int indices[1024];

  make_search_lookup(word, word_size);

  while (data_index < data_size) {
    int tmp_data_index = data_index;
    int word_index = word_size - 1;
    int match_count = 0;

    while (word_index >= 0 && word[word_index] == data[data_index]) {
      word_index--;
      data_index--;
      match_count++;
    }

    if (word_index < 0) {
      indices[count++] = data_index + 1;
      data_index += word_size + 1;
    }
    else if (match_count) {
      data_index = tmp_data_index + index_lookup[match_count];
    }
    else {
      data_index = tmp_data_index + char_lookup[(int)data[data_index]];
    }
  }
}

//--------------------------------------------------------------------------------------------------

static void resize_master_region() {
  get_terminal_size(&master_region.width, &master_region.height);
  resize_child_regions(&master_region);
  render();
}

//--------------------------------------------------------------------------------------------------

static void terminal_deinit() {
  reset_terminal_colors();
  clear_terminal();
  flush();
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

  signal(SIGWINCH, (void* )resize_master_region);
}

//--------------------------------------------------------------------------------------------------

static void editor_init() {
  char_array_init(&framebuffer, 16 * 1024);
  window_array_init(&windows, 16);
  file_array_init(&files, 16);

  master_region.window = allocate_window();
  focused_window = master_region.window;

  resize_master_region();

  set_cursor_color(ColorTypeEditorCursor);
  set_terminal_background_color(ColorTypeEditorBackground);
  clear_terminal();
  flush();
}

//--------------------------------------------------------------------------------------------------

int main() {
  terminal_init();
  editor_init();

  char path[] = "test/test.txt";
  File* file = open_file(path, sizeof(path) - 1);
  master_region.window->file = file;
  Window* window = split_window(master_region.window, false);
  window->file = file;

  while (running) {
    render();
    update();
  }

  return 0;
}
