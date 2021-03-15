# PLT trampoline for lazy binding
#define REGISTER_SAVE_SPACE 56
#define RAX_ADDR 0
#define RCX_ADDR (RAX_ADDR + 8)
#define RDX_ADDR (RCX_ADDR + 8)
#define RSI_ADDR (RDX_ADDR + 8)
#define RDI_ADDR (RSI_ADDR + 8)
#define R8_ADDR (RDI_ADDR + 8)
#define R9_ADDR (R8_ADDR + 8)

    .text
    .globl trampoline
    .hidden trampoline
    .type trampoline, @function
    .align 16

trampoline:
    # because `jmp GOT[2]` is called, this snippet is more like a "inline" function:
    # you don't what your registers be fucked up upon a `jmp` call
    # but we are calling a function, and it does fuck up caller-saved registers, so save them
    sub $REGISTER_SAVE_SPACE, %rsp
    movq %rax, RAX_ADDR(%rsp)
    movq %rcx, RCX_ADDR(%rsp)
    movq %rdx, RDX_ADDR(%rsp)
    movq %rsi, RSI_ADDR(%rsp)
    movq %rdi, RDI_ADDR(%rsp)
    movq %r8, R8_ADDR(%rsp)
    movq %r9, R9_ADDR(%rsp)

    movq REGISTER_SAVE_SPACE(%rsp), %rdi    # 1st argument, GOT[1]
    movq (REGISTER_SAVE_SPACE + 8)(%rsp), %rsi    # 2nd argument, the index of reloc entries
    call runtimeResolve
    movq %rax, %r11

    movq R9_ADDR(%rsp), %r9
    movq R8_ADDR(%rsp), %r8
    movq RDI_ADDR(%rsp), %rdi
    movq RSI_ADDR(%rsp), %rsi
    movq RDX_ADDR(%rsp), %rdx
    movq RCX_ADDR(%rsp), %rcx
    movq RAX_ADDR(%rsp), %rax

    add $(REGISTER_SAVE_SPACE + 16), %rsp    # we have 2 pushes
    jmp *%r11                               # call the function found
