// offer the same functionality with dlopen, but with an exact load address

#include "library.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h> //TODO: use small inline functions to clobber those heavy libc implementations

extern void *mapLibrary(const char *name, void *addr);
extern void relocLibrary(Library *l, int mode);

//a decent dynamic linker should prevent user from opening twice
//in ld.so there is a global scope and a local scope, and I only maintain a scope when mapping
/* 
Library *openList = NULL;
static void* isLibraryOpen(const char *name)
{
    Library *search = openList;
    while (search)
    {
        if(strcmp(name, search->name) == 0)
            return search;
        search = search->next;
    }
    return NULL;
} 
*/

void *openLibrary(const char *name, int mode, void *addr)
{
    Library *new = mapLibrary(name, addr); //map a shared object and its dependencies
    relocLibrary(new, mode); //relocate a shared object and its dependencies


    return new;
}
