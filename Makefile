all: mapLibrary.o openLibrary.o relocLibrary.o findSymbol.o runtimeResolve.o trampoline.o
	gcc -shared -fPIC -o libredl.so -g mapLibrary.o openLibrary.o relocLibrary.o findSymbol.o runtimeResolve.o trampoline.o -ldl

mapLibrary.o: mapLibrary.c
	gcc -fPIC -g -c mapLibrary.c

openLibrary.o: openLibrary.c
	gcc -fPIC -g -c openLibrary.c

relocLibrary.o: relocLibrary.c
	gcc -fPIC -g -c relocLibrary.c

findSymbol.o: findSymbol.c
	gcc -fPIC -g -c findSymbol.c

runtimeResolve.o: runtimeResolve.c
	gcc -fPIC -g -c runtimeResolve.c

trampoline.o: trampoline.S
	gcc -fPIC -g -c trampoline.S

clean:
	rm *.o *.so
