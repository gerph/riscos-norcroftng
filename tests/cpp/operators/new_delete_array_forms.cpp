// Regression test: `operator new[]`/`operator delete[]` (the array forms)
// must parse as ordinary overloadable function declarations, not crash
// the compiler.
//
// rd_operator_name() (cppfe/xsyn.c) recognised `operator new`/`operator
// delete` but never checked for a following `[` `]`, so
// `operator new[](...)` was misparsed as declaring an array-typed
// identifier named "__nw" - cascading into "array of <function> illegal",
// "type disagreement for '__nw'", and ultimately a
// `Fatal error: Failure of internal consistency check` compiler abort.
//
// Fixed by peeking for `[` `]` immediately after `new`/`delete` and, when
// present, naming the declaration "__nw_v"/"__dl_v" - matching the
// mangled names the compiler's own codegen already calls for
// `new T[n]`/`delete[] p` when T's destructor is trivial (xbuiltin.c's
// cppsim.xnewvec/xdelvec).
//
// Note: for element types with a non-trivial destructor, `new T[n]`
// calls a different, three-argument helper (also named `__nw_v`, but
// taking an extra destructor-callback parameter, so it mangles
// differently: `__nw_v__FPvUiT2PFPv_v`) that no runtime library
// currently provides - a separate, already-documented gap (no C++
// standard library), not something this test covers. This test uses a
// trivial-destructor element type so the whole program actually links
// and runs, not just compiles.

// RUN: %cxx %s -S -o -
// CHECK-ERR-NOT: Fatal error

#include <stdlib.h>

void *operator new[](unsigned int size) { return malloc(size); }
void operator delete[](void *p) { free(p); }

struct Trivial { int x; };

int main() {
    Trivial *arr = new Trivial[4];
    arr[1].x = 9;
    int r = arr[1].x;
    delete[] arr;
    return r;
}

// CHECK: __nw_v
// CHECK: __dl_v
