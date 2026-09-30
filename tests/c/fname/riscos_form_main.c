// Regression test: a source file given in RISC OS's own on-disk form
// (directory-first "c/main", no dot-extension at all) must be recognised
// and compiled, not rejected with "type of 'c/main' unknown".
//
// This is the exact shape a real AMU Makefile passes (LibraryCommand's
// rules always invoke the compiler with "c/main", never "main.c"), and is
// what TOOLCHAIN32=norcroftng builds were failing on before this fix - see
// design/riscos-build/filenames-and-paths.md.
//
// The discovered test file itself is never compiled directly: it only
// carries this comment plus the RUN/CHECK directives. The real source
// lives in the companion riscos_form_main_fixture file (no extension, so
// runtests.py's *.c/*.cpp glob never picks it up on its own), copied into
// a real "c/main" path at run time.

// RUN: mkdir -p c && cp "$(dirname %s)/riscos_form_main_fixture" c/main && %cc c/main -S -o -

// CHECK: fname_riscos_form_probe
