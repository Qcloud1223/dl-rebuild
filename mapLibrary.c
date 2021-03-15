//map the shared object into memory, and also generate a struct Library for it
#include "library.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <elf.h>
#include <unistd.h> //for getpagesize
#include <sys/mman.h>

#define ALIGN_DOWN(base, size) ((base) & -((__typeof__(base))(size)))
#define ALIGN_UP(base, size) ALIGN_DOWN((base) + (size)-1, (size))

static const char *sys_path[] = {
    "/usr/lib/x86_64-linux-gnu/",
    "/lib/x86_64-linux-gnu/",
    ""
};

static const char *fake_so[] = {
    "libc.so.6",
    "ld-linux.so.2",
    ""
};

//fill in an almost empty Library, and put its deps on
static uint64_t mapWorker(Library *lib, void *addr);

Library *openedHead = NULL;
Library *openedTail = NULL; //we need a tail for single library can put multiple deps on list
static void* isLibraryOpen(const char *name)
{
    Library *search = openedHead;
    while (search)
    {
        if(strcmp(name, search->name) == 0)
            return search;
        search = search->next;
    }
    return NULL;
}

static int doFakeLoad(const char *libname)
{
    for(const char **s = fake_so; *s; s++)
    {
        if(strcmp(*s, libname) == 0)
            return 1;
    }
    return 0;
}

static FILE *openFile(const char *name)
{
    // go for it if it's an absolute path
    if(strchr(name, '/')) return fopen(name, "rb");

    char tmp[128]; //temporary array for checking FILE availability
    *tmp = 0;
    for(const char **s = sys_path; *s; s++)
    {
        strcat(tmp, *s);
        strcat(tmp, name);
        FILE *curr = fopen(tmp, "rb");
        if(curr) return curr;
        *tmp = 0; //flush the array
    }
    return NULL;
}

static FILE *openDep(const char *name, const char *runpath)
{
    // we do one step more when open a so as a dependency
    if(runpath == NULL)
        return openFile(name);
    char *p, *last;
    FILE *curr;
    char *xpath = strdup(runpath);
    for((p = strtok_r(xpath, ":", &last)); p; p = strtok_r(NULL, ":", &last))
    {
        char tmp_name[128];
        *tmp_name = '\0';
        strcat(tmp_name, p);
        strcat(tmp_name, "/");
        strcat(tmp_name, name);
        curr = fopen(tmp_name, "rb");
        if(curr)
        {
            free(xpath);
            return curr;
        }
    }
    free(xpath);
    //maybe it's in system path?
    curr = openFile(name);
    if(curr) return curr;

    return NULL;
}

void *mapLibrary(const char *name, void *addr)
{
    // map a shared object and its dependencies compactly together 
    
    FILE *lib = openFile(name);
    if(!lib)
    {
        fprintf(stderr, "mapLibrary error: file %s not found.\n", name);
        exit(-1);
    }
    openedHead = calloc(sizeof(Library), 1);
    openedHead->fs = lib;
    //make name have a solid place, so if it depend on other lib, its name won't be freed when its dep is freed
    openedHead->name = strdup(name); 
    openedTail = openedHead;

    Library *curr = openedHead;
    uint64_t curr_addr = (uint64_t)addr;
    while(curr != NULL)
    {
        curr_addr += mapWorker(curr, (void *)curr_addr);
        curr = curr->next;
    }

    //now life is sane, we've finished building the shared object and its deps as a whole chain
    //with the head pointer returned, we can traverse this chain later
    return openedHead;
}

/* struct to store PT_LOAD info */
struct loadcmd
{
    Elf64_Addr mapstart, mapend, dataend, allocend;
    Elf64_Off mapoff;
    int prot; /* PROT_* bits.  */
};

static uint64_t mapSegment(Library *lib, Elf64_Phdr *phdr, void *addr, uint16_t phnum)
{
    struct loadcmd loadcmds[phnum]; //each segment could be a PT_LOAD
    int nloadcmd = 0;
    int pagesize = getpagesize();
    for(Elf64_Phdr *ph = phdr; ph < &phdr[phnum]; ph++)
    {
        switch (ph->p_type)
        {
        case PT_LOAD:
        {
            struct loadcmd *c = &loadcmds[nloadcmd++];
            c->mapstart = ALIGN_DOWN(ph->p_vaddr, pagesize);
            c->mapend = ALIGN_UP(ph->p_vaddr + ph->p_filesz, pagesize);
            c->dataend = ph->p_vaddr + ph->p_filesz;
            c->allocend = ph->p_vaddr + ph->p_memsz;
            c->mapoff = ALIGN_DOWN(ph->p_offset, pagesize);
            //TODO: add hole fixing
            c->prot = 0;
            c->prot |= (ph->p_flags & PF_R) >> 2;
            c->prot |= ph->p_flags & PF_W;
            c->prot |= (ph->p_flags & PF_X) << 2;
            break;
        }
        case PT_DYNAMIC:
            //piggybacking the dynamic section info
            lib->dyn = (void *)(ph->p_vaddr + (uint64_t)addr);
            lib->dyn_num = ph->p_memsz / sizeof(Elf64_Dyn);
            break;
        default:
            break;
        }
    }

    //now loading...
    uint64_t maplength = loadcmds[nloadcmd - 1].allocend - loadcmds[0].mapstart;
    struct loadcmd *c = loadcmds;
    int fd = fileno(lib->fs);
    if(mmap(addr, maplength, c->prot, MAP_FILE | MAP_PRIVATE | MAP_FIXED, fd, c->mapoff) < 0)
    {
        //ask for maplength B of contigious memory at addr, fails if cannot allocate
        fprintf(stderr, "mapLibrary error: mmap failed when trying to load %s", lib->name);
        exit(-1);
    }
    while(c < &loadcmds[nloadcmd])
    {
        mmap((void *) (c->mapstart + addr), c->mapend - c->mapstart, c->prot,
                MAP_FILE | MAP_PRIVATE | MAP_FIXED, fd, c->mapoff);
        if(c->allocend > c->dataend)
        {
            // here comes the .bss
            Elf64_Addr bss_start, bss_end, bss_page;
            bss_start = (Elf64_Addr)addr + c->dataend;
            bss_end = (Elf64_Addr)addr + c->allocend;
            bss_page = ALIGN_UP(bss_start, pagesize);

            //initialize the .bss
            if(bss_end < bss_page)
                bss_page = bss_end;
            if(bss_page > bss_start)
                memset((void *)bss_start, 0, bss_page - bss_start);
            if(bss_end > bss_page)
                mmap((void *)bss_page, ALIGN_UP(bss_end, pagesize) - bss_page,
                        c->prot, MAP_ANON | MAP_PRIVATE | MAP_FIXED, -1, 0);
        }
        c++;
    }
    return ALIGN_UP(maplength, pagesize); //TODO: make it more like real situation, this is just a quick fix

}

static void fill_info(Library *lib)
{
    //fill in info that might be reused later, like strtab. But this fails when one section has mutiple entries
    //so we have to traverse it when checking deps...
    Elf64_Dyn *dyn = lib->dyn;
    Elf64_Dyn **dyn_info = lib->dyn_info;

    while (dyn->d_tag != DT_NULL)
    {
        if ((Elf64_Xword)dyn->d_tag < DT_NUM)
            dyn_info[dyn->d_tag] = dyn;
        else if ((Elf64_Xword)dyn->d_tag == DT_RELACOUNT)
            //info[ DT_NUM + (DT_VERNEEDNUM - dyn->d_tag)] = dyn; //this is a quick fix for relacount
            dyn_info[DT_NUM + DT_RELACOUNT_NEW] = dyn;
        else if ((Elf64_Xword)dyn->d_tag == DT_GNU_HASH)
            dyn_info[DT_NUM + DT_GNU_HASH_NEW] = dyn;
        //TODO: optimize this huge branch if the performance is poor
        ++dyn;
    }
    #define rebase(tag)                             \
        do                                          \
        {                                           \
            if (dyn_info[tag])                          \
                dyn_info[tag]->d_un.d_ptr += lib->addr; \
        } while (0)
    rebase(DT_SYMTAB);
    rebase(DT_STRTAB);
    rebase(DT_RELA);
    rebase(DT_JMPREL);
    rebase(DT_NUM + DT_GNU_HASH_NEW); //DT_GNU_HASH
    rebase(DT_PLTGOT);
}

static void setup_hash(Library *l)
{
    uint32_t *hash;

    /* borrowed from dl-lookup.c:_dl_setup_hash */
    Elf32_Word *hash32 = (Elf32_Word *)l->dyn_info[DT_NUM + DT_GNU_HASH_NEW]->d_un.d_ptr;
    l->l_nbuckets = *hash32++;
    Elf32_Word symbias = *hash32++;
    Elf32_Word bitmask_nwords = *hash32++;

    l->l_gnu_bitmask_idxbits = bitmask_nwords - 1;
    l->l_gnu_shift = *hash32++;

    l->l_gnu_bitmask = (Elf64_Addr *)hash32;
    hash32 += 64 / 32 * bitmask_nwords;

    l->l_gnu_buckets = hash32;
    hash32 += l->l_nbuckets;
    l->l_gnu_chain_zero = hash32 - symbias;
}

static uint64_t mapWorker(Library *lib, void *addr)
{
    // fill in the infomation and allocate space for shared object specified by lib
    lib->addr = (uint64_t)addr;

    char tmp_ehdr[832]; //this strange number is for compatiable issues, refer to glibc
    if(!fread(tmp_ehdr, sizeof(Elf64_Ehdr), 1, lib->fs))
    {
        fprintf(stderr, "mapLibrary error: cannot read ELF header of file %s", lib->name);
        exit(-1);
    }

    //get the elf header, now to read the program header
    Elf64_Ehdr *ehdr = (Elf64_Ehdr *)tmp_ehdr;
    uint16_t phnum = ehdr->e_phnum;
    uint64_t phoff = ehdr->e_phoff;
    uint64_t phlen = phnum * sizeof(Elf64_Phdr);
    Elf64_Phdr *phdr = malloc(phlen);
    if(!fread(phdr, phlen, 1, lib->fs))
    {
        fprintf(stderr, "mapLibrary error: cannot read ELF program header of file %s", lib->name);
        exit(-1);
    }

    //actually loading it into memory
    uint64_t maplength = mapSegment(lib, phdr, addr, phnum);
    //fill in dynamic sections
    fill_info(lib);
    setup_hash(lib);
    //inspect DT_NEEDED, and put them on the list
    Elf64_Dyn *dyn = lib->dyn;
    const char *strtab = (void *)lib->dyn_info[DT_STRTAB]->d_un.d_ptr; //rebased string table for pointing runpath
    const char *runpath = (lib->dyn_info[DT_RUNPATH])? strdup(lib->dyn_info[DT_RUNPATH]->d_un.d_val + strtab):NULL;
    
    int nneeded = 0;
    //count how many needs are there
    while(dyn->d_tag != DT_NULL)
    {
        if(dyn->d_tag == DT_NEEDED)
            nneeded++;
        dyn++;
    }
    lib->search_list = malloc((nneeded + 1) * sizeof(Library *));

    dyn = lib->dyn;
    while(dyn->d_tag != DT_NULL)
    {
        int need_processed = 0;
        if(need_processed == nneeded)
            break;
        if(dyn->d_tag == DT_NEEDED)
        {
            //can't use index to access DT_NEEEDED for there could be many of them
            char *depname = strdup(dyn->d_un.d_val + strtab);
            Library *dep_addr = isLibraryOpen(depname);
            if(!dep_addr)
            {
                //we encounter a shared object whose Library isn't set up
                //put it on list and next call to maoWorker will fix it
                FILE *dep_fs = openDep(depname, runpath);
                if(dep_fs == NULL)
                {
                    fprintf(stderr, "mapLibrary error: unable to open %s as a dependency of %s",
                        depname, lib->name);
                    //free(depname);
                    exit(-1);
                }
                dep_addr = calloc(sizeof(Library), 1);
                dep_addr->name = depname;
                dep_addr->fs = dep_fs;
                lib->search_list[++need_processed] = dep_addr; //make room for search_list[0] by using ++n

                openedTail->next = dep_addr;
                openedTail = dep_addr;
                dep_addr->next = NULL;
                if(doFakeLoad(depname))
                    dep_addr->fake = 1;
            }
            else
            {
                //we've opened it, so we just fill in the dependency list
                lib->search_list[++need_processed] = dep_addr;
            }
        }
        dyn++;
    }
    //search self for symbols first
    lib->search_list[0] = lib;
    return maplength;
    
}
