all: mapLibrary.o openLibrary.o relocLibrary.o findSymbol.o
	gcc -shared -fPIC -o libredl.so -g mapLibrary.o openLibrary.o relocLibrary.o findSymbol.o -ldl

mapLibrary.o: mapLibrary.c
	gcc -fPIC -g -c mapLibrary.c

openLibrary.o: openLibrary.c
	gcc -fPIC -g -c openLibrary.c

relocLibrary.o: relocLibrary.c
	gcc -fPIC -g -c relocLibrary.c

findSymbol.o: findSymbol.c
	gcc -fPIC -g -c findSymbol.c

clean:
	rm *.o *.so
