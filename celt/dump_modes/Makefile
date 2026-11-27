
CFLAGS=-O2 -Wall -Wextra -DHAVE_CONFIG_H
INCLUDES=-I. -I../ -I../.. -I../../include

SOURCES = dump_modes.c \
          ../modes.c \
          ../cwrs.c \
          ../rate.c \
          ../entcode.c \
          ../entenc.c \
          ../entdec.c \
          ../mathops.c \
          ../mdct.c \
          ../celt.c \
          ../kiss_fft.c


all: dump_modes

dump_modes:
	$(PREFIX)$(CC) $(CFLAGS) $(INCLUDES) -DCUSTOM_MODES_ONLY -DCUSTOM_MODES $(SOURCES) -o $@ $(LIBS) -lm

clean:
	rm -f dump_modes
