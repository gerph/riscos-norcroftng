// Regression test: when a "*.c"-style path is given but no literal file
// exists by that exact name, the compiler must fall back to RISC OS's own
// on-disk form (eg "probe.c" absent -> try "c/probe") rather than simply
// failing to find the file. See literal_preferred.c for the companion
// case (literal wins when both exist) and
// design/riscos-build/filenames-and-paths.md for the decision record.

// RUN: mkdir -p c && cp "$(dirname %s)/riscos_form_fallback_fixture" c/probe && %cc probe.c -S -o -

// CHECK: fname_riscos_form_fallback_used
