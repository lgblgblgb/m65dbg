#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <stdlib.h>
#include <ctype.h>
#include <unistd.h>
#include "commands.h"
#include "serial.h"
#include "gs4510.h"

int get_sym_value(char* token);

typedef struct
{
  int pc;
	int a;
	int x;
	int y;
	int z;
	int b;
	int sp;
	int mapl;
	int maph;
} reg_data;

typedef struct
{
  int addr;
  unsigned int b[16];
} mem_data;

bool outputFlag = true;

char outbuf[BUFSIZE] = { 0 };	// the buffer of what command is output to the remote monitor
char inbuf[BUFSIZE] = { 0 }; // the buffer of what is read in from the remote monitor

char* type_names[] = { "BYTE  ", "WORD  ", "DWORD ", "STRING" };

bool autocls = false; // auto-clearscreen flag
bool autowatch = false; // auto-watch flag
bool ctrlcflag = false; // a flag to keep track of whether ctrl-c was caught
int  traceframe = 0;  // tracks which frame within the backtrace

type_command_details command_details[] =
{
  { "help", cmdHelp, NULL,  "Shows help information on m65dbg commands" },
	{ "dump", cmdDump, "<addr> [<count>]", "Dumps memory (CPU context) at given address (with character representation in right-column" },
	{ "mdump", cmdMDump, "<addr> [<count>]", "Dumps memory (28-bit addresses) at given address (with character representation in right-column" },
  { "dis", cmdDisassemble, "[<addr> [<count>]]", "Disassembles the instruction at <addr> or at PC. If <count> exists, it will dissembly that many instructions onwards" },
  { "step", cmdStep, NULL, "Step into next instruction" }, // equate to pressing 'enter' in raw monitor
  { "n", cmdNext, NULL, "Step over to next instruction" },
  { "finish", cmdFinish, NULL, "Continue running until function returns (ie, step-out-from)" },
  { "pb", cmdPrintByte, "<addr>", "Prints the byte-value of the given address" },
  { "pw", cmdPrintWord, "<addr>", "Prints the word-value of the given address" },
  { "pd", cmdPrintDWord, "<addr>", "Prints the dword-value of the given address" },
  { "ps", cmdPrintString, "<addr>", "Prints the null-terminated string-value found at the given address" },
  { "cls", cmdClearScreen, NULL, "Clears the screen" },
  { "autocls", cmdAutoClearScreen, "0/1", "If set to 1, clears the screen prior to every step/next command" },
	{ "break", cmdSetBreakpoint, "<addr>", "Sets the hardware breakpoint to the desired address" },
  { "wb", cmdWatchByte, "<addr>", "Watches the byte-value of the given address" },
  { "ww", cmdWatchWord, "<addr>", "Watches the word-value of the given address" },
  { "wd", cmdWatchDWord, "<addr>", "Watches the dword-value of the given address" },
  { "ws", cmdWatchString, "<addr>", "Watches the null-terminated string-value found at the given address" },
  { "watches", cmdWatches, NULL, "Lists all watches and their present values" },
  { "wdel", cmdDeleteWatch, "<watch#>/all", "Deletes the watch number specified (use 'watches' command to get a list of existing watch numbers)" },
  { "autowatch", cmdAutoWatch, "0/1", "If set to 1, shows all watches prior to every step/next/dis command" },
  { "symbol", cmdSymbolValue, "<symbol>", "retrieves the value of the symbol from the .map file" },
  { "save", cmdSave, "<binfile> <addr28> <count>", "saves out a memory dump to <binfile> starting from <addr28> and for <count> bytes" },
  { "load", cmdLoad, "<binfile> <addr28>", "loads in <binfile> to <addr28>" },
	{ "back", cmdBackTrace, NULL, "produces a rough backtrace from the current contents of the stack" },
	{ "up", cmdUpFrame, NULL, "The 'dis' disassembly command will disassemble one stack-level up from the current frame" },
	{ "down", cmdDownFrame, NULL, "The 'dis' disassembly command will disassemble one stack-level down from the current frame" },
	{ NULL, NULL }
};

char* get_extension(char* fname)
{
  return strrchr(fname, '.');
}

typedef struct tfl
{
	int addr;
  char* file;
	int lineno;
	struct tfl *next;
} type_fileloc;

type_fileloc* lstFileLoc = NULL;

type_symmap_entry* lstSymMap = NULL;

type_watch_entry* lstWatches = NULL;

void add_to_list(type_fileloc fl)
{
	type_fileloc* iter = lstFileLoc;

  // first entry in list?
  if (lstFileLoc == NULL)
	{
		lstFileLoc = malloc(sizeof(type_fileloc));
		lstFileLoc->addr = fl.addr;
		lstFileLoc->file = strdup(fl.file);
		lstFileLoc->lineno = fl.lineno;
		lstFileLoc->next = NULL;
		return;
	}

  while (iter != NULL)
	{
	  // replace existing?
	  if (iter->addr == fl.addr)
		{
			iter->file = strdup(fl.file);
			iter->lineno = fl.lineno;
			return;
		}
		// insert entry?
		if (iter->addr > fl.addr)
		{
		  type_fileloc* flcpy = malloc(sizeof(type_fileloc));
			flcpy->addr = iter->addr;
			flcpy->file = iter->file;
			flcpy->lineno = iter->lineno;
			flcpy->next = iter->next;

			iter->addr = fl.addr;
			iter->file = strdup(fl.file);
			iter->lineno = fl.lineno;
			iter->next = flcpy;
			return;
		}
		// add to end?
		if (iter->next == NULL)
		{
		  type_fileloc* flnew = malloc(sizeof(type_fileloc));
			flnew->addr = fl.addr;
			flnew->file = strdup(fl.file);
			flnew->lineno = fl.lineno;
			flnew->next = NULL;

			iter->next = flnew;
			return;
		}

		iter = iter->next;
	}
}

void add_to_symmap(type_symmap_entry sme)
{
	type_symmap_entry* iter = lstSymMap;

  // first entry in list?
  if (lstSymMap == NULL)
	{
		lstSymMap = malloc(sizeof(type_symmap_entry));
		lstSymMap->addr = sme.addr;
		lstSymMap->sval = strdup(sme.sval);
		lstSymMap->symbol = strdup(sme.symbol);
		lstSymMap->next = NULL;
		return;
	}

  while (iter != NULL)
	{
		// insert entry?
		if (iter->addr >= sme.addr)
		{
		  type_symmap_entry* smecpy = malloc(sizeof(type_symmap_entry));
			smecpy->addr = iter->addr;
			smecpy->sval = iter->sval;
			smecpy->symbol = iter->symbol;
			smecpy->next = iter->next;

			iter->addr = sme.addr;
			iter->sval = strdup(sme.sval);
			iter->symbol = strdup(sme.symbol);
			iter->next = smecpy;
			return;
		}
		// add to end?
		if (iter->next == NULL)
		{
		  type_symmap_entry* smenew = malloc(sizeof(type_symmap_entry));
			smenew->addr = sme.addr;
			smenew->sval = strdup(sme.sval);
			smenew->symbol = strdup(sme.symbol);
			smenew->next = NULL;

			iter->next = smenew;
			return;
		}

		iter = iter->next;
	}
}

void add_to_watchlist(type_watch_entry we)
{
  type_watch_entry* iter = lstWatches;

	// first entry in list?
	if (lstWatches == NULL)
	{
	  lstWatches = malloc(sizeof(type_watch_entry));
		lstWatches->type = we.type;
    lstWatches->name = strdup(we.name);
		lstWatches->next = NULL;
		return;
	}

	while (iter != NULL)
	{
	  // add to end?
		if (iter->next == NULL)
		{
		  type_watch_entry* wenew = malloc(sizeof(type_watch_entry));
			wenew->type = we.type;
			wenew->name = strdup(we.name);
			wenew->next = NULL;

			iter->next = wenew;
			return;
		}

		iter = iter->next;
	}
}

type_fileloc* find_in_list(int addr)
{
	type_fileloc* iter = lstFileLoc;

	while (iter != NULL)
	{
	  if (iter->addr == addr)
			return iter;

		iter = iter->next;
	}

	return NULL;
}

type_symmap_entry* find_in_symmap(char* sym)
{
	type_symmap_entry* iter = lstSymMap;

	while (iter != NULL)
	{
	  if (strcmp(sym, iter->symbol) == 0)
			return iter;

		iter = iter->next;
	}

	return NULL;
}

type_watch_entry* find_in_watchlist(char* name)
{
  type_watch_entry* iter = lstWatches;

	while (iter != NULL)
	{
	  if (strcmp(iter->name, name) == 0)
		  return iter;

		iter = iter->next;
	}

	return NULL;
}

bool delete_from_watchlist(int wnum)
{
  int cnt = 0;

	type_watch_entry* iter = lstWatches;
	type_watch_entry* prev = NULL;

	while (iter != NULL)
	{
	  cnt++;

    // we found the item to delete?
		if (cnt == wnum)
		{
		  // first entry of list?
		  if (prev == NULL)
			{
			  lstWatches = iter->next;
				free(iter->name);
				free(iter);
				if (outputFlag)
					printf("watch#%d deleted!\n", wnum);
				return true;
			}
			else
			{
			  prev->next = iter->next;
				free(iter->name);
				free(iter);
				if (outputFlag)
					printf("watch#%d deleted!\n", wnum);
				return true;
			}
		}

    prev = iter;
		iter = iter->next;
	}

	return false;
}

// loads the *.map file corresponding to the provided *.list file (if one exists)
void load_map(char* fname)
{
  char strMapFile[200];
	strcpy(strMapFile, fname);
	char* sdot = strrchr(strMapFile, '.');
	*sdot = '\0';
	strcat(strMapFile, ".map");

  // check if file exists
	if (access(strMapFile, F_OK) != -1)
	{
		printf("Loading \"%s\"...\n", strMapFile);

		// load the map file
		FILE* f = fopen(strMapFile, "rt");

		while (!feof(f))
		{
			char line[1024];
			char sval[256];
			fgets(line, 1024, f);

			int addr;
			char sym[1024];
			sscanf(line, "$%04X %s", &addr, sym);
			sscanf(line, "%s", sval);

			//printf("%s : %04X\n", sym, addr);
			type_symmap_entry sme;
			sme.addr = addr;
			sme.sval = sval; 
			sme.symbol = sym;
			add_to_symmap(sme);
		}
	}
}

// loads the given *.list file
void load_list(char* fname)
{
  FILE* f = fopen(fname, "rt");
	char line[1024];

  while (!feof(f))
	{
	  fgets(line, 1024, f);

		if (strlen(line) == 0)
		  continue;

		char *s = strrchr(line, '|');
		if (s != NULL && *s != '\0')
		{
			s++;
			if (strlen(s) < 5)
			  continue;

			int addr;
      char file[1024];
			int lineno;
			strcpy(file, &strtok(s, ":")[1]);
			sscanf(strtok(NULL, ":"), "%d", &lineno);
			sscanf(line, " %X", &addr);

			//printf("%04X : %s:%d\n", addr, file, lineno);
			type_fileloc fl;
			fl.addr = addr;
			fl.file = file;
			fl.lineno = lineno;
			add_to_list(fl);
		}
	}

	load_map(fname);
}

#define KNRM  "\x1B[0m"
#define KRED  "\x1B[31m"
#define KGRN  "\x1B[32m"
#define KYEL  "\x1B[33m"
#define KBLU  "\x1B[34m"
#define KMAG  "\x1B[35m"
#define KCYN  "\x1B[36m"
#define KWHT  "\x1B[37m"
#define KINV  "\x1B[7m"
#define KCLEAR "\x1B[2J"
#define KPOS0_0 "\x1B[1;1H"

void show_location(type_fileloc* fl)
{
  FILE* f = fopen(fl->file, "rt");
  if (f == NULL)
    return;
	char line[1024];
	int cnt = 1;

	while (!feof(f))
	{
		fgets(line, 1024, f);
		if (cnt >= (fl->lineno - 10) && cnt <= (fl->lineno + 10) )
		{
		  if (cnt == fl->lineno)
		  {
				printf("%s> %d: %s%s", KINV, cnt, line, KNRM);
			}
			else
				printf("> %d: %s", cnt, line);
			//break;
		}
		cnt++;
	}
	fclose(f);
}

// search the current directory for *.list files
void listSearch(void)
{
  DIR           *d;
  struct dirent *dir;
  d = opendir(".");
  if (d)
  {
    while ((dir = readdir(d)) != NULL)
    {
		  char* ext = get_extension(dir->d_name);
		  if (ext != NULL && strcmp(ext, ".list") == 0)
			{
				printf("Loading \"%s\"...\n", dir->d_name);
				load_list(dir->d_name);
			}
    }

    closedir(d);
  }
}

reg_data get_regs(void)
{
  reg_data reg = { 0 };
  char* line;
  serialWrite("r\n");
  serialRead(inbuf, BUFSIZE);
  line = strstr(inbuf+2, "\n") + 1;
  sscanf(line,"%04X %02X %02X %02X %02X %02X %04X %04X %04X",
    &reg.pc, &reg.a, &reg.x, &reg.y, &reg.z, &reg.b, &reg.sp, &reg.mapl, &reg.maph);

  return reg;
}


mem_data get_mem(int addr)
{
  mem_data mem = { 0 };
  char str[100];
  sprintf(str, "d%04X\n", addr); // use 'd' instead of 'm' (for memory in cpu context)
  serialWrite(str);
  serialRead(inbuf, BUFSIZE);
  sscanf(inbuf, " :%X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
  &mem.addr, &mem.b[0], &mem.b[1], &mem.b[2], &mem.b[3], &mem.b[4], &mem.b[5], &mem.b[6], &mem.b[7], &mem.b[8], &mem.b[9], &mem.b[10], &mem.b[11], &mem.b[12], &mem.b[13], &mem.b[14], &mem.b[15]); 

  return mem;
}

mem_data get_mem28(int addr)
{
  mem_data mem = { 0 };
  char str[100];
  sprintf(str, "m%04X\n", addr);
  serialWrite(str);
  serialRead(inbuf, BUFSIZE);
  sscanf(inbuf, " :%X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
  &mem.addr, &mem.b[0], &mem.b[1], &mem.b[2], &mem.b[3], &mem.b[4], &mem.b[5], &mem.b[6], &mem.b[7], &mem.b[8], &mem.b[9], &mem.b[10], &mem.b[11], &mem.b[12], &mem.b[13], &mem.b[14], &mem.b[15]); 

  return mem;
}

// read all 32 lines at once (to hopefully speed things up for saving memory dumps)
mem_data* get_mem28array(int addr)
{
  static mem_data multimem[32];
  mem_data* mem;
  char str[100];
  sprintf(str, "M%04X\n", addr);
  serialWrite(str);
  serialRead(inbuf, BUFSIZE);
  char* strLine = strtok(inbuf, "\n");
  for (int k = 0; k < 32; k++)
  {
    mem = &multimem[k];
    sscanf(strLine, " :%X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
    &mem->addr, &mem->b[0], &mem->b[1], &mem->b[2], &mem->b[3], &mem->b[4], &mem->b[5], &mem->b[6], &mem->b[7], &mem->b[8], &mem->b[9], &mem->b[10], &mem->b[11], &mem->b[12], &mem->b[13], &mem->b[14], &mem->b[15]); 
    strLine = strtok(NULL, "\n");
  }

  return multimem;
}

// write buffer to client ram
void put_mem28array(int addr, unsigned char* data, int size)
{
  char str[10];
  sprintf(outbuf, "s%08X", addr);

  int i = 0;
  while(i < size) 
  {
    sprintf(str, " %02X", data[i]);
    strcat(outbuf, str);
    i++;
  }
  strcat(outbuf, "\n");

  serialWrite(outbuf);
  serialRead(inbuf, BUFSIZE);
}

void cmdHelp(void)
{
  printf("m65dbg commands\n"
         "===============\n");

  for (int k = 0; command_details[k].name != NULL; k++)
  {
	  type_command_details cd = command_details[k];

		if (cd.params == NULL)
			printf("%s = %s\n", cd.name, cd.help);
		else
			printf("%s %s = %s\n", cd.name, cd.params, cd.help);
  }

  printf(
	 "[ENTER] = repeat last command\n"
   "q/x/exit = exit the program\n"
   );
}


void cmdDump(void)
{
	char* strAddr = strtok(NULL, " ");

	if (strAddr == NULL)
	{
	  printf("Missing <addr> parameter!\n");
		return;
	}

	int addr = get_sym_value(strAddr);

	int total = 16;
	char* strTotal = strtok(NULL, " ");

	if (strTotal != NULL)
	{
	  sscanf(strTotal, "%X", &total);
	}

  int cnt = 0;
	while (cnt < total)
	{
		// get memory at current pc
		mem_data mem = get_mem(addr + cnt);

		printf(" :%07X ", mem.addr);
		for (int k = 0; k < 16; k++)
		{
		  if (k == 8) // add extra space prior to 8th byte
			  printf(" ");

			printf("%02X ", mem.b[k]);
		}
		
		printf(" | ");

		for (int k = 0; k < 16; k++)
		{
			int c = mem.b[k];
			if (isprint(c))
				printf("%c", c);
			else
				printf(".");
		}
		printf("\n");
		cnt+=16;

		if (ctrlcflag)
			break;
	}
}

void cmdMDump(void)
{
	char* strAddr = strtok(NULL, " ");

	if (strAddr == NULL)
	{
	  printf("Missing <addr> parameter!\n");
		return;
	}

	int addr = get_sym_value(strAddr);

	int total = 16;
	char* strTotal = strtok(NULL, " ");

	if (strTotal != NULL)
	{
	  sscanf(strTotal, "%X", &total);
	}

  int cnt = 0;
	while (cnt < total)
	{
		// get memory at current pc
		mem_data mem = get_mem28(addr + cnt);

		printf(" :%07X ", mem.addr);
		for (int k = 0; k < 16; k++)
		{
		  if (k == 8) // add extra space prior to 8th byte
			  printf(" ");

			printf("%02X ", mem.b[k]);
		}
		
		printf(" | ");

		for (int k = 0; k < 16; k++)
		{
			int c = mem.b[k];
			if (isprint(c))
				printf("%c", c);
			else
				printf(".");
		}
		printf("\n");
		cnt+=16;

		if (ctrlcflag)
			break;
	}
}

// return the last byte count
int disassemble_addr_into_string(char* str, int addr)
{
  int last_bytecount = 0;
  char s[32] = { 0 };

	// get memory at current pc
	mem_data mem = get_mem(addr);

	// now, try to disassemble it

	// Program counter
	sprintf(str, "$%04X ", addr & 0xffff);

	type_opcode_mode mode = opcode_mode[mode_lut[mem.b[0]]];
	sprintf(s, " %10s:%d ", mode.name, mode.val);
	strcat(str, s);

	// Opcode and arguments
	sprintf(s, "%02X ", mem.b[0]);
	strcat(str, s);

	last_bytecount = mode.val + 1;

	if (last_bytecount == 1)
	{
		strcat(str, "      ");
	}
	if (last_bytecount == 2)
	{
		sprintf(s, "%02X    ", mem.b[1]);
		strcat(str, s);
	}
	if (last_bytecount == 3)
	{
		sprintf(s, "%02X %02X ", mem.b[1], mem.b[2]);
		strcat(str, s);
	}

	// Instruction name
	strcat(str, instruction_lut[mem.b[0]]);

	switch(mode_lut[mem.b[0]])
	{
		case M_impl: break;
		case M_InnX:
			sprintf(s, " ($%02X,X)", mem.b[1]);
			strcat(str, s);
			break;
		case M_nn:
			sprintf(s, " $%02X", mem.b[1]);
			strcat(str, s);
			break;
		case M_immnn:
			sprintf(s, " #$%02X", mem.b[1]);
			strcat(str, s);
			break;
		case M_A: break;
		case M_nnnn:
			sprintf(s, " $%02X%02X", mem.b[2], mem.b[1]);
			strcat(str, s);
			break;
		case M_nnrr:
			sprintf(s, " $%02X,$%04X", mem.b[1], (addr + 3 + mem.b[2]) );
			strcat(str, s);
			break;
		case M_rr:
			if (mem.b[1] & 0x80)
				sprintf(s, " $%04X", (addr + 2 - 256 + mem.b[1]) );
			else
				sprintf(s, " $%04X", (addr + 2 + mem.b[1]) );
			strcat(str, s);
			break;
		case M_InnY:
			sprintf(s, " ($%02X),Y", mem.b[1]);
			strcat(str, s);
			break;
		case M_InnZ:
			sprintf(s, " ($%02X),Z", mem.b[1]);
			strcat(str, s);
			break;
		case M_rrrr:
			sprintf(s, " $%04X", (addr + 2 + (mem.b[2] << 8) + mem.b[1]) & 0xffff );
			strcat(str, s);
			break;
		case M_nnX:
			sprintf(s, " $%02X,X", mem.b[1]);
			strcat(str, s);
			break;
		case M_nnnnY:
			sprintf(s, " $%02X%02X,Y", mem.b[2], mem.b[1]);
			strcat(str, s);
			break;
		case M_nnnnX:
			sprintf(s, " $%02X%02X,X", mem.b[2], mem.b[1]);
			strcat(str, s);
			break;
		case M_Innnn:
			sprintf(s, " ($%02X%02X)", mem.b[2], mem.b[1]);
			strcat(str, s);
			break;
		case M_InnnnX:
			sprintf(s, " ($%02X%02X,X)", mem.b[2], mem.b[1]);
			strcat(str, s);
			break;
		case M_InnSPY:
			sprintf(s, " ($%02X,SP),Y", mem.b[1]);
			strcat(str, s);
			break;
		case M_nnY:
			sprintf(s, " $%02X,Y", mem.b[1]);
			strcat(str, s);
			break;
		case M_immnnnn:
			sprintf(s, " #$%02X%02X", mem.b[2], mem.b[1]);
			strcat(str, s);
			break;
	}

  return last_bytecount;
}

int* get_backtrace_addresses(void)
{
	// get current register values
	reg_data reg = get_regs();

  static int addresses[8];

	// get memory at current pc
	mem_data mem = get_mem(reg.sp+1);
	for (int k = 0; k < 8; k++)
	{
	  int addr = mem.b[k*2] + (mem.b[k*2+1] << 8);
		addr -= 2;
    addresses[k] = addr;
	}

	return addresses;
}

void cmdDisassemble(void)
{
  char str[128] = { 0 };
  int last_bytecount = 0;

  if (autowatch)
	  cmdWatches();

  int addr;
	int cnt = 1; // number of lines to disassemble

	// get current register values
	reg_data reg = get_regs();

	// get address from parameter?
  char* token = strtok(NULL, " ");
  
  if (token != NULL)
	{
	  if (strcmp(token, "-") == 0) // '-' equates to current pc
		{
      // get current register values
  		addr = reg.pc;
		}
		else
      addr = get_sym_value(token);

		token = strtok(NULL, " ");

		if (token != NULL)
		{
		  cnt = get_sym_value(token);
		}
	}
	// default to current pc
	else
	{
		addr = reg.pc;
  }

  // are we in a different frame?
  if (addr == reg.pc && traceframe != 0)
	{
	  int* addresses = get_backtrace_addresses();
		addr = addresses[traceframe-1];

		printf("<<< FRAME#: %d >>>\n", traceframe);
	}

  int idx = 0;

	while (idx < cnt)
	{
    last_bytecount = disassemble_addr_into_string(str, addr);

    // print from .list ref? (i.e., find source in .a65 file?)
    if (idx == 0)
		{
			type_fileloc *found = find_in_list(addr);
			if (found)
			{
				printf("> %s:%d\n", found->file, found->lineno);
				show_location(found);
        printf("---------------------------------------\n");
			}
		}

		// just print the raw disassembly line
		if (cnt != 1 && idx == 0)
			printf("%s%s%s\n", KINV, str, KNRM);
		else
			printf("%s\n", str);

		if (ctrlcflag)
			break;

    addr += last_bytecount;
		idx++;
	} // end while
}

void cmdStep(void)
{
  traceframe = 0;

  // just send an enter command
  serialWrite("\n");
  serialRead(inbuf, BUFSIZE);

  if (outputFlag)
	{
		if (autocls)
			cmdClearScreen();
		printf("%s", inbuf);
		cmdDisassemble();
	}
}

void cmdNext(void)
{
  traceframe = 0;

	if (autocls)
		cmdClearScreen();

  // check if this is a JSR command
  reg_data reg = get_regs();
	mem_data mem = get_mem(reg.pc);
		
	// if not, then just do a normal step
	if (strcmp(instruction_lut[mem.b[0]], "JSR") != 0)
	{
		cmdStep();
	}
	else
	{
		// if it is JSR, then keep doing step into until it returns to the next command after the JSR

		type_opcode_mode mode = opcode_mode[mode_lut[mem.b[0]]];
		int last_bytecount = mode.val + 1;
		int next_addr = reg.pc + last_bytecount;

		while (reg.pc != next_addr)
		{
			// just send an enter command
			serialWrite("\n");
			serialRead(inbuf, BUFSIZE);

			reg = get_regs();

			if (ctrlcflag)
				break;
		}

    // show disassembly of current position
		serialWrite("r\n");
		serialRead(inbuf, BUFSIZE);
		if (outputFlag)
		{
			if (autocls)
				cmdClearScreen();
			printf("%s", inbuf);
			cmdDisassemble();
		}
	}
}


void cmdFinish(void)
{
  traceframe = 0;

  reg_data reg = get_regs();

  int cur_sp = reg.sp;
	bool function_returning = false;

  //outputFlag = false;
  while (!function_returning)
	{
	  reg = get_regs();
		mem_data mem = get_mem(reg.pc);

		if (strcmp(instruction_lut[mem.b[0]], "RTS") == 0
				&& reg.sp == cur_sp)
			function_returning = true;

    cmdClearScreen();
		cmdNext();

		if (ctrlcflag)
			break;
	}
	//outputFlag = true;
	cmdDisassemble();
}

// check symbol-map for value. If not found there, just return
// the hex-value of the string
int get_sym_value(char* token)
{
  int addr = 0;

	type_symmap_entry* sme = find_in_symmap(token);
	if (sme != NULL)
	{
		return sme->addr;
	}
	else
	{
    sscanf(token, "%X", &addr);
    return addr;
	}
}

void print_byte(char *token)
{
	int addr = get_sym_value(token);

	mem_data mem = get_mem(addr);

	printf(" %s: %02X\n", token, mem.b[0]);
}

void cmdPrintByte(void)
{
  char* token = strtok(NULL, " ");
  
  if (token != NULL)
  {
	  print_byte(token);
  }
}

void print_word(char* token)
{
	int addr = get_sym_value(token);

	mem_data mem = get_mem(addr);

	printf(" %s: %02X%02X\n", token, mem.b[1], mem.b[0]);
}

void cmdPrintWord(void)
{
  char* token = strtok(NULL, " ");
  
  if (token != NULL)
  {
	  print_word(token);
  }
}

void print_dword(char* token)
{
	int addr = get_sym_value(token);

	mem_data mem = get_mem(addr);

	printf(" %s: %02X%02X%02X%02X\n", token, mem.b[3], mem.b[2], mem.b[1], mem.b[0]);
}

void cmdPrintDWord(void)
{
  char* token = strtok(NULL, " ");
  
  if (token != NULL)
  {
	  print_dword(token);
  }
}

void print_string(char* token)
{
	int addr = get_sym_value(token);
	static char string[2048] = { 0 };
	char c[2] = { 0 };

	int cnt = 0;
	string[0] = '\0';

	while (1)
	{
		mem_data mem = get_mem(addr+cnt);

		for (int k = 0; k < 16; k++)
		{
			c[0] = mem.b[k];
			strcat(string, c);
			if (mem.b[k] == 0)
			{
				printf(" %s: %s\n", token, string);
				return;
			}
			cnt++;
		}
	}
}

void cmdPrintString(void)
{
  char* token = strtok(NULL, " ");
  
  if (token != NULL)
  {
	  print_string(token);
  }
}

void cmdClearScreen(void)
{
  printf("%s%s", KCLEAR, KPOS0_0);
}

void cmdAutoClearScreen(void)
{
  char* token = strtok(NULL, " ");

  // if no parameter, then just toggle it
	if (token == NULL)
		autocls = !autocls;
	else if (strcmp(token, "1") == 0)
		autocls = true;
	else if (strcmp(token, "0") == 0)
		autocls = false;
	
	printf(" - autocls is turned %s.\n", autocls ? "on" : "off");
}

void cmdSetBreakpoint(void)
{
  char* token = strtok(NULL, " ");
  char str[100];
  
  if (token != NULL)
  {
		int addr = get_sym_value(token);
		printf("- Setting hardware breakpoint to $%04X\n", addr);

    sprintf(str, "b%04X\n", addr);
    serialWrite(str);
    serialRead(inbuf, BUFSIZE);
	}
}

void cmd_watch(type_watch type)
{
  char* token = strtok(NULL, " ");
  
  if (token != NULL)
  {
	  if (find_in_watchlist(token))
		{
		  printf("watch already exists!\n");
			return;
		}

	  type_watch_entry we;
		we.type = type;
		we.name = token;
		add_to_watchlist(we);

		printf("watch added!\n");
	}
}

void cmdWatchByte(void)
{
  cmd_watch(TYPE_BYTE);
}

void cmdWatchWord(void)
{
  cmd_watch(TYPE_WORD);
}

void cmdWatchDWord(void)
{
  cmd_watch(TYPE_DWORD);
}

void cmdWatchString(void)
{
  cmd_watch(TYPE_STRING);
}

void cmdWatches(void)
{
  type_watch_entry* iter = lstWatches;
	int cnt = 0;

  printf("---------------------------------------\n");
	
	while (iter != NULL)
	{
	  cnt++;

		printf("#%d: %s ", cnt, type_names[iter->type]);

		switch (iter->type)
		{
		  case TYPE_BYTE:   print_byte(iter->name);   break;
		  case TYPE_WORD:   print_word(iter->name);   break;
		  case TYPE_DWORD:  print_dword(iter->name);  break;
		  case TYPE_STRING: print_string(iter->name); break;
		}

		iter = iter->next;
	}

	if (cnt == 0)
	  printf("no watches in list\n");
  printf("---------------------------------------\n");
}

void cmdDeleteWatch(void)
{
  char* token = strtok(NULL, " ");
  
  if (token != NULL)
  {
	  // user wants to delete all watches?
	  if (strcmp(token, "all") == 0)
		{
		  // TODO: add a confirm yes/no prompt...
			outputFlag = false;
			while (delete_from_watchlist(1))
			  ;
			outputFlag = true;
			printf("deleted all watches!\n");
		}
		else
		{
		  int wnum;
		  int n = sscanf(token, "%d", &wnum);

			if (n == 1)
			{
			  delete_from_watchlist(wnum);
			}
		}
	}
}


void cmdAutoWatch(void)
{
  char* token = strtok(NULL, " ");

  // if no parameter, then just toggle it
	if (token == NULL)
		autowatch = !autowatch;
	else if (strcmp(token, "1") == 0)
		autowatch = true;
	else if (strcmp(token, "0") == 0)
		autowatch = false;
	
	printf(" - autowatch is turned %s.\n", autowatch ? "on" : "off");
}

void cmdSymbolValue(void)
{
  char* token = strtok(NULL, " ");
  
  if (token != NULL)
  {
    type_symmap_entry* sme = find_in_symmap(token);

		if (sme != NULL)
		  printf("%s : %s\n", sme->sval, sme->symbol);
	}
}

void cmdSave(void)
{
  char* strBinFile = strtok(NULL, " ");

  if (!strBinFile)
  {
    printf("Missing <binfile> parameter!\n");
    return;
  }

	char* strAddr = strtok(NULL, " ");
	if (!strAddr)
	{
	  printf("Missing <addr> parameter!\n");
		return;
	}

  char* strCount = strtok(NULL, " ");
  if (!strCount)
  {
    printf("Missing <count> parameter!\n");
    return;
  }

	int addr = get_sym_value(strAddr);
  int count;
  sscanf(strCount, "%X", &count);

  int cnt = 0;
  FILE* fsave = fopen(strBinFile, "wb");
	while (cnt < count)
	{
		// get memory at current pc
		mem_data* multimem = get_mem28array(addr + cnt);

    for (int line = 0; line < 32; line++)
    {
      mem_data* mem = &multimem[line];

      for (int k = 0; k < 16; k++)
      {
        fputc(mem->b[k], fsave);

        cnt++;

        if (cnt >= count)
          break;
      }

      printf("0x%X bytes saved...\r", cnt);
      if (cnt >= count)
        break;
    }

		if (ctrlcflag)
			break;
	}

  printf("\n0x%X bytes saved to \"%s\"\n", cnt, strBinFile);
  fclose(fsave);
}

void cmdLoad(void)
{
	char* strBinFile = strtok(NULL, " ");

	if (!strBinFile)
	{
		printf("Missing <binfile> parameter!\n");
		return;
	}

	char* strAddr = strtok(NULL, " ");
	if (!strAddr)
	{
		printf("Missing <addr> parameter!\n");
		return;
	}

	int addr = get_sym_value(strAddr);

  	FILE* fload = fopen(strBinFile, "rb");
	if(fload)
	{
		fseek(fload, 0, SEEK_END);
		int fsize = ftell(fload);	
		rewind(fload);      		
		char* buffer = (char *)malloc(fsize*sizeof(char));
		if(buffer) 
		{
			fread(buffer, fsize, 1, fload);
		
			int i = 0;
			while(i < fsize)
			{
				int outSize = fsize - i;
				if(outSize > 16) {
					outSize = 16;
				}

				put_mem28array(addr + i, (unsigned char*) (buffer + i), outSize);
				i += outSize;
			}	

			free(buffer);
		}
  		fclose(fload);
	}
	else 
	{
		printf("Error opening the file '%s'!\n", strBinFile);
	}
}

void cmdBackTrace(void)
{
  char str[128] = { 0 };

	// get current register values
	reg_data reg = get_regs();

	disassemble_addr_into_string(str, reg.pc);
	if (traceframe == 0)
		printf(KINV "#0: %s\n" KNRM, str);
	else
		printf("#0: %s\n", str);

	// get memory at current pc
	int* addresses = get_backtrace_addresses();

	for (int k = 0; k < 8; k++)
	{
	  disassemble_addr_into_string(str, addresses[k]);
		if (traceframe-1 == k)
		  printf(KINV "#%d: %s\n" KNRM, k+1, str);
		else
			printf("#%d: %s\n", k+1, str);
	}
}

void cmdUpFrame(void)
{
  if (traceframe == 0)
	{
	  printf("Already at highest frame! (frame#0)\n");
		return;
	}

	traceframe--;

  if (autocls)
		cmdClearScreen();
	cmdDisassemble();
}

void cmdDownFrame(void)
{
  if (traceframe == 8)
	{
	  printf("Already at lowest frame! (frame#8)\n");
		return;
	}

	traceframe++;

  if (autocls)
		cmdClearScreen();
	cmdDisassemble();
}
