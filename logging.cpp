//#include "stdafx.h"
#include <stdarg.h>
#include "logging.h"

FILE *logF;

void log_printf(FILE *f, char * format, ...)
{
  va_list args;
  va_start(args, format);
  vfprintf(f, format, args);
  fflush(f);
  va_end(args);
}
