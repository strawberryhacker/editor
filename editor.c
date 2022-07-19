#include "array.h"
#include "stdio.h"
#include "stdarg.h"
#include "assert.h"

//--------------------------------------------------------------------------------------------------

#define min(x, y) (((int)(x) < (int)(y)) ? x : y)
#define max(x, y) (((int)(x) < (int)(y)) ? y : x)

//--------------------------------------------------------------------------------------------------

static void print(const char* data, ...) {
  static char buffer[1024];

  va_list arguments;
  va_start(arguments, data);
  int size = vsnprintf(buffer, 1024, data, arguments);
  va_end(arguments);

  // Run tty in the remoote terminal to find the path.
  FILE* fd = fopen("/dev/pts/3", "w");
  assert(fd > 0);

  fwrite(buffer, 1, size, fd);
  fclose(fd);
}

//--------------------------------------------------------------------------------------------------

int main() {
  print("Testing\n");
  return 0;
}
