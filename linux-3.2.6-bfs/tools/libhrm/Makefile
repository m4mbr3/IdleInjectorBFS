.PHONY: libhrm.a producer consumer

all: libhrm.a

libhrm.a: hrm.h hrm.c
	gcc -std=gnu99 -pedantic -Wall -Wextra -c -o hrm.o hrm.c -I.
	ar crs libhrm.a hrm.o
	ranlib libhrm.a

dist: producer consumer

producer: producer.c libhrm.a
	gcc -std=gnu99 -pedantic -Wall -Wextra -ggdb -o producer producer.c -I. -L. -lhrm -lpthread -lrt

consumer: consumer.c libhrm.a
	gcc -std=gnu99 -pedantic -Wall -Wextra -ggdb -o consumer consumer.c -I. -L. -lhrm

clean:
	rm -f hrm.o libhrm.a config.h

distclean: clean
	rm -f producer consumer

