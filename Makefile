LIB = libmfs.a

CC = gcc
AR = ar
RANLIB = ranlib
CFLAGS = -arch i386 -arch ppc -arch x86_64 -fPIC -std=c99 -I.. -DUSE_LIBRES

all: $(LIB)

$(LIB): mfs.c
	$(CC) -c $(CFLAGS) mfs.c
	$(AR) -ru $(LIB) mfs.o
	$(RANLIB) $(LIB)

clean:
	rm -rf libmfs.a mfs.o