#ifndef _DEFINES_H
#define _DEFINES_H

#include <windows.h>
typedef unsigned int   uint;
/*#ifndef DWORD
typedef unsigned int   DWORD; //32 bits
typedef          int   LONG; //32 bits
typedef unsigned short WORD; //16 bits
typedef unsigned char  BYTE;
typedef          int   BOOL;
#endif*/

#define INLINE         __forceinline 
#define Abs(x)         ((x) >= 0 ? x : -(x))
//#define NULL           0
//#define FALSE          0
//#define TRUE           1

/*typedef struct tagBITMAPINFOHEADER {
    DWORD  biSize;
    LONG   biWidth;
    LONG   biHeight;
    WORD   biPlanes;
    WORD   biBitCount;
    DWORD  biCompression;
    DWORD  biSizeImage;
    LONG   biXPelsPerMeter;
    LONG   biYPelsPerMeter;
    DWORD  biClrUsed;
    DWORD  biClrImportant;
} BITMAPINFOHEADER;

typedef struct tagBITMAPFILEHEADER { 
  WORD    bfType; 
  DWORD   bfSize; 
  WORD    bfReserved1; 
  WORD    bfReserved2; 
  DWORD   bfOffBits; 
} BITMAPFILEHEADER, *PBITMAPFILEHEADER; */



#endif