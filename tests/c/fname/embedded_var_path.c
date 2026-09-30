// Regression test: a "-I<Var>.sub." include-path argument (an environment
// variable reference embedded inside an otherwise-dotted RISC OS path,
// eg the real "-I<Lib$Dir>.GetOpt." convention this environment's own
// Makefiles use) must expand the variable and treat the dot immediately
// after it as a directory separator - not be passed through literally.
// The variable name's '$' must become '_' (matching how this
// environment's shared Makefiles name the corresponding shell/environment
// variable, eg "Lib$Dir" <-> "LIB_DIR"). See
// design/riscos-build/filenames-and-paths.md for the decision record and
// its empirical verification against the real riscos-cc.

// RUN: mkdir -p sub/h && cp "$(dirname %s)/embedded_var_path_fixture" sub/h/fnametestvarmarker && FNAMETEST_ROOT="$(pwd)" %cc "-I<FnameTest\$Root>.sub." %s -c -o t.o

#include <fnametestvarmarker.h>

int main(void) { return FNAMETEST_VAR_OK; }
