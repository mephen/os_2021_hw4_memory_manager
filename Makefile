CC := gcc
override CFLAGS += -O3 -Wall

SOURCE := MemManager.c
BINARY := MemManager

$(BINARY): $(SOURCE) $(patsubst %.c, %.h, $(SOURCE))
	$(CC) $(CFLAGS) $< -o $@ -lm

.PHONY: clean
clean:
	rm -f *.o $(BINARY)