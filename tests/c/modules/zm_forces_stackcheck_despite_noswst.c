// Regression test: -zM must force software stack checking on even when the
// user explicitly asks for it to be suppressed via -apcs.../noswst.
//
// A module has no stub-provided stack-extension trampoline (that veneer
// lives in C:o.stubsG, which module code doesn't link against the same
// way), so it must check its own stack use in software - unconditionally,
// regardless of any /noswst request that would be perfectly safe for an
// ordinary application relying on the trampoline. Before this fix, -zM
// didn't touch PCS_NOSTACKCHECK at all, so an explicit /noswst silently
// left module code with no stack check whatsoever.

// RUN: %cc -zM -apcs 3/32/noswst %s -S -o -

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
