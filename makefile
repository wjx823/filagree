UNAME := $(shell uname -s)

CC=gcc
CFLAGS=-c -Wall -Os -std=gnu99 -I -fnested-functions -fms-extensions -DCLI -DDEBUG
LDFLAGS=-lm -lcyassl -lpthread
SOURCES=vm.c struct.c serial.c compile.c util.c sys.c variable.c interpret.c hal_stub.c node.c
OBJECTS=$(SOURCES:.c=.o)
EXECUTABLE=filagree

all: $(SOURCES) $(EXECUTABLE)
	
$(EXECUTABLE): $(OBJECTS) 
	$(CC) $(OBJECTS) -o $@ $(LDFLAGS)
	strip $(EXECUTABLE)

.c.o:
	$(CC) $(CFLAGS) $< -o $@

clean:
	rm -f $(OBJECTS) $(EXECUTABLE) 
