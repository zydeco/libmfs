LIB = libmfs.a

CC = gcc
#CC = arm-apple-darwin9-gcc
AR = ar
CFLAGS = -std=c99 -I.. -DUSE_LIBRES
#CFLAGS = -std=c99 -I.. -march=armv6 -mcpu=arm1176jzf-s

all: $(LIB)

$(LIB): mfs.c
	$(CC) -c $(CFLAGS) mfs.c
	$(AR) -ru $(LIB) mfs.o

clean:
	rm -rf libmfs.a mfs.o