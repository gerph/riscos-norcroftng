// Regression test: try/catch/throw must fail cleanly with a compile-time
// error, not crash the compiler.
//
// Exception handling is not implemented on this backend: try/catch builds
// an exception-dispatch jopcode (J_TYPECASE) structurally similar to
// J_THUNKTABLE/J_CASEBRANCH, but several places that special-case those -
// notably mip/csescan.c's CSE pass, and the ARM backend's own code
// generator (arm/gen.c), which has a literal no-op for it - were never
// extended to handle J_TYPECASE too. Compiling this used to segfault deep
// inside cse_eliminate(), well past parsing, rather than failing cleanly.
// See the riscos-build design notes (design/riscos-build/
// build-and-integration.md) for the full investigation.
//
// Until exception handling is actually implemented, the compiler rejects
// try/catch/throw immediately at parse time with a fatal error instead.

// RUN: %cxx %s -c
// EXPECT-ERROR
// CHECK-ERR: 'try-catch' unimplemented

int main() {
    try {
        throw 42;
    } catch (int e) {
        return e;
    }
    return 0;
}
