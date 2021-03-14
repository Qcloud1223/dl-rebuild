//interface for the users
#define BIND_NOW 0
#define LAZY_BIND 1

extern void* openLibrary(const char *name, int mode, void *addr);
extern void* findSymbol(void *library, const char *symname);
