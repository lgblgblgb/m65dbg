# Refreshed my memory on how to write makefiles via this site:
# - http://mrbook.org/blog/tutorials/make/

# Add some logic to detect cygwin
TEST:=$(shell test -d /cygdrive && echo cygwin)
ifneq "$(TEST)" ""
	LDFLAGS=-L/usr/bin -lreadline7
else
	LDFLAGS=-lreadline
endif

CC		= gcc
CFLAGS		= -c -Wall -g -std=c99
SOURCES		= main.c serial.c commands.c gs4510.c
OBJECTS		= $(SOURCES:.c=.o)
EXECUTABLE	= m65dbg

all: $(EXECUTABLE)

$(EXECUTABLE): $(OBJECTS)
	$(CC) $(OBJECTS) $(LDFLAGS) -o $@

.c.o:
	$(CC) $(CFLAGS) $< -o $@

clean:
	rm -f $(OBJECTS) $(EXECUTABLE)
