#include <stdbool.h>

void listSearch(void);
void cmdRawHelp(void);
void cmdHelp(void);
void cmdDump(void);
void cmdMDump(void);
void cmdDisassemble(void);
void cmdStep(void);
void cmdNext(void);
void cmdFinish(void);
void cmdPrintByte(void);
void cmdPrintWord(void);
void cmdPrintDWord(void);
void cmdPrintString(void);
void cmdClearScreen(void);
void cmdAutoClearScreen(void);
void cmdSetBreakpoint(void);
void cmdWatchByte(void);
void cmdWatchByte(void);
void cmdWatchWord(void);
void cmdWatchDWord(void);
void cmdWatchString(void);
void cmdWatches(void);
void cmdDeleteWatch(void);
void cmdAutoWatch(void);
void cmdSymbolValue(void);
void cmdSave(void);
void cmdLoad(void);
void cmdBackTrace(void);
void cmdUpFrame(void);
void cmdDownFrame(void);

#define BUFSIZE 4096

extern char outbuf[];
extern char inbuf[];
extern bool ctrlcflag;

typedef struct
{
  char* name;
  void (*func)(void);
  char* params;
  char* help;
} type_command_details;

typedef struct tse
{
  char* symbol;
  int addr;   // integer value of symbol
  char* sval; // string value of symbol
  struct tse* next;
} type_symmap_entry;

typedef enum { TYPE_BYTE, TYPE_WORD, TYPE_DWORD, TYPE_STRING } type_watch;
extern char* type_names[];

typedef struct we
{
  type_watch type; 
  char* name;
  struct we* next;
} type_watch_entry;

extern type_command_details command_details[];
extern type_symmap_entry* lstSymMap;
extern type_watch_entry* lstWatches;
