// Regression test: a bare "-IVAR:" include-path argument must expand VAR
// as an environment variable naming a single search directory (this
// environment's own "-IC:" convention for the system C library, used
// directly by the shared Makefiles) - not be passed through literally as
// an unopenable "VAR:" directory name. See
// design/riscos-build/filenames-and-paths.md for the decision record and
// its empirical verification against the real riscos-cc, including that
// tool's own (matched, not improved on) limitation for a genuinely
// multi-directory value.

// RUN: mkdir -p h && cp "$(dirname %s)/colon_env_path_fixture" h/fnametestcolonmarker && FNAMETESTCOLONVAR="$(pwd)" %cc -IFNAMETESTCOLONVAR: %s -c -o t.o

#include <fnametestcolonmarker.h>

int main(void) { return FNAMETEST_COLON_OK; }
