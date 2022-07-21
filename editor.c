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

// Margin must be less than the minimum width and height.

#define SpacesPerTab               2
#define WindowCursorMarginTop      6
#define WindowCursorMarginBottom   6
#define WindowCursorMarginLeft     6
#define WindowCursorMarginRight    6
#define BarCursorMarginLeft        5
#define BarCursorMarginRight       5
#define LinenumberMargin           2
#define StatusBarCount             1
#define ErrorShowTime              1500
#define MinimumWindowWidth         40
#define MinimumWindowHeight        10

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
  KeyCodeTab            = 9,
  KeyCodeEnter          = 10,
  KeyCodeEscape         = 27,
  KeyCodeDelete         = 127,
  KeyCodeCapsDelete     = 8,

  KeyCodeCtrlC          = 3,
  KeyCodeCtrlG          = 7,
  KeyCodeCtrlN          = 14,
  KeyCodeCtrlQ          = 17,
  KeyCodeCtrlS          = 19,
  KeyCodeCtrlX          = 24,

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
  UserKeyFocusNext     = KeyCodeShiftRight,
  UserKeyFocusPrevious = KeyCodeShiftLeft,
  UserKeyPageUp        = KeyCodeShiftUp,
  UserKeyPageDown      = KeyCodeShiftDown,
  UserKeyExit          = KeyCodeCtrlQ,
  UserKeyOpen          = KeyCodeCtrlG,
  UserKeyNew           = KeyCodeCtrlN,
  UserKeySave          = KeyCodeCtrlS,
  UserKeyCommand       = KeyCodeCtrlC,
};

//--------------------------------------------------------------------------------------------------

struct Line {
  CharArray chars;
  CharArray colors;
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

struct Region {
  int width;
  int height;
  int position_x;
  int position_y;

  float split_point;

  Region* parent;
  Region* childs[2];

  bool stacked;
  Window* window;
};

//--------------------------------------------------------------------------------------------------

struct Window {
  File* file;
  Region* region;

  int width;
  int height;
  int position_x;
  int position_y;

  int offset_x;
  int offset_y;
  int cursor_x;
  int cursor_y;
  int ideal_cursor_x;

  bool redraw;

  int bar_type;
  int bar_cursor;
  int bar_offset;
  bool bar_focused;
  CharArray bar_chars;

  Time error_time;
  bool error_active;
  CharArray error_message;
};

//--------------------------------------------------------------------------------------------------

static Termios saved_terminal;
static CharArray framebuffer;
static WindowArray windows;
static FileArray files;

static Region initial_region;

static char* bar_message[BarTypeCount] = {
  [BarTypeOpen]    = " open: ",
  [BarTypeNew]     = " new: ",
  [BarTypeCommand] = " command: "
};

static bool redraw_line[1024];

static bool running = true;
static int focused_window;
static int editor_width;
static int editor_height;

static int previous_keycode;

//--------------------------------------------------------------------------------------------------

static void delete_line(Line* line);
static Line* append_line(File* file);
static void append_chars(Line* line, char* data, int size);
static void split_window(Window* window, bool vertical);
static void child_resized(Region* region);
static void resize_child_regions(Region* region);
static void resize(Region* region, int amount);
static void remove_window(Window* window);
static void swap_windows(Window* window);
static void focus_next();
static void render_line(Line* line);
static void focus_previous();

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
  set_cursor(window->position_x + x, window->position_y + y);
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

static void clear_formatting() {
  print("\x1b[0m");
}

//--------------------------------------------------------------------------------------------------

static int get_input() {
  char keys[64];
  int size = read(STDIN_FILENO, keys, sizeof(keys));
  if (!size) return KeyCodeNone;

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

  char_array_append_multiple(&file->path, path, path_size);
  file->dirty = true;

  return file;
}

//--------------------------------------------------------------------------------------------------

static Window* new_window(File* file) {
  Window* window = calloc(1, sizeof(Window));
  window->redraw = true;
  window->file = file;
  window_array_append(&windows, window);
  return window;
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
      debug_print("Path exist\n");
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
  LineArray* lines = &file->lines;

  int fd = open(file->path.items, O_WRONLY | O_CREAT | O_TRUNC, 0666); // Do we want r/w for all users?
  if (fd < 0) return false;

  for (int i = 0; i < lines->count; i++) {
    if (i) write(fd, "\r\n", 2);

    Line* line = lines->items[i];
    write(fd, line->chars.items, line->chars.count);
  }

  close(fd);

  return false;

  file->unsaved = false;
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

static Line* insert_line(File* file, int offset) {
  Line* line = calloc(1, sizeof(Line));
  line_array_insert(&file->lines, line, offset);
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
  render_line(line);
}

//--------------------------------------------------------------------------------------------------

static void append_char(Line* line, char c) {
  char_array_append(&line->chars, c);
  line->dirty = true;
  render_line(line);
}

//--------------------------------------------------------------------------------------------------

static void delete_char(Line* line, int index) {
  char_array_remove(&line->chars, index);
  line->dirty = true;
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
  if (window->file == 0) return LinenumberMargin + 1; 
  return count_digits(window->file->lines.count) + LinenumberMargin;
}

//--------------------------------------------------------------------------------------------------

static int get_left_bar_padding(Window* window) {
  return strlen(bar_message[window->bar_type]);
}

//--------------------------------------------------------------------------------------------------

static void get_active_size(Window* window, int* width, int* height) {
  int x = window->position_x ? 2 : 0;
  *width  = window->width - get_left_padding(window) - x;
  *height = window->height - StatusBarCount;
}

//--------------------------------------------------------------------------------------------------

static int get_visible_line_count(Window* window) {
  if (!window->file) return 0;

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
  if (!window->file) return;

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
  window->ideal_cursor_x = x;
  limit_window_cursor(window);
}

//--------------------------------------------------------------------------------------------------

static void update_window_cursor_y(Window* window, int y) {
  window->cursor_y = y;
  window->cursor_x = window->ideal_cursor_x;
  limit_window_cursor(window);
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

static void file_insert_chars(Window* window, char* data, int size) {
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
  if (size == 1 && data[0] == '\n') {
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

  debug_print("Line: %d\n", buffer.count);
  append_chars(line, buffer.items, buffer.count);
}

//--------------------------------------------------------------------------------------------------

static void file_insert_char(Window* window, char c) {
  file_insert_chars(window, &c, 1);
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

    window->file->dirty = true;
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

static void editor_handle_keypress(Window* window, int keycode) {
  if (window->file && KeyCodePrintableStart <= keycode && keycode <= KeyCodePrintableEnd) {
    file_insert_char(window, keycode);
    window->file->unsaved = true;
  }
  else switch (keycode) {
    case KeyCodeUp:
      update_window_cursor_y(window, window->cursor_y - 1);
      break;

    case KeyCodeDown:
      update_window_cursor_y(window, window->cursor_y + 1);
      break;

    case UserKeyPageUp:
      update_window_cursor_y(window, window->cursor_y - window->height / 2);
      break;

    case UserKeyPageDown:
      update_window_cursor_y(window, window->cursor_y + window->height / 2);
      break;

    case KeyCodeShiftHome:
      update_window_cursor_x(window, 0);
      update_window_cursor_y(window, 0);
      break;

    case KeyCodeShiftEnd:
      if (!window->file) break;
      update_window_cursor_x(window, window->file->lines.items[window->file->lines.count - 1]->chars.count);
      update_window_cursor_y(window, window->file->lines.count - 1);
      break;

    case KeyCodeLeft:
      update_window_cursor_x(window, window->cursor_x - 1);
      break;

    case KeyCodeRight:
      update_window_cursor_x(window, window->cursor_x + 1);
      break;

    case KeyCodeHome:
      update_window_cursor_x(window, 0);
      break;

    case KeyCodeEnd:
      if (!window->file) break;
      update_window_cursor_x(window, window->file->lines.items[window->cursor_y]->chars.count);
      break;

    case KeyCodeCtrlUp:
      resize(window->region, 1);
      break;

    case KeyCodeCtrlDown:
      resize(window->region, -1);
      break;

    case KeyCodeCtrlLeft:
      remove_window(window);
      break;

    case KeyCodeCtrlRight:
      swap_windows(window);
      break;
    
    case KeyCodeCapsDelete:
    case KeyCodeDelete: {
      if (!window->file) break;
      window->file->unsaved = true;
      int delete_count = 1;
      int space_count = get_leading_spaces(window->file->lines.items[window->cursor_y]);

      if (window->cursor_x <= space_count) {
        space_count = min(space_count, window->cursor_x);
        if (space_count % SpacesPerTab == 0 && space_count) {
          delete_count++;
        }
      }

      while (delete_count--) {
        file_delete_char(window);
      }
      break;
    }

    case KeyCodeTab:
      if (!window->file) break;
      window->file->unsaved = true;
      for (int i = 0; i < SpacesPerTab; i++) {
        file_insert_char(window, ' ');
      }
      break;

    case KeyCodeEnter:
      if (!window->file) break;
      window->file->unsaved = true;
      file_insert_char(window, '\n');
      break;
    
    case UserKeyFocusNext:
      //if (++focused_window == windows.count) focused_window = 0;
      focus_next();
      break;

    case UserKeyFocusPrevious:
      focus_previous();
      //if (--focused_window < 0) focused_window = windows.count - 1;
      break;

    case UserKeyOpen:
      enter_bar_mode(window, BarTypeOpen);
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
    
    case UserKeySave:
      if (!window->file) break;
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
  window->ideal_cursor_x = 0;
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

static void vsplit(Window* window) {
  int new_height = window->height / 2;

  debug_print("height: %d new height: %d left: %d\n", window->height, new_height, window->height - new_height);

  if ((window->height - new_height) < MinimumWindowHeight) {
    error(window, "can't resize window");
    return;
  }

  window->height -= new_height;

  Window* new = calloc(1, sizeof(Window));

  new->width = window->width;
  new->height = new_height;
  new->position_x = window->position_x;
  new->position_y = window->position_y + window->height;
  new->file = window->file;

  new->redraw = true;
  window->redraw = true;

  window_array_append(&windows, new);
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
      debug_print("splitting -\n");
    }
    else if (skip_char(&command, '|')) {
      debug_print("splitting |\n");
      split_window(window, false);
    }
    else {
      error(window, "cant split");
    }
  }
  else if (skip_keyword(&command, "hsplit")) {
  }
  else if (skip_keyword(&command, "adjust")) {
    int adjust = 0;
    char direction = '-';

    if (!read_number(&command, &adjust)) {
      error(window, "missing value");
    }

    if (skip_char(&command, '<')) {
      direction = '<';
    }
    else if (skip_char(&command, '>')) {
      direction = '>';
    }
    else {
      error(window, "give a direction < >");
    }

    debug_print("Adjusting %i to %c\n", adjust, direction);
  }
  else if (skip_keyword(&command, "close")) {
    
  }
  else {
    error(window, "unknow command `%.*s`", window->bar_chars.count, window->bar_chars.items);
  }
}

//--------------------------------------------------------------------------------------------------

static void bar_handle_enter(Window* window) {
  debug_print("Got command: %.*s\n", window->bar_chars.count, window->bar_chars.items);

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
    
    case KeyCodeCapsDelete:
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
      Window* window = windows.items[focused_window];

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
  bool window_focus = window == windows.items[focused_window];
  int width = window->width;

  set_window_cursor(window, 0, window->height - 1);

  if (window->error_active || window->bar_focused) {
    if (window->error_active) {
      print("\x1b[31m"); // Red.

      width -= print(" error: ");
      width -= print("%.*s", min(window->error_message.count, width), window->error_message.items);  

      clear_formatting();
    }
    else if (window->bar_focused) {
      width -= print("%s", bar_message[window->bar_type]) - 1;
      width -= 1;

      window->bar_offset = get_updated_offset(window->bar_cursor, window->bar_offset, width, BarCursorMarginLeft, BarCursorMarginRight);
      int bar_size = min(window->bar_chars.count - window->bar_offset, width);

      width -= print("%.*s", bar_size, &window->bar_chars.items[window->bar_offset]);
    }
  }
  else {
    int size = 0;
    int percent;
    if (window->file) {
      percent = 100 * window->cursor_y / window->file->lines.count;
      size = window->file->path.count + 1 + count_digits(percent) + sizeof((char)'%') + 1;
    }

    invert();

    for (int i = 0; i < width - size; i++) {
      print(" ");
    }

    if (window->file) {
      print("%.*s %d%% ", window->file->path.count, window->file->path.items, percent);
    }
    clear_formatting();
  }
}

//--------------------------------------------------------------------------------------------------

static void render_window(Window* window) {
  int width, height;
  get_active_size(window, &width, &height);

  int number_width = window->file ? count_digits(window->file->lines.count) : 1;

  int color = 0;

  for (int j = 0; j < get_visible_line_count(window); j++) {
    if (!redraw_line[window->position_y + j]) continue;

    Line* line = window->file->lines.items[window->offset_y + j];

    set_window_cursor(window, 0, j);

    if (window->position_x) {
      invert();
      print(" ");
      clear_formatting();
      print(" ");
    }

    print("%*d", number_width, window->offset_y + j);
    print("%*c", LinenumberMargin, ' ');

    int size = max(min(line->chars.count - window->offset_x, width), 0);

    for (int i = 0; i < size; i++) {
      int index = window->offset_x + i;

      if (line->colors.items[index] != color) {
        color = line->colors.items[index];
        print("\x1b[%dm", color);
      }

      print("%c", line->chars.items[index]);
    }

    if (color) {
      clear_formatting();
    }
  }

  for (int j = get_visible_line_count(window); j < height; j++) {
    if (!redraw_line[window->position_y + j]) continue;
    set_window_cursor(window, 0, j);
    if (window->position_x) {
      invert();
      print(" ");
      clear_formatting();
      print(" ");
    }
  }

  // 

  render_status_bar(window);
}

//--------------------------------------------------------------------------------------------------

static void mark_lines_for_redraw() {
  memset(redraw_line, 0, editor_height);

  for (int i = 0; i < windows.count; i++) {
    Window* window = windows.items[i];

    debug_print("Window %d: %p %p\n", i, window->file, window);

    // Redraw entire occupied region if either the window is moved or the file is dirty.
    if (window->file == 0 || window->file->dirty || window->redraw) {
      window->redraw = false;

      for (int j = 0; j < window->height; j++) {
        redraw_line[window->position_y + j] = true;
      }
    }

    // Redraw dirty visible lines.
    if (window->file) {
      for (int j = 0; j < get_visible_line_count(window); j++) {
        if (window->file->lines.items[window->offset_y + j]->dirty) {
          redraw_line[window->position_y + j] = true;
        }
      }
    }

    // Redraw status bars.
    for (int j = 0; j < StatusBarCount; j++) {
      redraw_line[window->position_y + window->height - j - 1] = true;
    }
  }

  // Files are shared and must be marked clean after all checks are done.
  for (int i = 0; i < windows.count; i++) {
    Window* window = windows.items[i];

    if (window->file) {
      window->file->dirty = false;

      for (int j = 0; j < get_visible_line_count(window); j++) {
        window->file->lines.items[window->offset_y + j]->dirty = false;
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

  for (int i = 0; i < editor_height; i++) {
    if (redraw_line[i]) {
      clear_line(i);
    }
  }

  hide_cursor();
  
  for (int i = 0; i < windows.count; i++) {
    render_window(windows.items[i]);
  }

  Window* window = windows.items[focused_window];

  int cursor_x;
  int cursor_y;

  if (window->bar_focused) {
    cursor_x = window->bar_cursor - window->bar_offset + get_left_bar_padding(window);
    cursor_y = window->height - 1;
    debug_print("cursor: %d\n", cursor_x);
  }
  else {
    cursor_x = window->cursor_x - window->offset_x + get_left_padding(window);
    cursor_y = window->cursor_y - window->offset_y;
  }

  set_window_cursor(window, cursor_x, cursor_y);
  show_cursor();
  flush();
}

//--------------------------------------------------------------------------------------------------

static void window_resize_handler(int signal) {
  Window* window = windows.items[focused_window];

  debug_print("Resize\n");

  for (int i = 0; i < windows.count; i++) {
    windows.items[i]->redraw = true;
  }

  int width, height;
  get_terminal_size(&width, &height);

  window->width = width;
  window->height = height;

  editor_width = width;
  editor_height = height;

  render();
}

//--------------------------------------------------------------------------------------------------

static void update_region_window(Region* region) {
  region->window->position_x = region->position_x;
  region->window->position_y = region->position_y;
  region->window->width = region->width;
  region->window->height = region->height;
  region->window->region = region;
  region->window->redraw = true;
}

//--------------------------------------------------------------------------------------------------

static void resize(Region* region, int amount) {
  //print_regions();

  Region* parent = region->parent;
  assert(parent);

  if (!region->stacked) amount *= 2;

  int total = parent->stacked ? parent->height : parent->width;
  float increment = (float)amount / total;

  if (parent->stacked) {
    int new_height = parent->childs[0]->height + amount;
    
    if (MinimumWindowHeight <= new_height && new_height <= total - MinimumWindowHeight) {
      parent->split_point += increment;
    }
  }
  else {
    int new_width = parent->childs[0]->width + amount;
    
    if (MinimumWindowWidth <= new_width && new_width <= total - MinimumWindowWidth - 1) {
      parent->split_point += increment;
    }
  }

  debug_print("%f\n", parent->split_point);
  resize_child_regions(parent);
}

//--------------------------------------------------------------------------------------------------

static void resize_child_regions(Region* region) {
  if (region->window) {
    update_region_window(region);
    return;
  }

  assert(region->childs[0]);
  assert(region->childs[1]);

  region->childs[0]->position_x = region->position_x;
  region->childs[0]->position_y = region->position_y;

  if (region->stacked) {

    int height = limit(region->height * region->split_point, MinimumWindowHeight, region->height - MinimumWindowHeight);

    region->childs[0]->width = region->width;
    region->childs[1]->width = region->width;

    debug_print("Child0: %d -> %d\n", region->childs[0]->height, height);
    debug_print("Child1: %d -> %d\n", region->childs[1]->height, region->height - height);

    region->childs[0]->height = height;
    region->childs[1]->height = region->height - height;

    region->childs[1]->position_x = region->position_x;
    region->childs[1]->position_y = region->position_y + height;
  }
  else {
    int width = limit(region->width * region->split_point, MinimumWindowWidth, region->width - MinimumWindowWidth - 1);

    region->childs[0]->height = region->height;
    region->childs[1]->height = region->height;

    region->childs[0]->width = width;
    region->childs[1]->width = region->width - width - 1; // Split line.

    region->childs[1]->position_x = region->position_x + width;
    region->childs[1]->position_y = region->position_y;
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
  Window* window = windows.items[focused_window];

  Region* region = get_next_region(window->region);
  window = region->window;

  debug_print("Window: %p\n", window);
  debug_print("Window: %p\n", region->childs[0]);
  debug_print("Window: %p\n", region->childs[1]);

  for (int i = 0; i < windows.count; i++) {
    if (window == windows.items[i]) {
      focused_window = i;
      return;
    }
  }

  assert(0);
}

static void focus_previous() {
  Window* window = windows.items[focused_window];

  Region* region = get_previous_region(window->region);
  window = region->window;

  debug_print("Window: %p\n", window);
  debug_print("Window: %p\n", region->childs[0]);
  debug_print("Window: %p\n", region->childs[1]);

  for (int i = 0; i < windows.count; i++) {
    if (window == windows.items[i]) {
      focused_window = i;
      return;
    }
  }

  assert(0);
}

//--------------------------------------------------------------------------------------------------

static void remove_window(Window* window) {
  Region* delete_this = window->region;

  if (delete_this == &initial_region) return;

  Region* parent = delete_this->parent;

  focus_next();

  assert(parent->childs[0]);
  assert(parent->childs[1]);
  assert(!parent->window);

  Region* keep_this = parent->childs[0] == delete_this ? parent->childs[1] : parent->childs[0];

  parent->childs[0] = keep_this->childs[0];
  parent->childs[1] = keep_this->childs[1];
  parent->window = keep_this->window;

  if (!parent->window) {
    parent->childs[0]->parent = parent;
    parent->childs[1]->parent = parent;
  }



  resize_child_regions(parent);

  // Todo!
  //free(delete_this);
  //free(window);
}

//--------------------------------------------------------------------------------------------------

static void swap_windows(Window* window) {
  Region* region = window->region;

  if (region == &initial_region) return;

  Region* parent = region->parent;

  Region* tmp = parent->childs[0];
  parent->childs[0] = parent->childs[1];
  parent->childs[1] = tmp;

  resize_child_regions(parent);
}

//--------------------------------------------------------------------------------------------------

static void split_window(Window* window, bool vertical) {
  Region* region = window->region;

  region->window = 0;

  region->childs[0] = calloc(1, sizeof(Region));
  region->childs[1] = calloc(1, sizeof(Region));

  region->childs[0]->window = window;
  region->childs[1]->window = new_window(window->file);

  region->childs[0]->parent = region;
  region->childs[1]->parent = region;

  region->split_point = 0.5;
  region->stacked = vertical;

  resize_child_regions(region);
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

  char test_file[] = "test/test.txt";
  File* file = open_file(test_file, sizeof(test_file) - 1);

  get_terminal_size(&initial_region.width, &initial_region.height);
  initial_region.window = new_window(file);
  update_region_window(&initial_region);
}

//--------------------------------------------------------------------------------------------------

int main() {
  terminal_init();
  editor_init();

  
  int width, height;
  get_terminal_size(&width, &height);
  editor_width = width;
  editor_height = height;

  /*

  int half = editor_width / 2;

  char test_file[] = "test/test.txt";
  Window* window = calloc(1, sizeof(Window));
  window->file = open_file(test_file, sizeof(test_file) - 1);
  window->redraw = true;
  window->width = half;
  window->height = height;
  window_array_append(&windows, window);
  focused_window = 0;

  Window* window1 = calloc(1, sizeof(Window));
  window1->file = window->file;
  window1->redraw = true;
  window1->width = editor_width - half;
  window1->position_x = half;
  window1->height = height;
  window_array_append(&windows, window1);
  focused_window = 0;

  window->file = 0;
  */

  while (running) {
    render();
    update();
  }

  debug_print("Quit\n");

  return 0;
}
