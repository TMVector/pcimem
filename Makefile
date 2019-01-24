
CC ?= gcc
CFLAGS ?= -Wall -march=native -O3

main: pcimem libpcimem.so

libpcimem.so: libpcimem.c
	$(CC) $(CFLAGS) -fPIC -shared $^ -o $@

clean:
	-rm -f *.o *~ core pcimem libpcimem.so

