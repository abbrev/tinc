CFLAGS=-Wall `pkg-config --cflags ticables2`
LDFLAGS=`pkg-config --libs ticables2`

all: tinc

.PHONY: clean
clean:
	rm -f *.o tinc
