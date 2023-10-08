all: disk.o fs.o

disk.o: disk.c disk.h

fs.o: fs.c fs.h

clean:
	rm -f disk.o fs.o