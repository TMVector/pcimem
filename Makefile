
CC ?= gcc
CFLAGS ?= -Wall -g

main: pcimem libpcimem.so

libpcimem.so: libpcimem.c
	$(CC) -Wall -fPIC -shared $^ -o $@

clean:
	-rm -f *.o *~ core pcimem libpcimem.so

