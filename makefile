UNAME := $(shell uname -s)

CC=gcc
CFLAGS=-c -Wall -Os -std=c99 -I -fnested-functions -fms-extensions -DTEST -DFILE_RW -DDEBUG -ffunction-sections -fdata-sections
LDFLAGS=-Wl --gc-sections
SOURCES=vm.c struct.c serial.c compile.c test.c util.c sys.c
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
