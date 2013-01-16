UNAME := $(shell uname -s)

CC=gcc
CFLAGS=-c -Wall -Os -std=c99 -I -fnested-functions -fms-extensions -DDEBUG -DCLI
LDFLAGS=-Wl --gc-sections
SOURCES=vm.c struct.c serial.c compile.c util.c sys.c variable.c interpret.c hal_stub.c
OBJECTS=$(SOURCES:.c=.o)
EXECUTABLE=filagree

all: $(SOURCES) $(EXECUTABLE)
	
$(EXECUTABLE): $(OBJECTS) 
	$(CC) $(LDFLAGS) $(OBJECTS) -o $@
	strip $(EXECUTABLE)

.c.o:
	$(CC) $(CFLAGS) $< -o $@

clean:
	rm -f $(OBJECTS) $(EXECUTABLE) 
