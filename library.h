//minimum information needed by the runtime dynamic linker, add something here when it's necessary
#ifndef LIBRARY_INTERNALS
#define LIBRARY_INTERNALS

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <elf.h>

//in glibc there is a cluster of rules to map these OS-specific flags into array indice
//Here I make it simple by appending the flags I need after DT_NUM
#define OS_SPECIFIC_FLAG 2
#define DT_RELACOUNT_NEW 0
#define DT_GNU_HASH_NEW 1

typedef struct libraryInternal
{
    uint64_t addr;
    char *name;
    Elf64_Dyn *dyn;
    Elf64_Dyn *dyn_info[DT_NUM + OS_SPECIFIC_FLAG];
    int dyn_num;
    struct libraryInternal **search_list;
    struct libraryInternal *next;
    FILE *fs;
    int relocated;
    int fake; // this is a currently unresolvable bug: some .so like libc, 
    //I can't map it correctly, so I just borrow dlopen, hopefully I can solve it later
    //see: https://sourceware.org/pipermail/libc-help/2021-January/005615.html
    void *fake_handle; //after fake search, use this handle to dl-close it

    /* symbol lookup thing borrowed from ld.so */
    uint32_t l_nbuckets;
    Elf32_Word l_gnu_bitmask_idxbits;
    Elf32_Word l_gnu_shift;
    const Elf64_Addr *l_gnu_bitmask;
    const Elf32_Word *l_gnu_buckets;
    const Elf32_Word *l_gnu_chain_zero;

} Library;


#endif