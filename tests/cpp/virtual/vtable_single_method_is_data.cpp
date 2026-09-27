// Regression test: a class with a single virtual method (no multiple
// inheritance, no this-pointer adjustment) must emit its vtable slot as a
// plain data word (DCD) referencing the method, not a branch instruction.
//
// The call site for a virtual call loads the vtable pointer and does
// `LDR pc, [vtable]` - i.e. it expects the slot to be a data pointer it
// can dereference and jump to. If the slot instead holds a `B <method>`
// branch instruction (meant to be jumped to directly, not dereferenced),
// the load reads the branch's own raw encoding as if it were an address
// and jumps there, crashing immediately - this reproduced as a runtime
// abort in the smallest possible case (one virtual method, no
// inheritance, called directly on a stack object).
//
// Root cause: ccacorn/options.h (used for TARGET=riscos, both C and C++)
// did not define TARGET_VTAB_ELTSIZE, so it fell back to the generic
// default of 12 (mip/defaults.h), which is >4. cg.c's s_thunkentry case
// only skips emitting a J_ORG padding directive per vtable entry when
// TARGET_VTAB_ELTSIZE <= 4; with it >4, J_ORG was always emitted, and
// flowgraf.c's vtable lowering treats the presence of J_ORG as a signal
// to bypass its entire target_has_data_vtables-aware decision, always
// falling back to a plain branch instruction regardless of that flag.
// cpparm/cppthumb/cppint's options.h all correctly define
// TARGET_VTAB_ELTSIZE as 4 ("for indirect VTABLEs optimised for single
// inheritance"); ccacorn/options.h was simply missing the same define.

// RUN: %cxx %s -S -o -

class Base {
public:
    virtual int val() { return 1; }
};

int main() {
    Base b;
    return b.val();
}

// CHECK: __VTABLE__4Base
// CHECK: DCD
