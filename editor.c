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

enum {
  WindowMinimumWidth       = 40,
  WindowMinimumHeight      = 10,

  MaxActionCount           = 1024,
  MaxKeywordSize           = 32,
  MaxFindLength            = 1024,
  MaxLineLength            = 1024, // Has to do with the find.

  MinibarMaxPathWidth      = 20,
  MinibarCommandPadding    = 1,
  MinibarLeftPadding       = 1,
  MinibarRightPadding      = 1,
  MinibarLeftCursorMargin  = 5,
  MinibarRightCursorMargin = 5,
  MinibarCount             = 1,

  EditorLineNumberMargin   = 2,
  EditorSpacesPerTab       = 2,
  EditorCursorMarginTop    = 6,
  EditorCursorMarginBottom = 6,
  EditorCursorMarginLeft   = 6,
  EditorCursorMarginRight  = 6,
};

//--------------------------------------------------------------------------------------------------

enum {
  ColorTypeEditorCursor,
  ColorTypeEditorForeground,
  ColorTypeEditorBackground,
  ColorTypeMinibarCursor,
  ColorTypeMinibarForeground,
  ColorTypeMinibarBackground,
  ColorTypeMinibarError,
  ColorTypeSelectedMatchForeground,
  ColorTypeSelectedMatchBackground,
  ColorTypeMatchForeground,
  ColorTypeMatchBackground,
  ColorTypeComment,
  ColorTypeMultilineComment,
  ColorTypeKeyword,
  ColorTypeString,
  ColorTypeChar,
  ColorTypeNumber,
  ColorTypeCount,
};

//--------------------------------------------------------------------------------------------------

enum {
  ColorThemeDefault,
  ColorThemeLight,
  ColorThemeJonBlow,
  ColorThemeCount,
};

//--------------------------------------------------------------------------------------------------

static const struct { const char* name; int colors[ColorTypeCount]; } themes[ColorThemeCount] = {
  [ColorThemeDefault] = {
    .name = "default",
    .colors = {
      [ColorTypeEditorCursor]            = 0x000000,
      [ColorTypeEditorForeground]        = 0x000000,
      [ColorTypeEditorBackground]        = 0xffffff,
      [ColorTypeMinibarCursor]           = 0x082626,
      [ColorTypeMinibarForeground]       = 0x082626,
      [ColorTypeMinibarBackground]       = 0xd6b58d,
      [ColorTypeMinibarError]            = 0xff0000,
      [ColorTypeSelectedMatchForeground] = 0x082626,
      [ColorTypeSelectedMatchBackground] = 0xd1b897,
      [ColorTypeMatchForeground]         = 0x082626,
      [ColorTypeMatchBackground]         = 0x0a3f4a,
      [ColorTypeComment]                 = 0x44b340,
      [ColorTypeMultilineComment]        = 0x00ff00,
      [ColorTypeKeyword]                 = 0x8cde94,
      [ColorTypeString]                  = 0xc1d1e3,
      [ColorTypeChar]                    = 0xff0000,
      [ColorTypeNumber]                  = 0xc1d1e3,
    },
  },

  [ColorThemeJonBlow] = {
    .name = "blow",
    .colors = {
      [ColorTypeEditorCursor]            = 0xd1b897,
      [ColorTypeEditorForeground]        = 0xd1b897,
      [ColorTypeEditorBackground]        = 0x082626,
      [ColorTypeMinibarCursor]           = 0x082626,
      [ColorTypeMinibarForeground]       = 0x082626,
      [ColorTypeMinibarBackground]       = 0xd6b58d,
      [ColorTypeMinibarError]            = 0xff0000,
      [ColorTypeSelectedMatchForeground] = 0x082626,
      [ColorTypeSelectedMatchBackground] = 0xd1b897,
      [ColorTypeMatchForeground]         = 0x082626,
      [ColorTypeMatchBackground]         = 0x0a3f4a,
      [ColorTypeComment]                 = 0x44b340,
      [ColorTypeMultilineComment]        = 0x00ff00,
      [ColorTypeKeyword]                 = 0x8cde94,
      [ColorTypeString]                  = 0xc1d1e3,
      [ColorTypeChar]                    = 0xff0000,
      [ColorTypeNumber]                  = 0xc1d1e3,
    },
  },
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
  KeyCodeCtrlF          = 6,

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
  UserKeyFocusNext     = KeyCodeShiftRight,
  UserKeyFocusPrevious = KeyCodeShiftLeft,
  UserKeyPageUp        = KeyCodeShiftUp,
  UserKeyPageDown      = KeyCodeShiftDown,
  UserKeyExit          = KeyCodeCtrlQ,
  UserKeyOpen          = KeyCodeCtrlG,
  UserKeyNew           = KeyCodeCtrlN,
  UserKeySave          = KeyCodeCtrlS,
  UserKeyCommand       = KeyCodeCtrlR,
  UserKeyMark          = KeyCodeCtrlB,
  UserKeyCopy          = KeyCodeCtrlC,
  UserKeyPaste         = KeyCodeCtrlV,
  UserKeyCut           = KeyCodeCtrlX,
};

//--------------------------------------------------------------------------------------------------

enum {
  MinibarModeOpen,
  MinibarModeNew,
  MinibarModeCommand,
  MinibarModeFind,
  MinibarModeCount,
};

//--------------------------------------------------------------------------------------------------

static const char* const bar_message[MinibarModeCount] = {
  [MinibarModeOpen]    = "open: ",
  [MinibarModeNew]     = "new: ",
  [MinibarModeCommand] = "command: ",
  [MinibarModeFind]    = "find: ",
};

//--------------------------------------------------------------------------------------------------

#define min(x, y) (((int)(x) < (int)(y)) ? x : y)
#define max(x, y) (((int)(x) < (int)(y)) ? y : x)
#define limit(x, lower, upper) max(min(x, upper), lower)

//--------------------------------------------------------------------------------------------------

typedef struct termios Termios;
typedef struct Line Line;
typedef struct File File;
typedef struct Window Window;
typedef struct Region Region;
typedef struct Action Action;
typedef struct Highlight Highlight;
typedef struct FileState FileState;
typedef struct Match Match;
typedef struct Undo Undo;

//--------------------------------------------------------------------------------------------------

// C must know the size fore the array stuff.
struct Match {
  int x;
  int y; 
};

//--------------------------------------------------------------------------------------------------

define_array(string, String, char);
define_array(line_array, LineArray, Line* );
define_array(file_array, FileArray, File* );
define_array(window_array, WindowArray, Window* );
define_array(match_array, MatchArray, Match);
define_array(file_state_array, FileStateArray, FileState* );
define_array(int_array, IntArray, int);

//--------------------------------------------------------------------------------------------------

struct Line {
  String chars;
  IntArray colors;

  bool redraw;
};

//--------------------------------------------------------------------------------------------------

struct Highlight {
  char* extensions[16];
  char** keywords[MaxKeywordSize];

  char single_line_comment_start[3];
  char multiline_comment_start[3];
  char multiline_comment_end[3];

  bool comments;
  bool multiline_comments;
  bool strings;
  bool chars;
  bool numbers;
};

//--------------------------------------------------------------------------------------------------

enum {
  LanguageC,
  LanguageCount,
};

//--------------------------------------------------------------------------------------------------

static char* c_keywords_2[] = {"if", 0};
static char* c_keywords_3[] = {"int", "for", 0};
static char* c_keywords_4[] = {"case", "else", "true", "char", "void", "bool", 0};
static char* c_keywords_5[] = {"float", "break", "false", "while", 0};
static char* c_keywords_6[] = {"static", "struct", "return", "#endif", 0};
static char* c_keywords_7[] = {"#define", "#ifndef", 0};
static char* c_keywords_8[] = {"#include", 0};

//--------------------------------------------------------------------------------------------------

static Highlight highlights[LanguageCount] = {
  [LanguageC] = {
    .extensions = {".c", 0},

    .keywords = {
      [2] = c_keywords_2,
      [3] = c_keywords_3,
      [4] = c_keywords_4,
      [5] = c_keywords_5,
      [6] = c_keywords_6,
      [7] = c_keywords_7,
      [8] = c_keywords_8,
    },

    .single_line_comment_start = "//",
    .multiline_comment_start = "/*",
    .multiline_comment_end = "*/",

    .comments           = true,
    .multiline_comments = true,
    .strings            = true,
    .chars              = true,
    .numbers            = true,
  },
};

//--------------------------------------------------------------------------------------------------

struct Action {
  unsigned char type;
  unsigned char flags;
  char* data;
  int size;
  int x;
  int y;
};

//--------------------------------------------------------------------------------------------------

struct Undo {
  Action actions[MaxActionCount];
  int index;
  int head;
  int tail;

  String buffer;
  int x;
  int y;
  bool delete;
};

//--------------------------------------------------------------------------------------------------

struct File {
  String path;
  LineArray lines;

  bool redraw;
  bool saved;

  Highlight* highlight;
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

struct FileState {
  File* file;

  int cursor_x;
  int cursor_y;
  int cursor_x_ideal;

  int offset_x;
  int offset_y;
  
  int mark_x;
  int mark_y;
  bool mark_valid;

  int previous_keycode;
};

//--------------------------------------------------------------------------------------------------

struct Window {
  File* file;
  Region* region;

  FileStateArray file_states;

  bool redraw;

  int offset_x;
  int offset_y;

  int cursor_x;
  int cursor_y;
  int cursor_x_ideal;

  bool mark_valid;
  int mark_x;
  int mark_y;

  int minibar_mode;
  int minibar_cursor;
  int minibar_offset;
  bool minibar_active;
  String minibar_data;

  bool error_present;
  String error_message;

  MatchArray matches;
  int match_index;
  int match_length;

  int saved_cursor_x;
  int saved_cursor_y;

  int previous_keycode;
};

//--------------------------------------------------------------------------------------------------

static Termios saved_terminal;
static String framebuffer;
static Window* focused_window;
static Region master_region;
static WindowArray windows;
static FileArray files;
static String clipboard;
static String buffer;

static int find_char_lookup[256];
static int find_index_lookup[MaxFindLength];
static int find_indicies[MaxLineLength];

static bool redraw_line[1024];
static bool running = true;
static int current_theme = ColorThemeJonBlow;

int current_foreground_type = -1;
int current_background_type = -1;

// Temporary undo state.
int undo_type;
int undo_start_x;
int undo_start_y;
int undo_current_x;
int undo_current_y;
static String undo_buffer;

//--------------------------------------------------------------------------------------------------

static void delete_line(File* file, int index);
static Window* split_window(Window* window, bool vertical);
static void child_resized(Region* region);
static void resize_child_regions(Region* region);
static void resize_window(Window* window, int amount);
static void remove_window(Window* window);
static void swap_windows(Window* window);
static void focus_next();
static void focus_previous();
static void compile_and_parse_issues();
static void make_find_lookup(char* data, int size);
static int find(char* word, int word_length, char* data, int data_length);
static void render_line(File* file, Line* line);
static int get_delete_count(Window* window, char* data, int cursor, bool ctrl);
static void change_file(Window* window, File* file);

//--------------------------------------------------------------------------------------------------

static void debug(const char* data, ...) {
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
  string_extend(&framebuffer, framebuffer.count + 1024);

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

static void update_terminal_background() {
  int color = themes[current_theme].colors[ColorTypeEditorBackground];
  print("\x1b]11;rgb:%02x/%02x/%02x\x7", (color >> 16) & 0xFF, (color >> 8) & 0xFF, color & 0xFF);
}

//--------------------------------------------------------------------------------------------------

static void set_cursor_color(int type) {
  int color = themes[current_theme].colors[type];
  print("\x1b]12;rgb:%02x/%02x/%02x\x7", (color >> 16) & 0xFF, (color >> 8) & 0xFF, color & 0xFF);
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
  if (type == current_background_type) return;
  int color = themes[current_theme].colors[type];
  current_background_type = color;
  print("\x1b[48;2;%d;%d;%dm", (color >> 16) & 0xFF, (color >> 8) & 0xFF, color & 0xFF);
}

//--------------------------------------------------------------------------------------------------

static void set_foreground_color(int type) {
  if (type == current_foreground_type) return;
  int color = themes[current_theme].colors[type];
  current_foreground_type = type;
  print("\x1b[38;2;%d;%d;%dm", (color >> 16) & 0xFF, (color >> 8) & 0xFF, color & 0xFF);
}

//--------------------------------------------------------------------------------------------------

static void bold() {
  print("\x1b[1m");
}

//--------------------------------------------------------------------------------------------------

static void clear_formatting() {
  current_background_type = -1;
  current_foreground_type = -1;
  print("\x1b[0m");
}

//--------------------------------------------------------------------------------------------------

static bool input_is_pending() {
  fd_set set;
  FD_ZERO(&set);
  FD_SET(STDIN_FILENO, &set);

  struct timeval timeout;
  timeout.tv_sec = 0;
  timeout.tv_usec = 0;

  return select(1, &set, 0, 0, &timeout) == 1;
}

//--------------------------------------------------------------------------------------------------

static int get_input() {
  char keys[64];
  int size = read(STDIN_FILENO, keys, sizeof(keys));
  if (!size) return KeyCodeNone;

  if (0) {
    for (int i = 0; i < size; i++) {
      debug("%d, ", keys[i]);
    }
    debug("\n");
  }

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

static bool git_commit(const char* message, int size) {
  char data[256];
  snprintf(data, 256, "git add . && git commit -m \"%.*s\" > /dev/null", size, message);
  return system(data) == 0; 
}

//--------------------------------------------------------------------------------------------------

static void display_error(Window* window, char* message, ...) {
  string_clear(&window->error_message);
  string_extend(&window->error_message, 1024);

  va_list arguments;
  va_start(arguments, message);
  int size = vsnprintf(window->error_message.items, window->error_message.capacity, message, arguments);
  va_end(arguments);

  window->error_message.count = size;
  window->error_present = true;
}

//--------------------------------------------------------------------------------------------------

static void add_null_termination(String* array) {
  string_append(array, 0);
  array->count--;
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
      break;
    }
  }

  free(window);
}

//--------------------------------------------------------------------------------------------------

static File* allocate_file(char* path, int path_size) {
  File* file = calloc(1, sizeof(File));
  file_array_append(&files, file);

  string_append_multiple(&file->path, path, path_size);
  add_null_termination(&file->path);

  // Load the correct highlighting to the file.
  for (int i = 0; i < LanguageCount; i++) {
    bool match = false;
    for (char** ext = highlights[i].extensions; *ext; ext++) {
      int ext_size = strlen(*ext);
      if (path_size > ext_size && !memcmp(path + path_size - ext_size, *ext, ext_size)) {
        match = true;
        break;
      }
    }

    if (match) {
      file->highlight = &highlights[i];
    }
  }

  file->saved = true;
  file->redraw = true;

  return file;
}

//--------------------------------------------------------------------------------------------------

static void delete_file(File* file) {
  while (file->lines.count) {
    delete_line(file, 0);
  }

 line_array_delete(&file->lines);
  free(file);
}

//--------------------------------------------------------------------------------------------------

static void insert_lines(File* file, int index, int count) {
 line_array_allocate_insert_multiple(&file->lines, count, index);

  for (int i = 0; i < count; i++) {
    file->lines.items[index + i] = calloc(1, sizeof(Line));
  }
  
  file->redraw = true;
}

//--------------------------------------------------------------------------------------------------

static Line* insert_line(File* file, int index) {
  insert_lines(file, index, 1);
  return file->lines.items[index];
}

//--------------------------------------------------------------------------------------------------

static void delete_lines(File* file, int index, int count) {
  for (int i = 0; i < count; i++) {
    Line* line = file->lines.items[index + i];
    string_delete(&line->chars);
    free(line);
  }

 line_array_remove_multiple(&file->lines, index, count);
  file->redraw = true;
}

//--------------------------------------------------------------------------------------------------

static void delete_line(File* file, int index) {
  delete_lines(file, index, 1);
}

//--------------------------------------------------------------------------------------------------

static File* try_open_existing_file(char* path, int path_size) {
  for (int i = 0; i < files.count; i++) {
    if (!strncmp(files.items[i]->path.items, path, path_size)) {
      return files.items[i];
    }
  }
  return 0;
}

//--------------------------------------------------------------------------------------------------

static File* open_file(char* path, int path_size) {
  File* file = try_open_existing_file(path, path_size);
  if (file) return file;

  // Add null termination.
  assert(path_size < 64);
  char open_path[64];
  memcpy(open_path, path, path_size);
  open_path[path_size] = 0;

  // Try to read entire file.
  int fd = open(open_path, O_RDONLY);
  if (fd < 0) return 0;

  struct stat info;
  fstat(fd, &info);

  int size = info.st_size;
  char* data = malloc(info.st_size);

  if (read(fd, data, size) != size) return 0;

  // Get line count and veryify that \r only occur before \n.
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

  file = allocate_file(path, path_size);

  line_array_extend(&file->lines, line_count);
  Line* line = insert_line(file, 0);

  cr = 0;
  int index = 0;

  for (int i = 0; i < size; i++) {
    if (data[i] == '\n') {
      string_append_multiple(&line->chars, &data[index], i - index - cr);
      render_line(file, line);

      line = insert_line(file, file->lines.count);
      index = i + 1;
      cr = 0;
    }
    else if (data[i] == '\r') {
      cr = 1;
    }
  }

  if (index < size) {
    Line* line = insert_line(file, file->lines.count);
    string_append_multiple(&line->chars, &data[index], size - index - cr);
    render_line(file, line);
  }

  return file;
}

//--------------------------------------------------------------------------------------------------

static File* create_file(char* path, int path_size) {
  File* file = allocate_file(path, path_size);
  insert_line(file, 0);
  return file;
}

//--------------------------------------------------------------------------------------------------

static int save_file(File* file) {
  int fd = open(file->path.items, O_WRONLY | O_CREAT | O_TRUNC, 0666); // Do we want r/w for all users?
  if (fd < 0) return false;

  for (int i = 0; i < file->lines.count; i++) {
    if (i) {
      write(fd, "\r\n", 2);
    }

    Line* line = file->lines.items[i];
    write(fd, line->chars.items, line->chars.count);
  }

  close(fd);

  file->saved = true;
  return true;
}

//--------------------------------------------------------------------------------------------------

static FileState* get_file_state(Window* window, File* file) {
  for (int i = 0; i < window->file_states.count; i++) {
    FileState* state = window->file_states.items[i];
    if (state->file == file) {
      return state;
    }
  }

  return 0;
}

//--------------------------------------------------------------------------------------------------

static void change_file(Window* window, File* file) {
  File* old_file = window->file;
  window->file = file;

  if (old_file) {
    FileState* state = get_file_state(window, old_file);

    if (!state) {
      state = calloc(1, sizeof(FileState));
      state->file = old_file;
      file_state_array_append(&window->file_states, state);
    }

    state->cursor_x = window->cursor_x;
    state->cursor_y = window->cursor_y;
    state->cursor_x_ideal = window->cursor_x_ideal;
    state->offset_x = window->offset_x;
    state->offset_y = window->offset_y;
    state->mark_x = window->mark_x;
    state->mark_y = window->mark_y;
    state->mark_valid = window->mark_valid;
    state->previous_keycode = window->previous_keycode;
  }

  FileState* state = get_file_state(window, file);

  if (state) {
    window->cursor_x = state->cursor_x;
    window->cursor_y = state->cursor_y;
    window->cursor_x_ideal = state->cursor_x_ideal;
    window->offset_x = state->offset_x;
    window->offset_y = state->offset_y;
    window->mark_x = state->mark_x;
    window->mark_y = state->mark_y;
    window->mark_valid = state->mark_valid;
    window->previous_keycode = state->previous_keycode;
  }
  else {
    window->cursor_x = 0;
    window->cursor_y = 0;
    window->cursor_x_ideal = 0;
    window->offset_x = 0;
    window->offset_y = 0;
    window->mark_x = 0;
    window->mark_y = 0;
    window->mark_valid = false;
    window->previous_keycode = 0;
  }

  window->redraw = true;
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

static bool is_letter(char c) {
  return ('a' <= c && c <= 'z') || ('A' <= c && c <= 'Z');
}

//--------------------------------------------------------------------------------------------------

static bool is_number(char c) {
  return ('0' <= c && c <= '9');
}

//--------------------------------------------------------------------------------------------------

static bool is_indentifier_literal(char c) {
  return is_letter(c) || is_number(c) || c == '_';
}

//--------------------------------------------------------------------------------------------------

static int get_left_padding(Window* window) {
  int vertical_window_separator_width = (window->region->x) ? 2 : 0;
  int line_number_width = (window->file) ? count_digits(window->file->lines.count - 1) : 0;
  int margin = EditorLineNumberMargin;

  return vertical_window_separator_width + line_number_width + margin;
}

//--------------------------------------------------------------------------------------------------

static int get_left_bar_padding(Window* window) {
  return MinibarLeftPadding + strlen(bar_message[window->minibar_mode]);
}

//--------------------------------------------------------------------------------------------------

static void get_active_size(Window* window, int* width, int* height) {
  *width  = window->region->width - get_left_padding(window);
  *height = window->region->height - MinibarCount;
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

  window->offset_x = get_updated_offset(window->cursor_x, window->offset_x, width, EditorCursorMarginLeft, EditorCursorMarginRight);
  window->offset_y = get_updated_offset(window->cursor_y, window->offset_y, height, EditorCursorMarginTop, EditorCursorMarginBottom);

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
  update_window_offsets(window);
}

//--------------------------------------------------------------------------------------------------

static void update_window_cursor_x(Window* window, int x) {
  window->cursor_x = x;
  window->cursor_x_ideal = x;
}

//--------------------------------------------------------------------------------------------------

static void update_window_cursor_y(Window* window, int y) {
  window->cursor_y = y;
  window->cursor_x = window->cursor_x_ideal;
}

//--------------------------------------------------------------------------------------------------

static void update_window_offset_y(Window* window, int offset) {
  window->offset_y = limit(offset, 0, window->file->lines.count);
  window->redraw = true;
}

//--------------------------------------------------------------------------------------------------

static void insert_character(Window* window, char c) {
  File* file = window->file;
  Line* line = file->lines.items[window->cursor_y];

  string_insert(&line->chars, c, window->cursor_x++);
  render_line(file, line);

  file->saved = false;
}

//--------------------------------------------------------------------------------------------------

static void append_spaces(Line* line, int count) {
  while (count--) {
    string_append(&line->chars, ' ');
  }
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

static void insert_newline(Window* window) {
  File* file = window->file;
  Line* line = file->lines.items[window->cursor_y];

  char* tail = &line->chars.items[window->cursor_x];
  int tail_size = line->chars.count - window->cursor_x;
  
  string_truncate(&line->chars, window->cursor_x);
  render_line(file, line);

  int indent = get_leading_spaces(line);

  if (get_last_char(line) == '{') {
    if (window->previous_keycode == '{') {
      line = insert_line(file, window->cursor_y + 1);

      append_spaces(line, indent);
      string_append(&line->chars, '}');
      render_line(file, line);
    }

    indent += EditorSpacesPerTab;
  }
  
  line = insert_line(file, window->cursor_y + 1);

  append_spaces(line, indent);
  string_append_multiple(&line->chars, tail, tail_size);
  render_line(file, line);

  window->cursor_x = indent;
  window->cursor_y++;

  file->saved = false;
}

//--------------------------------------------------------------------------------------------------

static void delete_character(Window* window) {
  File* file = window->file;
  Line* line = file->lines.items[window->cursor_y];

  if (window->cursor_x) {
    string_remove(&line->chars, window->cursor_x - 1);
    update_window_cursor_x(window, window->cursor_x - 1);
    render_line(file, line);
  }
  else if (window->cursor_y) {
    Line* prev_line = file->lines.items[window->cursor_y - 1];

    update_window_cursor_x(window, prev_line->chars.count);
    update_window_cursor_y(window, window->cursor_y - 1);

    string_append_multiple(&prev_line->chars, line->chars.items, line->chars.count);
    render_line(file, prev_line);

    delete_line(window->file, window->cursor_y + 1);
  }

  file->saved = false;
}

//--------------------------------------------------------------------------------------------------

static int get_delete_count(Window* window, char* data, int cursor, bool ctrl) {
  if (!cursor) return 1;

  int space_count = 0;
  int other_count = 0;
  int char_count = 0;

  for (int i = 0; i < cursor; i++) {
    if (data[i] == ' ') {
      if (space_count == 2) {
        char_count = 0;
        other_count = 0;
      }

      space_count++;
    }
    else if (is_indentifier_literal(data[i])) {
      if (space_count) {
        char_count = 0;
      }

      space_count = 0;
      other_count = 0;
      char_count++;
    }
    else {
      if (space_count) {
        other_count = 0;
      }

      char_count = 0;
      space_count = 0;
      other_count++;
    }
  }

  bool aligned_to_tab = space_count && (space_count % EditorSpacesPerTab) == 0;

  if (ctrl) {
    return space_count + char_count + other_count;
  }
  else if (aligned_to_tab) {
    return EditorSpacesPerTab;
  }

  return 1;
}

//--------------------------------------------------------------------------------------------------

static void delete_character_or_word(Window* window, bool ctrl) {
  File* file = window->file;
  Line* line = file->lines.items[window->cursor_y];
  int count = get_delete_count(window, line->chars.items, window->cursor_x, ctrl);
  while (count--) {
    delete_character(window);
  }
}

//--------------------------------------------------------------------------------------------------

static void get_block_marks(Window* window, int* start_x, int* start_y, int* end_x, int* end_y) {
  if (window->mark_y > window->cursor_y || (window->mark_y == window->cursor_y && window->mark_x > window->cursor_x)) {
    *start_x = window->cursor_x;
    *start_y = window->cursor_y;

    *end_x = window->mark_x;
    *end_y = window->mark_y;
  }
  else {
    *start_x = window->mark_x;
    *start_y = window->mark_y;
    
    *end_x = window->cursor_x;
    *end_y = window->cursor_y;
  }
}

//--------------------------------------------------------------------------------------------------

static void insert_block(Window* window, char* data, int size) {
  File* file = window->file;
  Line* line = file->lines.items[window->cursor_y];

  string_clear(&buffer);
  string_append_multiple(&buffer, &line->chars.items[window->cursor_x], line->chars.count - window->cursor_x);
  string_truncate(&line->chars, window->cursor_x);

  int line_count = 0;
  for (int i = 0; i < size; i++) {
    if (data[i] == '\n') {
      line_count++;
    }
  }

  insert_lines(file, window->cursor_y + 1, line_count);

  int index = 0;
  int start = 0;

  while (index < size) {
    while (index < size && data[index] != '\n') {
      index++;
    }

    string_append_multiple(&line->chars, &data[start], index - start);
    render_line(file, line);

    if (index >= size) break;

    update_window_cursor_y(window, window->cursor_y + 1);
    line = file->lines.items[window->cursor_y];

    index++;
    start = index;
  }

  update_window_cursor_x(window, line->chars.count);

  if (buffer.count) {
    string_append_multiple(&line->chars, buffer.items, buffer.count);
    render_line(file, line);
  }
}

//--------------------------------------------------------------------------------------------------

static void delete_block(Window* window) {
  int start_x, start_y, end_x, end_y;
  get_block_marks(window, &start_x, &start_y, &end_x, &end_y);

  File* file = window->file;
  Line* line = file->lines.items[start_y];

  string_clear(&buffer);
  string_append_multiple(&buffer, line->chars.items, start_x);

  delete_lines(file, start_y, end_y - start_y);
  line = file->lines.items[start_y];

  string_remove_multiple(&line->chars, 0, end_x);
  string_insert_multiple(&line->chars, buffer.items, buffer.count, 0);
  
  update_window_cursor_x(window, buffer.count);
  update_window_cursor_y(window, start_y);

  line->redraw = true;
}

//--------------------------------------------------------------------------------------------------

static void copy_block(Window* window) {
  int start_x, start_y, end_x, end_y;
  get_block_marks(window, &start_x, &start_y, &end_x, &end_y);

  string_clear(&clipboard);

  File* file = window->file;
  Line* line = file->lines.items[start_y];

  while (start_y != end_y) {
    string_append_multiple(&clipboard, &line->chars.items[start_x], line->chars.count - start_x);
    string_append(&clipboard, '\n');
    render_line(file, line);

    line = file->lines.items[++start_y];
    start_x = 0;
  }

  string_append_multiple(&clipboard, &line->chars.items[start_x], end_x - start_x);
  render_line(file, line);
}

//--------------------------------------------------------------------------------------------------

static void cut(Window* window) {
  copy_block(window);
  delete_block(window);
}

//--------------------------------------------------------------------------------------------------

static void copy(Window* window) {
  copy_block(window);
}

//--------------------------------------------------------------------------------------------------

static void paste(Window* window) {
  insert_block(window, clipboard.items, clipboard.count);
}

//--------------------------------------------------------------------------------------------------

static void enter_minibar_mode(Window* window, int mode) {
  window->minibar_active = true;
  window->minibar_mode = mode;
  window->error_present = false;

  if (mode == MinibarModeFind) {
    window->saved_cursor_x = window->cursor_x;
    window->saved_cursor_y = window->cursor_y;
  }
}

//--------------------------------------------------------------------------------------------------

static void exit_minibar_mode(Window* window) {
  string_clear(&window->minibar_data);
  match_array_clear(&window->matches);

  window->minibar_active = false;
  window->minibar_cursor = 0;
  window->minibar_offset = 0;
}

//--------------------------------------------------------------------------------------------------


//--------------------------------------------------------------------------------------------------

static void editor_handle_keypress(Window* window, int keycode) {
  if (keycode == UserKeyOpen) {
    enter_minibar_mode(window, MinibarModeOpen);
  }
  else if (keycode == UserKeyFocusNext) {
    focus_next();
  }
  else if (keycode == UserKeyFocusPrevious) {
    focus_previous();
  }
  else if (keycode == UserKeyNew) {
    enter_minibar_mode(window, MinibarModeNew);
  }
  else if (keycode == UserKeyCommand) {
    enter_minibar_mode(window, MinibarModeCommand);
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
      delete_character_or_word(window, true);
      break;

    case KeyCodeCtrlF:
      enter_minibar_mode(window, MinibarModeFind);
      break;

    case KeyCodeDelete:
      delete_character_or_word(window, false);
      break;

    case KeyCodeTab:
      for (int i = 0; i < EditorSpacesPerTab; i++) {
        insert_character(window, ' ');
      }
      break;

    case KeyCodeEnter:
      insert_newline(window);
      break;

    case UserKeyFocusNext:
      focus_next();
      break;

    case UserKeyFocusPrevious:
      focus_previous();
      break;

    case UserKeyNew:
      enter_minibar_mode(window, MinibarModeNew);
      break;

    case UserKeyCommand:
      enter_minibar_mode(window, MinibarModeCommand);
      break;

    case KeyCodeEscape:
      window->error_present = false;
      break;

    case UserKeyMark:
      window->mark_valid = true;
      window->mark_x = window->cursor_x;
      window->mark_y = window->cursor_y;
      break;

    case UserKeyCut:
      cut(window);
      break;

    case UserKeyCopy:
      copy(window);
      break;

    case UserKeyPaste:
      paste(window);
      break;

    case UserKeySave:
      if (!save_file(window->file)) display_error(window, "can not save file `%.*s`", window->file->path.count, window->file->path.items);
      break;

    default:
      if (KeyCodePrintableStart <= keycode && keycode <= KeyCodePrintableEnd) {
        insert_character(window, (char)keycode);
      }
      else {
        debug("Unhandled window keycode: %d\n", keycode);
      }
  }

  if (window->file) {
    limit_window_cursor(window);
  }
}

//--------------------------------------------------------------------------------------------------

// The find algorithm is based on Boyer–Moore. It is much slower than VScode when searching files
// which are 100k+ lines. It is actually really slow... maybe something is wrong.
static void make_find_lookup(char* data, int size) {
  for (int i = 0; i < 256; i++) {
    find_char_lookup[i] = size;
  }

  for (int i = 0; i < size; i++) {
    find_char_lookup[(int)data[i]] = size - i - 1;
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

    find_index_lookup[i] = shift ? shift : 1;
  }
}

//--------------------------------------------------------------------------------------------------

static int find(char* word, int word_length, char* data, int data_length) {
  //debug("Worrd length: %d\n", word_length);
  //debug("Find: %.*s in %.*s [%d %d]\n", word_length, word, data_length, data, word_length, data_length);
  int data_index = word_length - 1;
  int matches = 0;

  while (data_index < data_length) {
    int tmp_data_index = data_index;
    int word_index = word_length - 1;
    int match_count = 0;

    while (word_index >= 0 && word[word_index] == data[data_index]) {
      word_index--;
      data_index--;
      match_count++;
    }

    if (word_index < 0) {
      find_indicies[matches++] = data_index + 1;
      data_index += word_length + 1;
      continue;
    }

    int skip = match_count ? find_index_lookup[match_count] : find_char_lookup[(int)data[data_index]];
    data_index = tmp_data_index + skip;
  }

  //debug("Done\n");

  return matches;
}


//--------------------------------------------------------------------------------------------------

// @Improve!
static void set_cursor_based_on_position(Window* window) {
  Match* position = &window->matches.items[window->match_index];

  int width, height;
  get_active_size(window, &width, &height);

  if (position->y >= window->offset_y + height - EditorCursorMarginBottom) {
    window->offset_y = 999999;
  }

  window->cursor_y = position->y;
  window->cursor_x = position->x;

  window->redraw = true;
}

//--------------------------------------------------------------------------------------------------

static void find_in_file(Window* window) {
  File* file = window->file;

  char* data = window->minibar_data.items;
  int size = window->minibar_data.count;

  int total_matches = 0;
  bool interrupted = false;

  make_find_lookup(data, size);
  match_array_clear(&window->matches);

  if (!size) {
    // @Hack: force clear markings.
    window->redraw = true;
    return;
  }

  for (int i = 0; i < file->lines.count; i++) {
    Line* line = file->lines.items[i];
    int count = find(data, size, line->chars.items, line->chars.count);

    total_matches += count;

    for (int j = 0; j < count; j++) {
      match_array_append(&window->matches, (Match){ .x = find_indicies[j], .y = i });
    }

    if (input_is_pending()) {
      match_array_clear(&window->matches); // Can't render nothing.
      interrupted = true;
      debug("Interrupt\n");
      break;
    }
  }

  if (!interrupted) {
    window->match_length = size;

    for (int i = 0; i < window->matches.count; i++) {
      Match* pos = &window->matches.items[i];
      //debug("Match at: (%d, %d)\n", pos->x, pos->y);
    }

    // Go through and select the closest match to the current cursor.
    for (int i = 0; i < window->matches.count; i++) {
      if (window->matches.items[i].y >= window->saved_cursor_y) {
        window->match_index = i;
        break;
      }
    }

    set_cursor_based_on_position(window);
  }
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

static bool skip_identifier(char** data, char* keyword) {
  skip_spaces(data);

  int size = strlen(keyword);

  if (!strncmp(*data, keyword, size)) {
    *data += size;
    return true;
  }

  return false;
}

//--------------------------------------------------------------------------------------------------

static char* read_identifier(char** data, int* size) {
  skip_spaces(data);

  char* tmp = *data;
  char* start = tmp;

  // Note: will accept names which start with numbers.
  while (is_indentifier_literal(*tmp)) {
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
  add_null_termination(&window->minibar_data);

  debug("test\n");

  char* data = window->minibar_data.items;

  if (skip_identifier(&data, "split")) {
    if (skip_char(&data, '-')) {
      split_window(window, true);
    }
    else if (skip_char(&data, '|')) {
      split_window(window, false);
    }
    else {
      display_error(window, "cant split");
    }
  }
  else if (skip_identifier(&data, "theme")) {
    int theme = -1;
    if (!read_number(&data, &theme)) {
      int size;
      char* name = read_identifier(&data, &size);
      if (size) {
        for (int i = 0; i < ColorThemeCount; i++) {
          if (themes[i].name && !strncmp(themes[i].name, name, size)) {
            theme = i;
            break;
          }
        }
      }
    }

    theme = limit(theme, 0, ColorThemeCount - 1);

    if (theme != current_theme) {
      current_theme = theme;
      update_terminal_background();

      for (int i = 0; i < windows.count; i++) {
        windows.items[i]->redraw = true;
      }
    }
  }
  else if (skip_identifier(&data, "close")) {
    remove_window(window);
  }
  else {
    display_error(window, "unknow command `%.*s`", window->minibar_data.count, window->minibar_data.items);
  }
}

//--------------------------------------------------------------------------------------------------

static void handle_minibar_enter(Window* window) {
  char* data = window->minibar_data.items;
  int size = window->minibar_data.count;

  switch (window->minibar_mode) {
    case MinibarModeOpen: {
      File* file = open_file(window->minibar_data.items, window->minibar_data.count);

      if (file) {
        change_file(window, file);
      }
      else {
        display_error(window, "can not open file `%.*s`", size, data);
      }
      break;
    }

    case MinibarModeNew:
      change_file(window, create_file(window->minibar_data.items, window->minibar_data.count));
      break;

    case MinibarModeCommand:
      handle_command(window);
      break;
    
    case MinibarModeFind:
      window->matches.count = 0;
      window->redraw = true;
      break;

    default:
      debug("Unhandled minibar type\n");
  }

  string_clear(&window->minibar_data);
}

//--------------------------------------------------------------------------------------------------

static void minibar_handle_keypress(Window* window, int keycode) {
  int delete_count = 1;

  if (KeyCodePrintableStart <= keycode && keycode <= KeyCodePrintableEnd) {
    string_insert(&window->minibar_data, keycode, window->minibar_cursor++);

    if (window->minibar_mode == MinibarModeFind) {
      find_in_file(window);
    }
  }
  else switch (keycode) {
    case KeyCodeEscape:
      // Terminatino of the current process. In case of find, the cursor might have changed. Restore it.
      window->cursor_x = window->saved_cursor_x;
      window->cursor_y = window->saved_cursor_y;
      window->matches.count = 0; // Disable find. Do something better.
      window->redraw = true;
      exit_minibar_mode(window);
      break;

    case KeyCodeLeft:
      window->minibar_cursor = max(window->minibar_cursor - 1, 0);
      break;
    
    case KeyCodeUp:
      if (window->minibar_mode == MinibarModeFind) {
        if (window->matches.count) {
          if (--window->match_index < 0) window->match_index = window->matches.count - 1;
          set_cursor_based_on_position(window);
        }
      }
      break;

    case KeyCodeDown:
      if (window->minibar_mode == MinibarModeFind) {
        if (window->matches.count) {
          if (++window->match_index == window->matches.count) window->match_index = 0;
          set_cursor_based_on_position(window);
        }
      }
      break;

    case KeyCodeCtrlDown:
      if (window->minibar_mode == MinibarModeFind) {
        if (window->matches.count) {
          int increment = 1 + (window->matches.count / 50);

          window->match_index += increment;

          if (window->match_index >= window->matches.count) {
            window->match_index -= window->matches.count;
          }

          set_cursor_based_on_position(window);
        }
      }
      break;

    case KeyCodeRight:
      window->minibar_cursor = min(window->minibar_cursor + 1, window->minibar_data.count);
      break;

    case KeyCodeHome:
      window->minibar_cursor = 0;
      break;

    case KeyCodeEnd:
      window->minibar_cursor = window->minibar_data.count;
      break;

    case KeyCodeCtrlDelete:
      delete_count = get_delete_count(window, window->minibar_data.items, window->minibar_cursor, true);
    case KeyCodeDelete:
      if (window->minibar_cursor) {
        string_remove_multiple(&window->minibar_data, window->minibar_cursor - delete_count, delete_count);
        window->minibar_cursor -= delete_count;

        if (window->minibar_mode == MinibarModeFind) {
          find_in_file(window);
        }
      }
      break;

    case KeyCodeEnter:
      handle_minibar_enter(window);
      exit_minibar_mode(window);
      break;

    default:
      debug("Unhandled minibar keycode: %d\n", keycode);
  }

  if (window->file) {
    limit_window_cursor(window);
  }
}

//--------------------------------------------------------------------------------------------------

static void update() {
  while (1) {
    int keycode = get_input();

    if (keycode == UserKeyExit) {
      running = false;
      return;
    }

    if (keycode != KeyCodeNone) {
      Window* window = focused_window;

      if (window->minibar_active) {
        minibar_handle_keypress(window, keycode);
      }
      else {
        editor_handle_keypress(window, keycode);
      }

      window->previous_keycode = keycode;
      break;
    }
  }
}

//--------------------------------------------------------------------------------------------------

static void render_line(File* file, Line* line) {
  // Must be done even though we have no highlighting.
  line->redraw = true;

  if (!file->highlight) return;

  int size = line->chars.count;
  char* data = line->chars.items;
  char* end = line->chars.items + size;

  int_array_extend(&line->colors, line->chars.count);
  line->colors.count = line->chars.count;

  Highlight* highlight = file->highlight;

  int def = ColorTypeEditorForeground;
  
  int i = 0;

  int single_size = strlen(highlight->single_line_comment_start);
  char* single = highlight->single_line_comment_start;

  while (i < size) {
    char c = data[i];

    while (i < size && c == ' ') {
      line->colors.items[i] = def;
      i++;
      c = data[i];
    }

    int remain = size - i;

    if (!remain) break;

    if (is_number(c)) {
      do {
        line->colors.items[i] = highlight->numbers ? ColorTypeNumber : def;
        i++;
      } while (i < size && is_number(data[i]));
    }
    else if (c == '"') {
      do {
        line->colors.items[i] = highlight->strings ? ColorTypeString : def;
        i++;
      } while (i < size && data[i] != '"');
      if (i < size) {
        line->colors.items[i] = highlight->strings ? ColorTypeString : def;
        ++i;
      }
    }
    else if (!memcmp(single, data + i, single_size)) {
      for (int x = i; x < size; x++) {
        line->colors.items[x] = ColorTypeComment;
      }
      break;
    }
    else if (is_letter(c)) {
      int start = i;

      while (i < size && (is_letter(data[i]) || is_number(data[i]) || data[i] == '_')) {
        i++;
      }
      int size = i - start;
      int color = def;
      if (size < MaxKeywordSize) {
        char** keywords = highlight->keywords[size];

        if (keywords) {
          while (*keywords) {
            if (!memcmp(data + start, *keywords, size)) {
              color = ColorTypeKeyword;
              break;
            }
            keywords++;
          }
        }
      }

      for (int x = start; x < i; x++) {
        line->colors.items[x] = color;
      }
    }
    else {
      line->colors.items[i] = def;
      i++;
    }
  }

  assert(line->colors.count == line->chars.count);

  for (int i = 0;i < line->chars.count; i++) {
    int tmp = line->colors.items[i];
    if (tmp < 0 || ColorTypeCount <= tmp) {
      debug("Error: %d\n", tmp);
    }
  }
}


//--------------------------------------------------------------------------------------------------

static void render_status_bar(Window* window) {
  static const char unsaved_string[] = "*";
  static const char marked_string[] = "[] ";
  static const char nofile_string[] = "no file";

  int width = window->region->width - MinibarLeftPadding - MinibarRightPadding;
  int percent;

  if (window->file) {
    assert(window->file->lines.count);
    percent = 100 * window->cursor_y / window->file->lines.count;

    int path_width = min(window->file->path.size, MinibarMaxPathWidth) + 1;
    int unsaved_width = !window->file->saved ? sizeof(unsaved_string) - 1 : 0;
    int marked_width = window->mark_valid ? sizeof(marked_string) - 1 : 0;
    int percent_width = count_digits(percent) + sizeof((char)'%');

    width -= path_width + unsaved_width + marked_width + percent_width;
  }
  else {
    width -= sizeof(nofile_string) - 1;
  }

  set_background_color(ColorTypeMinibarBackground);
  set_foreground_color(ColorTypeMinibarForeground);

  set_window_cursor(window, 0, window->region->height - 1);

  print("%*c", MinibarLeftPadding, ' ');

  if (window->error_present) {
    set_foreground_color(ColorTypeMinibarError);
    width -= print("error: ");
    width -= print("%.*s", min(window->error_message.count, width), window->error_message.items);
    set_foreground_color(ColorTypeMinibarForeground);
  }
  else if (window->minibar_active) {
    if (window->matches.count) {
      width -= count_digits(window->matches.count) + 1 + count_digits(window->match_index) + 1;
    }

    width -= print("%s", bar_message[window->minibar_mode]);

    window->minibar_offset = get_updated_offset(window->minibar_cursor, window->minibar_offset, width, MinibarLeftCursorMargin, MinibarRightCursorMargin);
    int bar_size = min(window->minibar_data.count - window->minibar_offset, width - MinibarCommandPadding);

    width -= print("%.*s", bar_size, &window->minibar_data.items[window->minibar_offset]);
  }

  if (width) {
    print("%*c", width, ' ');
  }

  if (window->matches.count) {
    print("%d/%d ", window->match_index, window->matches.count);
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

  print("%*c", MinibarRightPadding, ' ');

  clear_formatting();
}

//--------------------------------------------------------------------------------------------------

static void render_window(Window* window) {
  File* file = window->file;

  if (file) {
    int width, height;
    get_active_size(window, &width, &height);

    int number_width = count_digits(file->lines.count - 1); // Done another place, cleanup!

    set_background_color(ColorTypeEditorBackground);
    set_foreground_color(ColorTypeEditorForeground);

    int current_index = 0;

    while (current_index < window->matches.count && window->matches.items[current_index].y < window->offset_y) {
      current_index++;
    }

    for (int y = 0; y < get_visible_line_count(window); y++) {
      if (!redraw_line[window->region->y + y]) continue;

      Line* line = file->lines.items[window->offset_y + y];

      set_window_cursor(window, 0, y);

      if (window->region->x) {
        set_background_color(ColorTypeMinibarBackground);
        print(" ");
        set_background_color(ColorTypeEditorBackground);
        print(" ");
      }

      set_foreground_color(ColorTypeEditorForeground);
      print("%*d", number_width, window->offset_y + y);
      print("%*c", EditorLineNumberMargin, ' ');

      int size = max(min(line->chars.count - window->offset_x, width), 0);

      int count = window->match_length;
      bool match = window->offset_y + y == window->cursor_y;

      for (int i = 0; i < size; i++) {
        int index = window->offset_x + i;

        // Check for a highlight match.
        Match* position = &window->matches.items[current_index];

        if (window->matches.count && position->y == window->offset_y + y) {
          if (position->x == index) {
            if (position->x == window->cursor_x && position->y == window->cursor_y) {
              set_foreground_color(ColorTypeSelectedMatchForeground);
              set_background_color(ColorTypeSelectedMatchBackground);
            }
            else {
              set_foreground_color(ColorTypeMatchForeground);
              set_background_color(ColorTypeMatchBackground);
            }
          }
          else if (position->x + window->match_length == index) {
            set_foreground_color(ColorTypeEditorForeground);
            set_background_color(ColorTypeEditorBackground);
            if (++current_index == window->matches.count) current_index = 0;
          }
        }
        else if (file->highlight) {
          set_foreground_color(line->colors.items[index]);
        }

        print("%c", line->chars.items[index]);
      }
    }

    for (int j = get_visible_line_count(window); j < height; j++) {
      if (!redraw_line[window->region->y + j]) continue;
      set_window_cursor(window, 0, j);
      if (window->region->x) {
        set_background_color(ColorTypeMinibarBackground);
        print(" ");
        set_background_color(ColorTypeEditorBackground);
        print(" ");
      }
    }
  }
  else {

    for (int j = 0; j < window->region->height - 1; j++) {
      if (!redraw_line[window->region->y + j]) continue;
      set_window_cursor(window, 0, j);
      if (window->region->x) {
        set_background_color(ColorTypeMinibarBackground);
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
  memset(redraw_line, 0, master_region.height);

  for (int i = 0; i < windows.count; i++) {
    Window* window = windows.items[i];

    // Redraw entire occupied region if either the window is moved or the file is dirty.
    if (window->redraw || (window->file && window->file->redraw)) {
      window->redraw = false;

      for (int j = 0; j < window->region->height; j++) {
        redraw_line[window->region->y + j] = true;
      }
    }

    // Redraw dirty visible lines.
    if (window->file) {
      for (int j = 0; j < get_visible_line_count(window); j++) {
        if (window->file->lines.items[window->offset_y + j]->redraw) {
          redraw_line[window->region->y + j] = true;
        }
      }
    }

    // Redraw status bars.
    for (int j = 0; j < MinibarCount; j++) {
      redraw_line[window->region->y + window->region->height - j - 1] = true;
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
  mark_lines_for_redraw();
  set_background_color(ColorTypeEditorBackground); // @Necessary?

  for (int i = 0; i < master_region.height; i++) {
    if (redraw_line[i]) {
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

  if (window->minibar_active) {
    set_cursor_color(ColorTypeMinibarCursor);
    cursor_x = window->minibar_cursor - window->minibar_offset + get_left_bar_padding(window);
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
    int height = limit(region->height * region->split, WindowMinimumHeight, region->height - WindowMinimumHeight);

    region->split = (float)height / region->height;

    region->childs[0]->width = region->width;
    region->childs[1]->width = region->width;

    region->childs[0]->height = height;
    region->childs[1]->height = region->height - height;

    region->childs[1]->x = region->x;
    region->childs[1]->y = region->y + height;
  }
  else {
    int width = limit(region->width * region->split, WindowMinimumWidth, region->width - WindowMinimumWidth - 1);

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
  string_init(&framebuffer, 16 * 1024);
  window_array_init(&windows, 16);
  file_array_init(&files, 16);

  master_region.window = allocate_window();
  focused_window = master_region.window;

  resize_master_region();

  set_cursor_color(ColorTypeEditorCursor);
  update_terminal_background();
  clear_terminal();
  flush();
}

//--------------------------------------------------------------------------------------------------

int main() {
  printf("test: %d\n", git_commit("Another commit", 14));
  return 0;
  terminal_init();
  editor_init();

  char path[] = "test/test.c";
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
