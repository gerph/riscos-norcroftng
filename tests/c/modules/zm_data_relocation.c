// Regression test: -zM (RISC OS relocatable-module code generation) must
// actually change codegen, not just be accepted and silently ignored.
//
// A module's data area isn't at a fixed link-time address - it's wherever
// the RMA happens to allocate it at run time - so every reference to
// static/global data has to be corrected by a runtime delta. That delta is
// loaded through R_SL (the stack-limit register, repurposed here) from a
// linker-resolved offset, imported as the symbol _Mod$Reloc$Off (or
// _Lib$Reloc$Off for -zM1, "library callable by a module").
//
// Root cause this guards against: arthur_module (the internal flag that
// gates this whole mechanism in gen.c/mcdep.c/codebuf.c) was unconditionally
// set to 0 in config_init() - -zM was parsed and stored by
// mcdep_config_option(), but nothing ever read the value back, so the
// option silently produced ordinary, non-module-safe code. Fixed by reading
// it via TE_Integer(t, "-zm", 0), matching how every other -z<letter>
// option in config_init() is already handled.
//
// -zM also forces real software stack checking on (PCS_NOSTACKCHECK
// cleared), since a module has no stub-provided stack-extension trampoline
// and must check its own stack use.

// RUN: %cc -zM %s -S -o -

extern void touch(int *p);

int global_counter;

void bump(int n) {
    int stack_array[256];
    int i;
    for (i = 0; i < 256; i++)
        stack_array[i] = n + i;
    touch(stack_array);
    global_counter += stack_array[0];
}

// CHECK: cmp             ip, r10
// CHECK: blmi            __rt_stkovf_split_big
// CHECK: ldr             ip, [r10]
// CHECK: add             r0, ip, r0
// CHECK: IMPORT |_Mod$Reloc$Off|
// CHECK: IMPORT __rt_stkovf_split_big
