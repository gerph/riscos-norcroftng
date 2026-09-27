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

Actually copying these binaries into
`/riscos-resources/Install/Tools/Linux/` and wiring them into whatever
mechanism decides what's on `PATH` inside a build-environment container is
explicitly deferred — see [overview.md](overview.md)'s Scope. This section
exists so that later work has a concrete target to build, not to do that
work now.

### `riscos-n++` is a language-level C++ compiler only, for now

Confirmed with the user: there is no C++ standard library (no libstdc++
equivalent) for RISC OS in this environment yet, and building one is not
part of this design. `riscos-n++` will compile and link C++ *language*
features (classes, constructors, templates, etc. — all confirmed working
against the existing `tests/cpp` suite, see
[testing-and-validation.md](testing-and-validation.md)), and anything a
program brings via plain C library calls (through `C:o.stubsG`, same as
`riscos-ncc`), but `#include <vector>` or similar will not work without a
library that doesn't exist yet. This is worth stating plainly in whatever
user-facing help text ships with `riscos-n++`, so it's not discovered by
surprise.

## Open Questions

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
