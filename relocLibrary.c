//relocate shared object so that the symbols no longer hold a PIC address
#include "library.h"
#include <dlfcn.h> //turn to dlsym for help at fake load object
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <elf.h>
#include <link.h> //TODO: get rid of this later
#include <string.h>

// glibc version to hash a symbol
static uint_fast32_t
dl_new_hash(const char *s)
{
    uint_fast32_t h = 5381;
    for (unsigned char c = *s; c != '\0'; c = *++s)
        h = h * 33 + c;
    return h & 0xffffffff;
}

static void *symbolLookup(Library *dep, const char *name)
{
    //find symbol `name` inside the symbol table of `dep`
    if(dep->fake)
    {
        void *handle = dlopen(dep->name, RTLD_LAZY);
        if(!handle)
        {
            fprintf(stderr, "relocLibrary error: cannot dlopen a fake object named %s", dep->name);
            exit(-1);
        }
        dep->fake_handle = handle;
        return dlsym(handle, name);
    }

    Elf64_Sym *symtab = (Elf64_Sym *)dep->dyn_info[DT_SYMTAB]->d_un.d_ptr;
    const char *strtab = (const char *)dep->dyn_info[DT_STRTAB]->d_un.d_ptr;

    uint_fast32_t new_hash = dl_new_hash(name);
    Elf64_Sym *sym;
    const Elf64_Addr *bitmask = dep->l_gnu_bitmask;
    uint32_t symidx;
    Elf64_Addr bitmask_word = bitmask[(new_hash / __ELF_NATIVE_CLASS) & dep->l_gnu_bitmask_idxbits];
    unsigned int hashbit1 = new_hash & (__ELF_NATIVE_CLASS - 1);
    unsigned int hashbit2 = ((new_hash >> dep->l_gnu_shift) & (__ELF_NATIVE_CLASS - 1));
    if ((bitmask_word >> hashbit1) & (bitmask_word >> hashbit2) & 1)
    {
        Elf32_Word bucket = dep->l_gnu_buckets[new_hash % dep->l_nbuckets];
        if (bucket != 0)
        {
            const Elf32_Word *hasharr = &dep->l_gnu_chain_zero[bucket];
            do
            {
                if (((*hasharr ^ new_hash) >> 1) == 0)
                {
                    symidx = hasharr - dep->l_gnu_chain_zero;
                    /* now, symtab[symidx] is the current symbol
                        hash table has done all work and can be stripped */
                    const char *symname = strtab + symtab[symidx].st_name;
                    /* FIXME: You may also want to check the visibility and strong/weak of the found symbol
                        but... not now */
                    /* FIXME: Please make sure no local symbols like "tmp" will be accessed here! */
                    if (!strcmp(symname, name))
                    {    
                        Elf64_Sym *s = &symtab[symidx];
                        return (void *)(s->st_value + dep->addr);
                    }
                }
            } while ((*hasharr++ & 1u) == 0);
        }
    }
    return NULL; //not this dependency
}

void relocRela(Library *lib)
{
    //use `readelf --relocs` to see '.rela.dyn'
    //this includes relative and glob_dat
    Elf64_Addr start = lib->dyn_info[DT_RELA]->d_un.d_ptr;
    Elf64_Addr size = lib->dyn_info[DT_RELASZ]->d_un.d_val;
    Elf64_Xword nrelative = lib->dyn_info[DT_NUM + DT_RELACOUNT_NEW]->d_un.d_val;

    Elf64_Rela *r_start = (void *)start;
    Elf64_Rela *r_end = r_start + nrelative; //relative_end
    //fill in all relative relocs here
    for(Elf64_Rela *it = r_start; it < r_end; it++)
    {
        Elf64_Addr *tmp = (void *)(lib->addr + it->r_offset);
        *tmp = lib->addr + it->r_addend;
    }

    Elf64_Rela *rela_end = (void *)(start + size);
    Elf64_Sym *symtab = (Elf64_Sym *)lib->dyn_info[DT_SYMTAB]->d_un.d_ptr;
    const char *strtab = (const char *)lib->dyn_info[DT_STRTAB]->d_un.d_ptr;
    for(Elf64_Rela *it = r_end; it < rela_end; it++)
    {
        Elf64_Xword idx = it->r_info;
        Elf64_Sym *tmp_sym = &symtab[idx >> 32]; //from dynamic symbol table get the symbol
        Elf64_Word name = tmp_sym->st_name;
        const char *real_name = strtab + name;

        //do glob_dat, search in searchlist
        Library **search = lib->search_list;
        while (*search)
        {
            void *res = symbolLookup(*search, real_name);
            if(res)
            {
                void *dest = (void *)(lib->addr + it->r_offset);
                *(Elf64_Addr *)dest = (Elf64_Addr)res + it->r_addend;
                break;
            }
            search++;
        }
    }

}

void relocPLT(Library *lib, int mode)
{
    //use `readelf --relocs` to see '.rela.plt'
    Elf64_Addr start = lib->dyn_info[DT_JMPREL]->d_un.d_ptr;
    Elf64_Addr size = lib->dyn_info[DT_PLTRELSZ]->d_un.d_val;
    
    Elf64_Rela *plt_start = (void *)start;
    Elf64_Rela *plt_end = (void *)(start + size);
    Elf64_Sym *symtab = (Elf64_Sym *)lib->dyn_info[DT_SYMTAB]->d_un.d_ptr;
    const char *strtab = (const char *)lib->dyn_info[DT_STRTAB]->d_un.d_ptr;
    for(Elf64_Rela *it = plt_start; it < plt_end; it++)
    {
        Elf64_Xword idx = it->r_info;
        Elf64_Sym *tmp_sym = &symtab[idx >> 32]; //from dynamic symbol table get the symbol
        Elf64_Word name = tmp_sym->st_name;
        const char *real_name = strtab + name;

        //deal with GNU IFUNC. I don't think it will be used if you mark libc.so.6 as fake
        const unsigned long int r_type = it->r_info & 0xffffffff;
        if (r_type == R_X86_64_IRELATIVE)
        {
            Elf64_Addr value = lib->addr + it->r_addend;
            //because it's IFUNC, the true address of the symbol is the address IFUNC resolver pointing to
            value = ((Elf64_Addr(*)(void))value)();
            void *dest = (void *)(lib->addr + it->r_offset);
            *(Elf64_Addr *)dest = value;
            continue;
        }
        Library **search = lib->search_list;
        while (*search)
        {
            void *res = symbolLookup(*search, real_name);
            if(res)
            {
                void *dest = (void *)(lib->addr + it->r_offset);
                *(Elf64_Addr *)dest = (Elf64_Addr)res + it->r_addend;
                break;
            }
            search++;
        }
    }
}

void relocLibrary(Library *lib, int mode)
{
    if(lib->fake)
        return; //no point in relocating a fake object
    relocRela(lib);
    relocPLT(lib, mode);
}