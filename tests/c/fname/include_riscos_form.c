// Regression test: "#include <marker.h>" must resolve against a RISC OS
// on-disk header ("h/marker", no dot-extension) when no literal
// "marker.h" exists in the search directory - matching the real
// riscos-cc's own behaviour (confirmed empirically against the installed
// tool - see design/riscos-build/filenames-and-paths.md) and what a real
// CLX-style library search directory actually looks like on disk.

// RUN: mkdir -p h && cp "$(dirname %s)/include_riscos_form_fixture" h/fnametestmarker && %cc -I. %s -c -o t.o

#include <fnametestmarker.h>

int main(void) { return FNAMETEST_MARKER_OK; }
