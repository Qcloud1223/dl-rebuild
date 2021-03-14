# dl-rebuild: a minimum counterpart of `ld.so`
## Intro
This project is originally part of an academic work, which wants to totally take over memory management,
for which we have to dictate where to load the shared object.

At the end of the day I found that `ld.so` is a surprisingly complex program featuring lots of functions
and various architecture support. This makes for the steep learning curve of dynamic linking. After having some hard time understanding it, now I want to rewrite it as clearly and simply as possible, for people who want to play with it later. 

I want this project to have the following features:
   
- closely resembles the working procedure of `ld.so`, but only the critical part: lazy binding, symbol resolution, ...(I may work on inline comments and some blogs later to state the counterparts in `ld.so`)
- a set of more understandable API and variable names
- works fine with existing shared objects, that is, works with code compiled with `-shared` and `-fPIC` without having to add custom segments 
- only support x64 Linux

Happy linking!

## TODO
- [ ] Implement lazy binding(hopefully in next commit)
- [ ] Restructure symbol hashing
- [ ] add `closeLibrary`

## Known Bug
Because this is not a full-function dynamic linker, some of the "black magic" used by `ld.so` is not implemented.

For example, `ld.so` should detect whether it is loading `libc.so.6` and fill in variables like `_rtld_global_ro`, you can find more details in `mapLibrary.c`. Failing to correctly assign value to these variables may lead to very tricky bugs.

To bypass this, I came up with a method called "fake loading", that is, when it comes to shared objects currently cannot be handled, I will turn to `dlopen` and `dlsym` for the actual job, and `dlclose` it when `closeLibrary` is called. I know it's dumb, but I've tried hard on it and you've got the idea.