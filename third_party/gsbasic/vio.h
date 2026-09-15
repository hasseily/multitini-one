#include <stdarg.h>
#include <stdio.h>
#ifdef __cplusplus
extern "C" {
#endif


void printToLine(char *text, int wr);
void printToLineC(char c, int wr);
void showVectrexOut();
void clearVectrexOut();
int printBackspace();

extern int vectrexMode;
extern int T_WIDTH;
extern int T_HEIGHT;
extern int MAX_VLINES;
extern int Y_START;
extern int MAX_VROWS;


/* Arguments are evaluated outside this function's buffer scope, including
 * the caller-local buf used by Executive::TraceStatement. */
static inline void mt_gsb_vprint(FILE *stream, int wrap, const char *format, ...)
{
  char buffer[512];
  va_list arguments;
  va_start(arguments, format);
  vsnprintf(buffer, sizeof buffer, format, arguments);
  va_end(arguments);
  if (vectrexMode && stream == stdout) printToLine(buffer, wrap);
  else fprintf(stream, "%s", buffer);
}

#define VPRINT(...) mt_gsb_vprint(stdout, 1, __VA_ARGS__)

#define FPUTS(s,f) \
do\
{\
  if ((vectrexMode) && (f==stdout)) printToLine((char *)s, 1);\
  else fputs(s,f);\
} while (0)

#define FPUTC(c,f) \
do\
{\
  if ((vectrexMode) && (f==stdout)) printToLineC(c, 1);\
  else {fputc(c,f);}\
} while (0)

#define VFPRINT(f,...) mt_gsb_vprint((f), 1, __VA_ARGS__)





#define VPRINT2(...) mt_gsb_vprint(stdout, 0, __VA_ARGS__)

#define FPUTS2(s,f) \
do\
{\
  if ((vectrexMode) && (f==stdout)) printToLine((char *)s, 0);\
  else fputs(s,f);\
} while (0)

#define FPUTC2(c,f) \
do\
{\
  if ((vectrexMode) && (f==stdout)) printToLineC(c, 0);\
  else {fputc(c,f);}\
} while (0)

#define VFPRINT2(f,...) mt_gsb_vprint((f), 0, __VA_ARGS__)













  #ifdef __cplusplus
}
#endif
