//find the address of a symbol
#include "library.h"
#include <elf.h>
#include <stdlib.h>
#include <string.h>

void *findSymbol(void *library, const char *symname)
{
    Library *lib = library;
    Elf64_Sym *symtab = (void *)lib->dyn_info[DT_SYMTAB]->d_un.d_ptr;
    const char *strtab = (void *)lib->dyn_info[DT_STRTAB]->d_un.d_ptr;
    
    Elf64_Sym *curr = symtab;
    //TODO: plz utilize the hash table... this is dumb
    //this works because strtab and symtab are adjacent
    while((void *)curr < (void *)strtab)
    {
        Elf64_Word name = curr->st_name;
        if(strcmp(symname, strtab + name) == 0)
        {
            //we have a match
            return (void *)(curr->st_value + lib->addr);
        }
        ++curr;
    }
    return NULL;
}