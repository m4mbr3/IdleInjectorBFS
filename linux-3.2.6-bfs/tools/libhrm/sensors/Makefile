all:  templog sensors

templog:
	gcc -lsensors -lrt -o templog main2.c chips.c

sensors:
	gcc -lsensors -lrt -o sensors main.c chips.c

clean:
	rm sensors templog
