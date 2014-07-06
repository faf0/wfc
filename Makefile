CC = g++
CPP = g++
CPPFLAGS += -I./ -Wall -pedantic -D_SVID_SOURCE
#CFLAGS += -std=c99 -O3
CXXFLAGS += -O3
LDFLAGS  += -L./
#LOADLIBES = -lm

.PHONY: all, clean

PROGNAME := wfc 

all:	wfc
wfc:	wfc.o

clean:
	rm *.o

