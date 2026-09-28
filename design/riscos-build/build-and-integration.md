# Build and integration

Part of [riscos-n++](overview.md).

## Decisions

### The cross-build already works; no new build system is needed

Confirmed by building, not just reading:

```
$ cd /riscos-source/Norcroft
$ make ncc TARGET=riscos      # -> bin/ncc-riscos
$ make n++ TARGET=riscos      # -> bin/n++-riscos
```

Both build cleanly with the host's plain `cc` (no cross-toolchain needed —
this is a *hosted* cross-compiler: it runs on Linux and emits RISC OS
object code, the same relationship `riscos-cc-via-gcc` has to
`arm-unknown-riscos-gcc`, just without a translation layer since Norcroft
NG is Norcroft's own compiler). The top-level `Makefile` already:

- Separates `TARGET` (what code is generated for: `arm`, `riscos`,
  `riscos26`, `newton`) from `HOST` (what the compiler itself runs on:
  blank = the build host, `riscos` = self-hosted). This is exactly the
  cross vs. native distinction this design needs, and it already exists.
- Uses `VPATH`-equivalent multi-directory source references
  (`ncc/mip`, `ncc/cfe`, `ncc/armthumb`, `ncc/arm`, `ncc/util`,
  `ncc-support`, `ncc/ccacorn`, plus a per-target derived-headers
  directory) via `INC_COMMON`, and per-tool source lists
  (`CC_COMMON_SRCS`, `CFE_SRCS`, `CPPFE_SRCS`, `ARM_SRCS`, `SUPPORT_SRCS`).
  This is the same *idea* as the reference `cc` tool's `Makefile,fe1`
  (`VPATH = @ arm cfe mip riscos util ccacorn`, `INCLUDES = riscos.,arm.,
  cfe.,mip.,util.,ccacorn.,<Lib$Dir>.CLX.`) — multiple source
  directories combined into one build, headers found across all of
  them — just expressed for a portable `make` rather than AMU. No
  restructuring is needed to get that property; Norcroft NG already has
  it, arguably more portably (it builds identically on Linux/macOS without
  AMU or a RISC OS host).
- The `cc` tool's use of the **CLX library** (`<Lib$Dir>.CLX`, referenced
  under `ifdef __crosscompile` in its Makefile) is specifically for
  building a *RISC-OS-hosted* tool that also needs to cross-build on Linux
  as an intermediate step (`genhdrs`/`peepgen`) — CLX is a portable file/IO
  shim for that scenario. Norcroft NG's equivalent host-abstraction layer
  is `ncc-support/` (`filestat.c`, `fname.c`, `prgname.c`, `trackfil.c`,
  `toolenv.c`, `riscos.c`) — already present, already used by both the
  cross-build and the `HOST=riscos` self-hosted build. No CLX dependency
  is needed for the cross-compiler; it would only become relevant if
  `HOST=riscos` native self-hosting (out of scope, see
  [overview.md](overview.md)) needed a host-side helper tool of its own,
  which it doesn't currently.

### AOF output works; ELF is not needed

Verified end-to-end:

```
$ ./bin/ncc-riscos -apcs 3/32 main.c -c -o o/main
$ file o/main
o/main: RISC OS Chunk data, AOF object
$ riscos-link o/main C:o.stubsG -o main
$ file main,ff8
$ riscos-run main,ff8
hello
```

...and the same for a small C++ program (a class with a constructor and a
member function) via `n++-riscos`, also producing correct output.

`ncc/armthumb/aaof.c` is a complete, working AOF object writer — not a
stub — and `riscos-link` (already installed in this environment, and
already documented in `riscos-help build-and-link` /
`riscos-help howto-gcc` as accepting both Norcroft's AOF and GCC's ELF
objects) consumes its output with no changes on either side. Because of
this, **an ELF backend is dropped from scope** — it would only be useful if
AOF didn't work, and it does. If a future need for ELF specifically
appears (e.g. a tool downstream of the compiler that only understands
ELF), that's a new, separate requirement to design when it actually shows
up, not a hedge to build now.

### No AOF librarian needed

The repository's own README describes `tools/` (not currently present) as
"a python recreation of `libfile`... useful for building libraries such as
`stubs`." That tool doesn't need to be written: this build environment
already has `riscos-libfile` and `riscos-libsearch` installed
(`/riscos-resources/Install/Tools/Linux/riscos-libfile`), and
`lib/stubs.a` / `lib/stubs-26.a` (both real ALF libraries — confirmed via
`file`) already exist in this repo. Building a static library from AOF
objects, if this project ever needs to produce one (e.g. packaging its own
runtime support code), is already covered.

### No GPL toolchain dependency

A separate but related project by the same contributor
(`github.com/pwombwell/aof-toolchain`, whose `norcroft/` submodule points
at the same Norcroft NG upstream this repository descends from) takes a
notably different path: it fetches GCCSDK's `drlink`/`asasm` (GPL-licensed)
from `svn://svn.riscos.info/gccsdk/...` at build time into a gitignored,
never-vendored path, specifically so its own git history stays free of GPL
code while still depending on GPL tools to link/assemble. That pattern
(fetch-not-vendor) is a reasonable way to depend on GPL tooling without
violating a "never commit GPL" policy — but it isn't needed here at all,
because `riscos-link` already does the job, without being GPL (it's part
of this build environment already, not a new dependency). This project
should not pull in `asasm`/`drlink`. See
[overview.md](overview.md) for the licensing rule this respects.

### Proposed binary naming: build and install both `riscos-ncc` and `riscos-n++`

`n++-riscos` and `ncc-riscos` are both natural, low-cost outputs of the
same build (`ncc` is not a throwaway intermediate — it's a complete,
independently useful C compiler, and the two share the vast majority of
their source). Installing only `riscos-n++` would leave Norcroft NG's own
C front end unreachable under its own name, even though building it costs
nothing extra. This does not touch or replace the existing `riscos-cc`
(a different, presumably ROOL-derived Norcroft build, version 5.18) or
`riscos-c++` (the 2005 Acorn cfront-style translator) — see
[overview.md](overview.md)'s open question on that positioning.

### Proposed installed names and invocation

Following the existing convention exactly (`riscos-cc` *is* the Norcroft
binary, just renamed/copied to that name — not a wrapper script, unlike
`riscos-cc-via-gcc` or `riscos64-gcc` which translate arguments):

- `bin/ncc-riscos` → installed as `riscos-ncc`
- `bin/n++-riscos` → installed as `riscos-n++`

No argument-translation wrapper is needed (there's no other compiler's CLI
being adapted to), so no shell script is proposed here — just the renamed
binary, exactly mirroring `riscos-cc`. Command-line usage is unchanged
from what's already shown in this environment's own docs, e.g.:

```
riscos-n++ -apcs 3/32 c/main -c
riscos-link o/main C:o.stubsG -o main
```

### Export pattern: mirror `cc`'s `resources.yaml`, don't install directly

Confirmed by the user: this repository should just export its build; a
separate mechanism handles installing it. That mechanism, concretely, is
the `resources.yaml` manifest convention already used by the reference
`cc` tool and other projects in `native-build-tools`. `cc`'s own
`resources.yaml` declares:

```yaml
exports:
  build/crosscompile-cc:
    phases: native:install
    upload: false
    path: $ROTOOL_DIR
    files:
      - riscos-cc
      - riscos-dem
      - riscos-toansi
```

i.e. it names the exact files it produces (already using their final
installed names — `riscos-cc` is both the export name and what ends up at
`/riscos-resources/Install/Tools/Linux/riscos-cc`), and a `path` variable
that the outer tooling resolves, not this repo. The proposed equivalent
for this repository:

```yaml
exports:
  build/crosscompile-norcroft-ng:
    phases: native:install
    upload: false
    path: $ROTOOL_DIR
    files:
      - riscos-ncc
      - riscos-n++
```

This repository's job is then only to make sure files with those exact
names (`riscos-ncc`, `riscos-n++`) exist somewhere the outer tooling looks
after a build — copies/renames of `bin/ncc-riscos`/`bin/n++-riscos` — not
to place them into `/riscos-resources` itself. Actually adding this
`resources.yaml`, and whatever Makefile step produces the renamed copies,
is still deferred to the later, explicitly-directed integration phase (see
[overview.md](overview.md)'s Scope) — this section specifies the *shape*
so that phase starts from a concrete target.

## Open Questions (continued)

- **Exactly where `resources.yaml`'s `files:` entries are resolved from
  is partially, not fully, traced.** `cc`'s `ci/riscos.sh` sets
  `ROTOOL_DIR=${ROTOOL_DIR:-$INSTALL_DIR/Tools/$(uname -s)}` and adds it to
  `PATH` — confirming `path: $ROTOOL_DIR` in `resources.yaml` does resolve
  to an `Install/Tools/<Host>/` shape, consistent with the directory
  actually observed in the checked-out repo. What's *not* confirmed: `cc`'s
  own `Makefile` has no `install`/`export` target and never references
  `riscos-build`, `Install/Tools`, or `ROTOOL_DIR` at all — so the actual
  copy from wherever the Makefile's build output lands (a `COMPONENT=cc`/
  `TYPE=aif` AMU convention, not a literal `riscos-cc`-named file produced
  directly) into `$ROTOOL_DIR` under the name `riscos-cc` must happen
  inside the *shared* `ci/build.sh` harness (itself a submodule shared
  across many `native-build-tools` projects, not something owned by `cc`).
  Tracing that shared harness fully is a bigger side-quest than seemed
  worth it here — it's shared CI infrastructure this design has already
  deferred touching (see [overview.md](overview.md)'s Scope), and you
  maintain it, so it's faster to just ask than for me to keep reading a
  generic harness used by many unrelated projects: does the AMU
  `COMPONENT`/`TYPE` convention need to be adopted here too for the export
  to work, or is a plain `bin/ncc-riscos` → `riscos-ncc` copy at a known
  path enough for the harness to pick up?

### `riscos-n++` is a language-level C++ compiler only, for now — virtual
### functions previously crashed at runtime; this is now fixed

**Fixed** (see the root-cause and fix write-up below) — kept here as the
record of what was wrong and why, since the two remaining C++ crashes
below are not fixed and the general "language-level compiler only, no
standard library" scoping below still applies.

Confirmed with the user: there is no C++ standard library (no libstdc++
equivalent) for RISC OS in this environment yet, and building one is not
part of this design. `#include <vector>` or similar will not work without
a library that doesn't exist yet — expected, and stated plainly here so
it's not discovered by surprise.

What's *not* just a missing-library gap, and needs to be stated with equal
prominence: **classes with any virtual function crash at runtime**,
confirmed with the smallest possible reproduction —

```cpp
class Base { public: virtual int val() { return 1; } };
int main() { Base b; return b.val(); }
```

— compiles and links cleanly, then crashes immediately on entry to
`_main` under `riscos-run` (Pyromaniac). No inheritance, no `new`, no
pointer-based polymorphic dispatch — just one virtual method called
directly on a concrete stack object. This is a compiler code-generation
or object-layout bug (vtable construction, `this`-pointer handling, or
similar), not a missing-runtime-support gap — nothing external could fix
it, since nothing external is even involved yet at this point.

By contrast, confirmed genuinely working: non-virtual member functions,
constructors/destructors, templates, `static_assert` (all pass in
`tests/cpp`, see [testing-and-validation.md](testing-and-validation.md)),
and the earlier hand-tested `Greeter` class in
[overview.md](overview.md) — none of those use a virtual function. The
line between "works" and "crashes" here is specifically virtual dispatch,
not "C++ in general."

This means the honest scope for `riscos-n++` right now is closer to
**"compiles non-polymorphic C++"** than **"C++ minus a standard
library"** — a real distinction, since idiomatic C++ leans on virtual
functions constantly (interfaces, `virtual` destructors on any base
class meant to be deleted polymorphically, most object-oriented designs).
This is worth fixing before calling C++ support usable for anything beyond
templates/toy examples, and is a strictly more urgent problem than the
library gap above.

#### Root cause, confirmed: a call-site/vtable-content mismatch, not a
#### constructor problem

Investigated directly rather than guessed at. Disassembling the crashing
case (`-S`) and running it under `riscos-run --debug traceblock` gives an
exact, unambiguous answer:

```
__VTABLE__4Base
        b               val__4BaseFv        ; the vtable "slot" is a branch instruction

...call site, inside main...
        ldr     r1, [sp]        ; r1 = the vtable pointer (address of __VTABLE__4Base)
        mov     lr, pc
        ldr     pc, [r1]        ; pc = *(word at r1) -- treats r1 as a data pointer
```

The crash trace confirms it precisely: `pc` ends up as `&ea00003a` —
which is *exactly* the raw 32-bit encoding of the `b val__4BaseFv`
instruction sitting in the vtable slot (`0xEA` = unconditional branch,
`0x00003a` = the branch's own word offset), not a real address. `LDR pc,
[r1]` *dereferences* the vtable slot, expecting to find a data word there
containing the target function's address — but what's actually stored
there is a branch *instruction*, meant to be jumped to directly (`MOV pc,
r1`), not read as data. Two code-generation paths disagree about which
vtable convention this backend uses, and the call site loses.

This is **not** a constructor-initialisation problem — there's no global
state, no static-initialiser table, and no heap involved anywhere in the
minimal repro (`Base b; b.val();`, one class, one method, called directly
on a stack object). The mismatch is entirely between how the vtable's
*content* gets generated and how a virtual call *site* reads it.

The relevant mechanism exists and looks correctly written where it's
easy to find: `ncc/arm/target.h` defines
`target_has_data_vtables` as `(!pcrel_vtables)`, and
`ncc/mip/flowgraf.c` (~line 3106) branches on that flag when building each
vtable slot — data-word emission (`J_WORD_ADCON`/`J_WORD_LABEL`) if true,
a tail-call branch if false. `pcrel_vtables` itself
(`ncc/mip/globals.h`) reads a pragma slot (`pp_pragmavec['u'-'a'] > 0`,
settable via `-zpu<n>`), which defaults to off (data vtables, matching
what the call site expects) per its initialisation in `ncc/cfe/pp.c`.

**Fully pinned down, with a temporary instrumented build** (added two
`fprintf(stderr, ...)` diagnostic lines directly in `flowgraf.c`,
rebuilt, tested, then reverted — nothing kept in the tree). The
`-zpu0`/`-zpu1` pragma genuinely has no effect here, and now it's clear
why: **the `target_has_data_vtables`-aware code at `flowgraf.c` ~3106
never runs at all for this vtable.** The loop that reaches it is guarded
by a fast-path check a few lines earlier:

```c
for (i=0; i<n; i++)
{ BlockHead *b = v[i]->block;
  if (blkcode_(b)[0].op == J_ORG) goto omit_casebranch;
}
```

Confirmed by instrumenting exactly this line: for our vtable's one entry,
`blkcode_(b)[0].op` **is** `J_ORG` (op 173), every time, so this always
jumps straight to `omit_casebranch:` — skipping the entire
`target_has_data_vtables` decision block (the code quoted above) without
ever evaluating it. Past that label, the block falls through to generic,
un-specialised code generation, which lowers the underlying `J_TAILCALLK`
icode the normal way: as a plain tail-call branch instruction. That's the
`b val__4BaseFv` we see — **it isn't a wrong choice between "data" and
"thunk" vtables at all; it's a specialised code path being skipped
entirely** because of the `J_ORG` guard, falling back to generic lowering
that was never written with `target_has_data_vtables` in mind (it
predates that distinction, or was never updated for it).

**Traced one level further, to why `J_ORG` appears at all, and fixed.**
`cg.c`'s `s_thunkentry` case (`case s_thunkentry:`) only emits `J_ORG`
padding per vtable entry `if (TARGET_VTAB_ELTSIZE > 4)` — that macro
defaults to 12 (`mip/defaults.h`) unless a target's `options.h`
overrides it. `cpparm/options.h`, `cppthumb/options.h`, and
`cppint/options.h` all correctly define it as 4 ("for indirect VTABLEs
optimised for single inheritance"); `ccacorn/options.h` — the options
header `TARGET=riscos` actually uses, for both `ncc` and `n++` — simply
never defined it, so it silently fell back to 12, unconditionally
triggering the `J_ORG` emission that then bypassed the whole
`target_has_data_vtables` decision in `flowgraf.c`.

**Fix applied**: added the same `#define TARGET_VTAB_ELTSIZE 4` to
`ncc/ccacorn/options.h` that the other C++ targets already have. No
change needed to `flowgraf.c`, `cg.c`, or the call site — once the
element size matches what "data vtables" actually need, the existing,
correctly-written `target_has_data_vtables` logic runs as designed.

Verified: the minimal repro now emits `DCD val__4BaseFv` (a data word)
instead of `b val__4BaseFv`, and real polymorphism now works end-to-end
under `riscos-run` — virtual dispatch through a pointer, through `new`,
and through a virtual destructor with a user-supplied `operator delete`,
all producing correct output where they previously crashed. Full
existing test suite unaffected (no regressions). A regression test,
`tests/cpp/virtual/vtable_single_method_is_data.cpp`, asserts the vtable
slot is a data word; confirmed it fails with the bug reintroduced before
restoring the fix — see
[testing-and-validation.md](testing-and-validation.md).

### `throw`/`try`/`catch`: investigated in depth, and this is a genuinely
### different kind of problem — not fixed, deliberately

Traced with the same discipline as the two fixed bugs (a temporary
instrumented build — `fprintf` diagnostics added at each candidate crash
site across `cg.c` and `flowgraf.c`, rebuilt and re-tested at each step,
then fully reverted with `git checkout` once done — nothing kept in the
tree). Unlike the previous two, this **isn't a small, localised fix**,
and no fix was applied.

**Isolating the trigger**: a bare `throw 42;` with no enclosing `try`
compiles cleanly. An empty `try { } catch (int e) { }` with no `throw`
inside also compiles cleanly. Only `throw` *inside* a `try` block
crashes — `throw` elsewhere in the same function, even one that also has
an unrelated `try`/`catch`, is fine. So the bug is specifically in how
`try`/`catch` codegen interacts with a `throw` in its body, not in either
construct alone.

**Tracing the crash itself**: `cg.c`'s `case s_try:` (the code that
builds the exception-handler dispatch table) runs to completion without
crashing — confirmed by instrumenting every step inside it. So does the
subsequent `return 0;` statement's own codegen (`cg_return`, called
twice — once explicitly, once for an apparent implicit safety-net return
— both complete cleanly). The crash is later still: inside
`cse_eliminate()` (`mip/cse.c`), the common-subexpression-elimination
pass that runs once the whole function body has been code-generated,
during per-function optimisation (`cg_topdecl2`, before register
allocation).

**Root cause, as far as traced**: `case s_try:` builds its handler
dispatch using a jopcode called `J_TYPECASE` — a case/table-like op
structurally similar to `J_THUNKTABLE`/`J_CASEBRANCH` (already used for
ordinary vtables and `switch` statements), specifically for matching a
thrown exception's runtime type against the declared `catch` clauses.
Grepping for handling of these three related ops together shows a
pattern repeated in **at least two separate places**: wherever
`J_THUNKTABLE`/`J_CASEBRANCH` get special-cased, `J_TYPECASE` is
conspicuously absent, falling through to generic logic that wasn't
written expecting its shape:

- `mip/csescan.c` (~line 4122): a `switch` on jopcode explicitly lists
  `case J_THUNKTABLE: case J_CASEBRANCH:` together, recording data used
  later in the pass (`cmp_r2vals_(&cmpk) = e1`) — `J_TYPECASE` isn't
  listed, so it silently falls to `default:` instead, skipping that
  bookkeeping.
- `arm/gen.c` (~line 2667): the ARM backend's final per-instruction code
  generator has `case J_TYPECASE: break;` — a **literal no-op**. No
  actual type-comparison/dispatch instructions are ever emitted for
  exception handlers on this target, regardless of the crash — meaning
  even a build that didn't crash here would still not correctly dispatch
  to the right `catch` clause at runtime, since there's no dispatch code
  generated at all.

Given `csescan.c`'s handling silently diverges for `J_TYPECASE` in a way
that (unlike the vtable bug) wasn't traced to one single exact
instruction, and given the backend's own code generator for it is an
acknowledged no-op, this reads as **a feature that was started
(front-end parsing, the `s_try`/`s_catch` AST and dispatch-table
construction in `cg.c`) and never finished** (CSE awareness, and any
actual ARM code generation for the type dispatch) — not a one-line
regression like the other two. Implementing it properly means adding
`J_TYPECASE` handling to CSE and writing real type-comparison codegen in
the ARM backend, plus almost certainly a runtime type-matching helper (in
the same category as the still-missing C++ standard library, see
[linking-and-c-library.md](linking-and-c-library.md)) — a real feature
project, not a bug fix.

**What this means practically**: `try`/`catch`/`throw` remain unsafe to
compile. The minimal, safe thing to do — not done here, since it's a
scope decision, not a fact to discover — would be turning the current
"warn then eventually crash deep in an unrelated optimisation pass" into
a clean, immediate compile-time error the moment `try`/`catch`/`throw` is
parsed, so a user hits a normal diagnostic instead of a segfault. That's
a small, safe, well-scoped change in its own right (reject cleanly,
don't implement the feature) — genuinely implementing exception handling
is a separate, much larger undertaking.

## Open Questions

- **Should `try`/`catch`/`throw` be turned into a clean compile-time
  error now** (small, safe, bounded — see above), **or left as-is
  pending an actual decision on whether exception-handling support is
  wanted at all** (a real feature project, given the CSE and codegen gaps
  found)? Recommend the clean-error change regardless of the bigger
  decision, since "compiler segfault" is strictly worse than "clear
  diagnostic" no matter which way the bigger question goes — but flagging
  it rather than just doing it, since it touches the front end's error
  handling for a whole language feature, not a self-contained code-gen
  path like the previous two fixes.
- Should the eventual `/riscos-resources` integration (deferred per Scope)
  also add a `TOOLCHAIN32`-style selector so existing projects' makefiles
  can opt into Norcroft NG without hardcoding `riscos-n++`/`riscos-ncc`
  directly (the way `TOOLCHAIN32=gcc` already lets a project swap to
  GCCSDK)? That mechanism lives in the shared AMU makefiles
  (`riscos-help makefiles`), which this design deliberately doesn't touch
  yet — noting it here so the later implementation phase considers it
  rather than defaulting to "just add a new binary."
- The versioning story: `bin/ncc-riscos` currently reports
  `Norcroft-NG RISC OS ARM C vsn 1.00 (Linux) [<build date>]`. Is `1.00`
  the right version to ship as, or should this track something else (a
  date-based scheme, or alignment with the upstream Norcroft NG repo's own
  versioning if it has one)? Low-stakes but worth a deliberate answer
  before the first real release rather than shipping whatever the
  Makefile currently defaults to.

## Proposals

- Directory for these design documents: `design/riscos-build/` at the
  repository root (this repo has no existing `docs/`-style convention to
  follow instead — its README describes `ncc/`, `ncc-support/`, `tests/`,
  `external/`, `tools/`, `bin/`, `lib/`, none of which fit a design
  document). Accepted implicitly by being where this document lives;
  revisit if a preferred location exists.
