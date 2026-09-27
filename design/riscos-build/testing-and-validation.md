# Testing and validation

Part of [riscos-n++](overview.md).

## Decisions

### Current test suite status against the cross-built compiler

Run directly against `bin/ncc-riscos`/`bin/n++-riscos` (built exactly as
described in [build-and-integration.md](build-and-integration.md), no
modifications), using the repository's own `runtests.py`:

| Suite         | Compiler       | Result                          |
|---------------|----------------|----------------------------------|
| `tests/c`     | `ncc-riscos`   | 4/4 pass                         |
| `tests/fpa`   | `ncc-riscos`   | 30/30 pass                       |
| `tests/vfp`   | `ncc-riscos`   | 29/30 pass (1 known failure — see [floating-point.md](floating-point.md)) |
| `tests/cpp`   | `n++-riscos`   | 11/11 pass                       |

This is a small suite overall (the repository is still early in rebuilding
its regression coverage — see `ncc/tests/` for a separate, older set of
numbered regression tests not yet wired into `runtests.py`, which weren't
run as part of this design pass and are worth including in whatever CI
this project ends up with). The point of running it here wasn't to declare
the compiler "done" — it was to confirm the cross-build is real and
functional before writing the rest of this design around it.

**The 11/11 `tests/cpp` pass rate is real but narrow, and shouldn't be
read as "C++ works."** None of those 11 tests use a virtual function,
`new`/`delete`, or exceptions — and direct hand-testing beyond the suite
found that virtual functions crash at runtime unconditionally (a compiler
bug, see [build-and-integration.md](build-and-integration.md)), that
`operator new[]`/`operator delete[]` crash the *compiler*, and that `throw`
segfaults the compiler (see
[linking-and-c-library.md](linking-and-c-library.md) for all three, found
by testing directly rather than assumed). The suite currently measures
"the C++ front end parses and generates code for the constructs it has
tests for," which is a real and useful signal, but a materially smaller
claim than "C++ works" — worth keeping in mind before quoting the pass
rate on its own.

### `runtests.py` checks codegen text, not runtime behaviour

Every test file in `tests/` uses `// RUN: %cc %s -S -o -` plus `// CHECK:`
lines matching against emitted assembly text. This is fast and good for
catching codegen regressions precisely, but **it does not prove the
compiled code actually runs correctly** — a `CHECK` line can match
superficially-plausible-but-subtly-wrong instructions, and nothing in this
suite links or executes anything. The "does it actually run" evidence in
this design (the `printf` program, the C++ class, the FPA/VFP
double-add-and-print) came from separately compiling, linking with
`riscos-link`, and running with `riscos-run` under Pyromaniac — outside
`runtests.py` entirely, because that facility doesn't exist in the harness
today.

## Open Questions

- **Should `runtests.py` grow an execution mode** — compile, link against
  `C:o.stubsG`, run under `riscos-run`, and check actual program output/exit
  code — alongside its existing codegen-assertion mode? This would close
  the gap above and let real varargs/ABI-mismatch/C++-runtime-behaviour
  tests (flagged as needed in [floating-point.md](floating-point.md) and
  [linking-and-c-library.md](linking-and-c-library.md)) actually get
  written and checked automatically, rather than relying on hand-run
  smoke tests like the ones in this design. This seems like the highest-
  leverage next step but hasn't been scoped in detail — how much
  `runtests.py` needs to change, whether it needs to know about
  `riscos-link`/`riscos-run` directly or shell out, and whether it needs
  to run inside a container that actually has those tools (this design was
  written and validated inside exactly such a container, so that
  constraint is already satisfied here, just worth stating for whoever
  sets up CI).
- Should the `ncc/tests/` legacy numbered regression tests (pre-existing,
  not wired into `runtests.py`) be triaged and folded in, ported to the
  `// RUN:`/`// CHECK:` convention, or left as-is? Not investigated as part
  of this design.

## Proposals

- Before relying on either floating-point ABI or C++ support for real
  work, extend the test suite with the specific cases flagged elsewhere in
  this design as unverified: varargs functions mixing float/double/int
  arguments under both ABIs
  ([floating-point.md](floating-point.md)), and RISC OS/POSIX filename
  duality (`c.main`/`main.c`/`c/main`, `foo.h`/`h/foo`) and colon-path
  include resolution once the CLX `fname` swap lands
  ([filenames-and-paths.md](filenames-and-paths.md)). These are the areas
  this design surfaced real doubt about, not generic caution.
- Add regression tests for the three concrete C++ bugs found while
  testing this design (virtual-function runtime crash, `operator new[]`/
  `operator delete[]` compiler-fatal, `throw` compiler segfault — see
  [build-and-integration.md](build-and-integration.md) and
  [linking-and-c-library.md](linking-and-c-library.md)) *once each is
  fixed*, so none of the three regress silently. Filing these as tests
  before the fix exists isn't useful (a crash isn't a `// CHECK:`-able
  assembly assertion) — they belong in whatever bug tracking this project
  uses, with a note to add the regression test alongside the fix.
- Any CI for this project (not designed here — no CI currently exists for
  Norcroft NG) should build with the exact commands in
  [build-and-integration.md](build-and-integration.md)
  (`make ncc TARGET=riscos`, `make n++ TARGET=riscos`) and run
  `runtests.py` against the result, so CI is testing the same artifact this
  design was validated against rather than a differently-configured build.
