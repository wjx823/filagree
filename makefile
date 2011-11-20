UNAME := $(shell uname -s)

CC=gcc
CFLAGS=-c -Wall -O0 -std=c99 -I -fnested-functions -fms-extensions -DTEST -DFILE_RW -DDEBUG
LDFLAGS=
SOURCES=vm.c struct.c serial.c compile.c test.c util.c
OBJECTS=$(SOURCES:.c=.o)
EXECUTABLE=interpret

all: $(SOURCES) $(EXECUTABLE)
	
$(EXECUTABLE): $(OBJECTS) 
	$(CC) $(LDFLAGS) $(OBJECTS) -o $@
	strip $(EXECUTABLE)

.c.o:
	$(CC) $(CFLAGS) $< -o $@

clean:
	rm -f $(OBJECTS) $(EXECUTABLE) 
