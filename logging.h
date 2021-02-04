#ifndef LOGGING_H
#define LOGGING_H
#include <stdio.h>

//#define DO_LOG

extern FILE *logF;
#ifdef DO_LOG
 #define lprintf log_printf
 void log_printf(FILE *f, char * format, ...);
#else
 #define lprintf()
#endif


#endif