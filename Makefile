CC = gcc
CFLAGS = -I.
DEPS =

CFLAGS += $(shell pkg-config --cflags gtk+-3.0)
CFLAGS += $(shell pkg-config --cflags vte-2.91)
CFLAGS += $(shell pkg-config --cflags glib-2.0)
CFLAGS += $(shell pkg-config --cflags pango)

LDFLAGS += $(shell pkg-config --libs gtk+-3.0)
LDFLAGS += $(shell pkg-config --libs vte-2.91)
LDFLAGS += $(shell pkg-config --libs glib-2.0)
LDFLAGS += $(shell pkg-config --libs pango)
LDFLAGS += "-lutil" #openpty

all: z0term

%.o: %.c $(DEPS)
	$(CC) -c -g -o $@ $< $(CFLAGS)

z0term: z0term.o
	$(CC) $(CFLAGS) $(LDFLAGS) $< -o $@

clean:
	rm *.o
	rm z0term
