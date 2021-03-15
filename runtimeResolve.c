//do the lazy bind of PLT
#include "library.h"
#include <elf.h>
#include <stdlib.h>
#include <stdio.h>

extern void *symbolLookup(Library *dep, const char *name);

Elf64_Addr __attribute__((visibility ("hidden"))) //this makes trampoline to call it w/o plt
runtimeResolve(Library *lib, Elf64_Word reloc_entry)
{
    //fill the address of PLT entry `reloc_entry` in Library `lib`
    const char *strtab = (void *)lib->dyn_info[DT_STRTAB]->d_un.d_ptr;
    Elf64_Sym *symtab = (void *)lib->dyn_info[DT_SYMTAB]->d_un.d_ptr;
    Elf64_Rela *plt_start = (void *)lib->dyn_info[DT_JMPREL]->d_un.d_ptr;
    Elf64_Rela *reloc_obj = plt_start + reloc_entry;
    Elf64_Xword idx = reloc_obj->r_info;
    Elf64_Sym *tmp_sym = &symtab[idx >> 32];
    Elf64_Word name = tmp_sym->st_name;
    const char *real_name = strtab + name; //finally, the name of the reloc entry

    Library **search = lib->search_list;
    void *res = NULL;
    while (*search)
    {
        res = symbolLookup(*search, real_name);
        if(res)
        {
            void *dest = (void *)(lib->addr + reloc_obj->r_offset);
            *(Elf64_Addr *)dest = (Elf64_Addr)res + reloc_obj->r_addend;
            fprintf(stderr, "runtimeResolve debug: lazy bind a symbol named %s\n", real_name);
            break;
        }
        search++;
    }
    if(!res)
    {
        fprintf(stderr, "runtimeResolve error: cannot resolve a PLT entry called %s in Library %s\n", real_name, lib->name);
        exit(-1);
    }
    return (Elf64_Addr)res + reloc_obj->r_addend;
}
