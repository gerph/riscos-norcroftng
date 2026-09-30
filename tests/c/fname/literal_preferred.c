// Regression test: when a literal POSIX-style source file exists (eg
// "probe.c"), it must still be used in preference to a same-named RISC OS
// on-disk form ("c/probe") sitting alongside it - preserving every
// existing test in this suite, which invokes the compiler with a bare
// "*.c" path directly (see the design doc's own record of this choice:
// design/riscos-build/filenames-and-paths.md, "try literal first, then
// RISC OS form"). Only once the literal is *absent* does the RISC OS
// form get tried - see riscos_form_fallback.c for that half.

// RUN: mkdir -p c && cp "$(dirname %s)/literal_preferred_literal_fixture" probe.c && cp "$(dirname %s)/literal_preferred_riscos_fixture" c/probe && %cc probe.c -S -o -

// CHECK: fname_literal_wins
// CHECK-NO: fname_riscos_form_wins
