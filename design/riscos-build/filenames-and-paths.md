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

### Proposed fix: swap in real CLX's `fname` module

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

## Open Questions

- **This gap now concretely blocks `TOOLCHAIN32=norcroftng`** (the
  cross-compile-docker/`rootenv` selector that routes AMU builds through
  `riscos-ncc`/`riscos-n++` — see that repository's own
  `crosscompile/design/gccsdk-4.7-builder.md` for the sibling
  `TOOLCHAIN32=gcc` work this mirrors). Tried a genuinely unmodified
  `riscos-project create --type command --skeleton` build through the
  standard `LibraryCommand` pipeline with `TOOLCHAIN32=norcroftng`: AMU's
  rule passes the source file as `c/main` (directory-based RISC OS form),
  and `riscos-ncc` fails with `Error: type of 'c/main' unknown (file
  ignored)`, reproducing exactly the failure already diagnosed above — the
  Makefile-level wiring itself is correct and verified separately (a
  minimal test Makefile confirms `CC`/`C++` resolve to
  `riscos-ncc`/`riscos-n++`), but no real, unmodified project can build
  end-to-end until this is fixed. Unlike the `gcc` branch (verified against
  a real, unmodified project with no changes needed), `norcroftng` is not
  yet usable for real project builds — it's a mechanically-correct
  selector pointed at a compiler with this one known, pre-existing gap.
  The proposed fix below (swap in real CLX's `fname`) would resolve this
  the same way it resolves the general requirement; not otherwise
  attempted here, since it's the same substantial, licensing-flagged piece
  of work already recorded as a proposal, not something to do as a side
  effect of wiring up a Makefile selector.

## Proposals

- Once the CLX `fname` swap is done, add tests exercising exactly the
  cases the user listed (`c.main`, `main.c` with a `c/main` fallback,
  `foo.h` with an `h/foo` fallback) as part of closing this area out —
  see [testing-and-validation.md](testing-and-validation.md). These
  weren't testable meaningfully before the swap, since the current
  implementation is known not to handle them.
