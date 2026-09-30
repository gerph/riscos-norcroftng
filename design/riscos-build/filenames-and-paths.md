# Filenames and paths

Part of [riscos-n++](overview.md).

The user's requirement, stated directly: *"Does the tooling accept RISCOS
format filenames? That's a requirement. If c.main is given it must use
c/main on posix systems. And if main.c is given it must look at c/main as
well if the literal name isn't present. Similarly for includes, foo.h
should also check h/foo... includes should handle RISCOS colon paths and
variables as cc does."*

## Decisions

### Root cause found: `ncc-support/fname.c` is a known-incomplete
### reimplementation, not real CLX — and the call sites are already correct

Traced concretely, not inferred:

- `ncc/mip/driver.c` and `ncc/mip/compiler.c` already call
  `fname_parse()`/`fname_unparse()` at exactly the same call sites, with
  the same arguments, as the reference `cc` tool's own `driver.c`/
  `compiler.c` (`/riscos-source/native-build-tools/.../Tools/cc/
  crosscompile/{driver,compiler}.c`) — confirmed by direct comparison,
  same line content, same `FNAME_SUFFIXES`/`FNAME_INCLUDE_SUFFIXES`
  constants. **Nothing needs to change in the compiler's own call sites.**
- What differs is the implementation behind those calls.
  `ncc-support/fname.c`/`fname.h` in this repository is a from-scratch
  reimplementation (Copyright 2025 Piers Wombwell, Apache-2.0) — its own
  header comment admits the gap directly: *"For RISC OS, this is joyous
  fun. The file extension is placed before the leafname, and I haven't
  figured out when we need to change slashes to dots."* That's precisely
  the `foo.c <-> c.foo <-> c/foo` extension-inversion duality the user is
  asking for.
- The real CLX `fname` module (found in this environment at
  `/riscos-source/native-build-tools/riscos-source/Sources/Lib/CLX/{c,h}/
  fname`, Copyright Advanced RISC Machines Ltd., 1992) documents exactly
  this feature as its purpose: *"to support the use of multiple
  file-naming conventions on one host... Note that, under RISC OS,
  name.<extn> is recognised as a RISC OS name. Extension inversion is
  performed... only for a specified list of extensions"* (the `suffixes`
  parameter — this is `FNAME_SUFFIXES`/`FNAME_INCLUDE_SUFFIXES`, already
  passed correctly by Norcroft NG's driver/compiler code).
- **Reproduced the failure live**, consistent with this diagnosis:
  compiling a file passed as `c/main` (POSIX-rendered RISC OS name) with
  the current cross-built `ncc-riscos` gives
  `Error: type of 'c/main' unknown (file ignored)` — the current
  `ncc-support/fname.c` doesn't recognise it. Compiling the same file as
  `main.c` (plain POSIX name) works today, but that only exercises the
  trivial case; nothing was found or tested that makes `c.main`,
  `c/main`, or the `foo.h`/`h/foo` include case work with the current
  reimplementation. Given the priority confirmed below (RISC-OS format is
  the *required* form; the POSIX form is compatibility-only), this means
  Norcroft NG's own cross-build currently has the priority backwards —
  it satisfies the optional case and fails the required one — which is
  exactly what the CLX `fname` swap below is expected to fix.

### Proposed fix: swap in real CLX's `fname` module — superseded, see "Fixed" below

**Not what was built.** Asked to choose between this and a fresh
reimplementation, Charles chose the reimplementation (the unlicensed
ARM/Pace copyright on CLX's own `fname` source is a different risk once
it's vendored into Norcroft NG's own redistributed, Apache-2.0 tree, versus
CLX being used as build-environment infrastructure elsewhere). Kept here
for the historical record of the option considered, not as a live plan —
see "Fixed: `ncc-support/fname.c` reimplemented fresh, not vendored" for
what was actually done.

Replace `ncc-support/fname.c` and `ncc-support/fname.h` with the real CLX
`c/fname` and `h/fname` sources (adjusted only as needed for this
project's build — e.g. header guards/include paths — not behaviourally).
No changes to `ncc/mip/driver.c` or `ncc/mip/compiler.c` are expected to be
needed, since their calls already match what `cc` itself does against the
same module. This directly satisfies the stated requirement per the
user's own framing — *"these are handled by CLX; any failures in that
regard are CLX bugs"* — by actually using CLX for it, rather than
maintaining a second, admittedly-incomplete implementation alongside it.

### A licensing fact worth recording plainly

CLX's `fname.h`/`pathmacro.h` (and, presumably, its other modules) carry
explicit copyright notices — "Advanced RISC Machines Ltd., 1992" and
"Pace Micro Technology plc., 2000" — with **no LICENSE file found**
anywhere in the CLX source tree, and no license header in the files
themselves beyond the bare copyright line. Under this project's licensing
rules (no GPL; otherwise MIT/Apache-2.0 as used elsewhere in this repo),
an unlicensed proprietary-copyright dependency would normally be a real
stop-and-check moment. Recording the mitigating context rather than
treating it as a fresh risk this project is introducing: CLX is already
pervasive, load-bearing infrastructure across this entire build
environment — it's exported by `native-build-tools`/`native-build-headers`
and consumed by dozens of existing projects under `/riscos-source`
(`riscos-gos`, `riscos-gcontext`, `pyromaniac`, `justin/p2cc`, and others),
and it's exactly what the reference `cc` tool this design was told to
mirror already depends on for this very feature. This isn't a new
acceptance decision Norcroft NG is making alone; it's adopting an
already-standing one. Flagging it here anyway, in case that standing
decision is itself something you'd want revisited — that's a call above
this design's scope, not something to silently assume is fine.

### Colon-path/variable expansion for `#include` search: confirmed working
### against the real `riscos-cc` — my first report of this failing was wrong

An earlier version of this document reported `-IC:` and
`-I<Lib$Dir>.GetOpt.` both failing against the production `riscos-cc`.
That was a test-fixture mistake on my part, not a real gap, and the
record is corrected here rather than left standing.

What actually happened: my test header existed only as a literal
`marker.h` file. Per the user's clarification below, that POSIX-style form
is compatibility-only and isn't guaranteed to resolve — the correct,
required form for a header reachable as `#include <marker.h>` is
`h/marker` (extension-inverted, RISC-OS style). Once the test fixture had
`h/marker` (with or without a `marker.h` alongside it), **both syntaxes
worked correctly** against real `riscos-cc`:

```
$ export C="/path/to/dir,$C"
$ riscos-cc -IC: -apcs 3/32 c/t -c        # works: searches every dir listed in $C

$ export LIB_DIR=/path/to
$ riscos-cc "-I<Lib\$Dir>.GetOpt." -apcs 3/32 c/t -c   # works: LIB_DIR maps to Lib$Dir
```

The `<Lib$Dir>.GetOpt.` form (correctly written with the trailing `.` —
my earlier draft dropped it and wrote `GetOptDir` as if it were one name,
which was my own misreading, not the user's example) is a complete,
literal command-line argument, resolved directly by the compiler/CLX
layer at the point the file is opened — confirmed by the user: AMU passes
command lines through unmodified (it transforms paths/targets/
dependencies internally for its own dependency graph, not arbitrary
argument text), so this was never an AMU-level concern to begin with.

### RISC OS format is the required form; POSIX-style names are
### best-effort compatibility only, not a guarantee

Confirmed directly by the user: *"files are always expected to be RISCOS
format - the posix form may not work and that's fine because it's only a
compatibility [aid]."* This resolves the asymmetry the testing above
surfaced (a bare `marker.h` alone did not resolve; `h/marker` alone did):
that's correct, expected behaviour, not a bug to fix. The requirement is
that RISC-OS-format names always work; a plain POSIX-style name working
too is a nice-to-have this fork inherits from history, not something to
invest further effort guaranteeing symmetrically for every case.

## Fixed: `ncc-support/fname.c` reimplemented fresh, not vendored

**Charles's direction**: fix the fname problems "in a similar way to the
CLX fname functions work so that we have the same behaviour in the
NorcroftNG system as the cc system" — but, when asked to choose between
vendoring CLX's actual source (this document's original Proposal) and a
fresh reimplementation, Charles chose the latter explicitly: CLX's
`c/fname`/`h/fname` carry an unlicensed ARM/Pace copyright (see "A
licensing fact worth recording plainly" below, unchanged), and vendoring
them directly into Norcroft NG's own Apache-2.0 source tree is a different
risk from CLX being used as build-environment infrastructure elsewhere,
which isn't redistributed as part of Norcroft NG's own source. The
reimplementation lives entirely in `ncc-support/fname.c`, its header
`ncc-support/fname.h`, plus two small, explained departures in
`ncc/mip/driver.c` and `ncc/mip/compiler.c` (see below) — verified against
the real, installed `riscos-cc` throughout, not against the CLX source
read in isolation.

### One behaviour genuinely differs from real `riscos-cc`, by deliberate choice

Empirically confirmed (not assumed): the real `riscos-cc` **never** tries
a literal POSIX name once it recognises an extension — given `t.c`, it
converts unconditionally to `c/t` and looks only there, even when a
literal `t.c` exists right beside it and no `c/t` does (reproduced
directly: `riscos-cc t.c -c` with only `t.c` on disk reports
`Compilation aborted: couldn't read file 'c/t'`). Matching that exactly
would have broken every one of the ~80 pre-existing Norcroft NG tests in
this suite, which invoke the compiler with a bare `*.c`/`*.cpp` path
directly (no `c/` subdirectory) — asked directly, Charles chose **try the
literal name first, then fall back to the RISC OS form** as a deliberate
superset of `riscos-cc`'s own behaviour, over faithfully matching it and
restructuring the whole test suite's invocation convention. This is
implemented per callsite, not inside `fname_parse`/`fname_unparse`
themselves (which stay pure string transforms, doing no filesystem
access, matching CLX's own architecture):

- **The primary source file argument** (`driver.c`'s `process_file_names`,
  `FNAME_SUFFIXES`): after the ordinary parse, if the recognised extension
  came from a literal dot-suffix (not already RISC OS "extension-as-
  directory" form) and the literal name doesn't exist on disk
  (`access(current, 0)`), re-synthesise the RISC OS form (eg `main.c` ->
  `c/main`) and re-parse it — this single location has one unambiguous
  "does the file I'm about to compile exist" check available, so the
  fallback decision can live right here.
- **`#include` resolution** (`compiler.c`'s `pp_inclopen`/`incl_search`,
  `FNAME_INCLUDE_SUFFIXES`): an include name has no single "does it
  exist" check available at parse time — it has to be tried against
  *each* `-I` search directory in turn, and `fname_parse` has no idea
  which directories those will be. So the existence-based choice doesn't
  belong in `fname.c` at all here: `pp_inclopen` now computes a second,
  RISC-OS-form candidate string (`<ext>/<root>`, eg `h/marker` for
  `<marker.h>`) alongside the existing literal one, and `incl_search`
  tries both, literal first, in the same directory before moving to the
  next — this is the one place the design doc's original "no
  `driver.c`/`compiler.c` changes needed" assumption (written when
  vendoring CLX exactly was still the plan) didn't hold, and is flagged
  here explicitly rather than silently expanded in scope.

A new `FNAME_EXTN_ASDIR` bit on `UnparsedName.type` (`fname.h`) records
which on-disk shape a parsed name is in, so `fname_unparse` knows whether
to reconstruct `root.ext` (literal) or `ext/root` (RISC OS form), and so
`compiler.c` can tell whether a second candidate is even meaningful (an
include already written in RISC OS form, or with no recognised extension
at all, needs no second candidate).

### Colon-path and `<Var>` expansion, including matching `riscos-cc`'s own limitation

`-IVAR:` (a bare environment-variable-as-volume prefix, this environment's
own `-IC:` convention) and `-I<Var>.tail.` (an embedded variable reference
inside an otherwise-dotted path, eg the real `-I<Lib$Dir>.GetOpt.`) are
both now expanded in `fname_unparse`, via `getenv()` on the variable name
uppercased with `$` turned into `_` (matching how this environment's own
shared Makefiles name the corresponding variable, eg `Lib$Dir` <->
`LIB_DIR`). Verified against the real `riscos-cc` for both forms before
implementing (not assumed from reading CLX's source alone): a
**single**-directory environment value expands correctly; a genuinely
**multi**-directory (comma-joined) value is left as literal, unopenable
`VAR:` text and the whole `-I` argument silently fails to resolve anything
— confirmed as `riscos-cc`'s own real behaviour (CLX's own
`faked_envvar_path` comment says as much: "Multi-path element - give up
and let the parent deal with it" — and nothing else does), not a gap this
reimplementation introduces or should try to improve on.

One correction found while testing this against real fixtures, not
assumed from the CLX source: a bare `-I.`/`-I..` (the ordinary Unix
relative-directory marker) must **not** have its dot(s) converted to `/`
the way a genuine RISC-OS dotted path fragment's dots are — an
unqualified "any slash-free argument's dots are RISC OS directory
separators" rule turns `-I.` into `-I/`. Fixed by exempting a bare `.`/
`..` specifically in `fname_unparse`'s path-reconstruction, before it
reaches the general dot-to-slash conversion.

### Verification

Six new regression tests in `tests/c/fname/` (each confirmed to actually
compile correctly, added to the permanent suite): RISC-OS-form primary
source file (`c/main`), literal-preferred-when-both-exist, literal-
missing-falls-back, `#include` RISC-OS-form fallback, `-IVAR:` expansion,
and `-I<Var>.tail.` expansion. Full suite after: 85 passed, 1 known
pre-existing VFP failure (unchanged) — no regressions.

End-to-end, real-tool verification beyond the unit-style tests above: a
genuinely unmodified `riscos-project create --type command --skeleton`
project, built through the standard `LibraryCommand` AMU pipeline with
`TOOLCHAIN32=norcroftng` (cross-compile-docker's `rootenv` selector — see
that repository's own commit), compiles and links cleanly
(`riscos-ncc ... c/main` -> `riscos-link ... -> All built`) and **runs
correctly under Pyromaniac**, printing its expected help/version text —
resolving the concrete blocker recorded below, not just the isolated
compiler bug.

## Open Questions

None remaining — both the general filename-duality requirement and the
concrete `TOOLCHAIN32=norcroftng` blocker it caused are now fixed and
verified, per "Fixed" above.

## Proposals

None outstanding — the CLX-fname-swap proposal below is superseded by the
fresh reimplementation above, per Charles's explicit choice (see "Fixed").
