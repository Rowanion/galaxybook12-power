CC ?= cc
CFLAGS ?= -O2 -pipe
CFLAGS += -std=c11 -Wall -Wextra -Wpedantic -D_GNU_SOURCE
LDLIBS += -lm

TARGET := galaxybook12-brightness
SOURCES := src/main.c src/dpAux.c src/brightness.c src/tables.c

.PHONY: all clean check

all: $(TARGET)

$(TARGET): $(SOURCES) src/dpAux.h src/brightness.h src/tables.h
	$(CC) $(CFLAGS) -o $@ $(SOURCES) $(LDFLAGS) $(LDLIBS)

check: $(TARGET)
	./$(TARGET) check

clean:
	rm -f $(TARGET)
