# RISC OS Throwback support

Not part of [riscos-n++](riscos-build/overview.md) — this is RISC OS-side
only (the `HOST=riscos` self-hosted native compiler), explicitly out of
scope for the cross-compiler design. Charles: "The next one to introduce
will be for the RISC OS-side only."

## Goal

Wire up the Throwback protocol (DDEUtils, used by desktop editors to
receive clickable warning/error/informational diagnostics with a
filename and line number) for the native, self-hosted Norcroft NG
compiler, matching what the reference `cc` tool does — but not by
copying `cc`'s own `dde.c`, only its intent. Charles: "we're not copying
that code, only the intent."

## Decisions

### Use CMunge's `throwback.c`/`throwback.h` wholesale, not `cc`'s `dde.c`

`commands/riscos-source/Sources/BuildUtils/CMunge/{c,h}/throwback` (Robin
Watts/Justin Fletcher, 1999-2000) provides a small, self-contained
`Throwback()`/`vThrowbackf()` API (`seriousness_t`: `s_warning`,
`s_error`, `s_seriouserror`, `s_information`) that already does the real
`DDEUtils_ThrowbackStart`/`Send`/`End` SWI sequence, with a harmless
`stderr`-printing fallback for non-`__riscos` hosts. Charles: "that's
my/Robin's code which is fine to copy wholesale." Copied verbatim into
`ncc-support/throwback.c`/`throwback.h`, keeping the original 1999-2000
copyright notice and no added SPDX line (Charles's explicit choice - he
has a direct, separately-granted usage right to reuse it here, so no
extra header was added).

Added unconditionally to the Makefile's `SUPPORT_SRCS` list, exactly like
every other `ncc-support/*.c` file (matching `dde.c`'s own existing
pattern) — its own `#ifdef __riscos` guard, unmodified, correctly selects
the real SWI-calling branch only when the file is compiled *by* a
RISC-OS-targeting Norcroft NG compiler (which auto-defines `__riscos` via
`ccacorn/options.h`'s `DRIVER_OPTIONS`, itself gated on `COMPILING_ON_ARM`
- defined for *any* `TARGET=riscos` build of Norcroft NG, cross or
native, not just `HOST=riscos`) and the harmless `stderr` stub otherwise
(eg when the plain host `cc`/gcc compiles `ncc-support/throwback.c` as
part of building `bin/ncc-riscos` itself in the ordinary cross-build).
Verified directly (see "Verification" below) — no Makefile-level
conditionalisation needed at all.

### `dde.c` becomes a thin wrapper around `Throwback()`

`ncc-support/dde.c`'s existing `dde_throwback_send()` (already correctly
gated behind `COMPILING_ON_RISC_OS`/only reachable from `main.c` when
`-throwback` was given - this plumbing already existed and needed no
change) now just calls `Throwback((seriousness_t)severity,
(char*)sourcefile, (int)line, (char*)msg)` directly, instead of its own
raw, rougher SWI calls (which had leftover `fprintf(stderr, ...)` debug
lines, no `ThrowbackEnd` call on any path, and no `Start`-failure
handling at all). Using `Throwback()` wholesale means `dde.c` inherits
CMunge's own error-tolerant behaviour (no return-value checking on any
SWI call) automatically, rather than needing a judgement call here about
fatal-vs-non-fatal mid-session failure — this is already proven safe in
production, since it's the exact code `riscos-cmunge` itself uses.

**Deliberately not replicated**: the reference `cc` tool's own `dde.c`
(read for intent only, at
`native-build-tools/riscos-source/Sources/BuildUtils/Tools/cc/riscos/c/dde`)
sends a `Throwback_ReasonProcessing` message once per file before the
first error/warning for it, matching the protocol's full documented
grammar. CMunge's `Throwback()` skips this (relying on receiver
leniency, which the protocol itself documents as acceptable). Scope was
kept to "use CMunge's code, call it for info/error/warning" rather than
also grafting in this extra behaviour from a different reference.

### Two real, separate bugs found and fixed in `dde_prefix_init()`

While confirming `-desktop`/`DDEUtils_Prefix` intent against `cc`'s real
`dde.c` (not copying it, just reading for behaviour):

- **It never used `dde_desktop_prefix` at all.** The existing
  implementation extracted the current file's directory (via
  `fname_parse`) and passed *that alone* to `DDEUtils_Prefix`, completely
  ignoring the string the user actually supplied via `-desktop <prefix>`.
  `cc`'s own version builds `<directory of current file>/<user's prefix
  string>` and registers *that* combined path - the whole point of the
  option. Fixed to match that intent, using Norcroft NG's own
  `fname_parse`-based approach rather than `cc`'s manual character-walking
  loop.
- **It fired unconditionally, even without `-desktop`.** `dde_desktop_prefix`
  defaults to `0`/unset; the existing code called `DDEUtils_Prefix` on
  every single compile regardless, which could affect the desktop's
  filing prefix context even when the user never asked for it. `cc`'s own
  version only does anything at all when `dde_desktop_prefix` is set.
  Fixed to match.
- A genuine off-by-one was also present: the old code allocated `plen + 1`
  bytes, copied `plen` bytes in, then wrote the NUL at `path[plen-1]`
  (overwriting the last real character) instead of `path[plen]`. Moot
  now the function was rewritten, but worth recording as a second,
  independent bug in the same few lines.

Confirmed directly (not assumed) that the real option name is `-desktop`
in both Norcroft NG's own `driver.c` and the reference `cc`'s own driver -
there is no separate `-dde` option in either tool. An earlier guess to the
contrary was corrected against the actual source before anything was
built on it.

### The diagnostic pipeline was missing its `BC_SEVERITY_INFO` case entirely

`main.c`'s `ErrorMessage()` - the single place every diagnostic (warning,
error, serious error, *and* informational) flows through via the
`BC_DIAGMSG` backchat callback - only had `switch` cases for
`BC_SEVERITY_WARN`/`ERROR`/`SERIOUS`. `BC_SEVERITY_INFO` is a real,
already-flowing severity (used by `misc.c`'s nested-context "in file
included from..." notes, which carry a genuine filename+line), but it
fell straight through the switch doing nothing for Throwback - this is
the concrete gap matching Charles's "information, errors and warnings"
framing, not something guessed at. Added `THROWBACK_INFO` (value `3`,
matching CMunge's `s_information`) to `dde.h`, and the missing `case
BC_SEVERITY_INFO: dde_throwback_send(THROWBACK_INFO, line, msg); break;`
to `main.c`.

## Verification

**Pyromaniac's own default Throwback behaviour**, checked properly via
documentation rather than grepping Pyromaniac's own source tree (a
process correction mid-session - see the `riscos-documentation` skill's
now-broadened trigger): `/riscos-source/pyromaniac/docs/FEATURES.md` and
`riscos-run --help-config` both confirm `Throwback.implementation`
defaults to `console` (prints straight to the VDU console), not `null` or
`posturl`. No special `*PyromaniacDebug`/`--debug` flag is needed or
exists for this - the default already gives direct, undecorated output.

**Codegen-level verification** (agreed with Charles as sufficient, over a
full desktop-receiver round-trip), done by compiling each changed file
directly with `bin/ncc-riscos` (the ordinary cross-compiler, which
already auto-defines `__riscos` for any `TARGET=riscos` compile - see
above) and inspecting the `-S` output directly:

- `ncc-support/throwback.c` compiles to real `_kernel_swi` calls with the
  correct SWI numbers: `0x42587` (Start), `0x42588` (Send), `0x42589`
  (End), plus a correct `atexit(Throwback_Shutdown)` registration - not
  the `stderr`-stub branch.
- `dde_prefix_init()` compiles to: an early return when
  `dde_desktop_prefix` is null; otherwise `fname_parse` + `malloc` +
  `memcpy` + `strcpy` to build the combined path, then `_kernel_swi` with
  R0 = `0x42580` (`DDEUtils_Prefix`) - confirmed by decoding the
  constant-load sequence (`mov r0,#9600` / `add r0,r0,#262144` =
  `0x2580` + `0x40000` = `0x42580`).
- `dde_throwback_send()` compiles to a tail-call into `Throwback()` with
  correctly shuffled arguments (severity, `sourcefile`, line, message in
  the right registers).
- `main.c`'s `ErrorMessage()` switch compiles to the correct mapping for
  all four cases, confirmed against `backchat.h`'s real enum values
  (`BC_SEVERITY_INFO=1`, `WARN=2`, `ERROR=3`, `SERIOUS=4` mapping to
  `THROWBACK_INFO=3`, `WARN=0`, `ERROR=1`, `SERIOUS=2` respectively).

Full existing test suite re-run after these changes: 85 passed, 1 known
pre-existing VFP failure (unchanged) - no regressions from the `dde.c`/
`main.c`/Makefile changes.

### Verified live, end-to-end - superseding the codegen-only bar above

Once the link was unblocked (see "Fixed (stopgap)" below), the actual
`bin/ncc,ff8` was built, copied into a scratch project, and run for real
under Pyromaniac (`riscos-run "ncc -throwback c.t -c -o o.t"`) against a
source file with a deliberate error and a deliberate warning. Real
DDEUtils SWI traffic, received and rendered by Pyromaniac's own default
`console` Throwback implementation:

```
Error: undeclared name, inventing 'extern int undeclared_thing'      (red, linked to c/t#3)
Warning: variable 'unused_variable' declared but not used            (yellow, linked to c/t#4)
Info: c.t: 1 warning, 1 error, 0 serious errors                      (blue, linked to c/t#0)
```

All three severities fired correctly with the right colours and
clickable file/line links (Pyromaniac's console implementation opens
`file://.../c/t#<line>`). The third line is the informational
end-of-file summary - exactly the message that the missing
`BC_SEVERITY_INFO` case would have silently dropped before this session's
fix, now confirmed reaching Throwback for real, not just compiling
correctly. `-desktop myprefix` was also exercised alongside `-throwback`
and ran cleanly with no crash or behavioural change to the diagnostics
above, confirming the rewritten `dde_prefix_init()` doesn't break
anything when actually given a prefix.

This fully supersedes the original "codegen-level is sufficient" bar
agreed at the start of this work - a real linked binary, a real
Pyromaniac desktop-console receiver, and all three severities were all
exercised for real.

**A genuinely unrelated, pre-existing bug was also found and fixed along
the way**: `ncc-support/fname.c` (from an earlier, unrelated session task)
had an unconditional, entirely unused `#include <unistd.h>` - the
`access()` call that needed it lives only in `driver.c`, correctly
guarded there. Harmless under the ordinary cross-build (plain host `cc`
happily parses glibc's `<unistd.h>`), but the *first* attempt at the
`HOST=riscos` native bootstrap build in this session used `bin/ncc-riscos`
itself to compile `ncc-support/fname.c`, and Norcroft NG's C front end
cannot parse modern glibc's GNU-attribute-laden `<unistd.h>` at all
(`__wur`, `__THROW`, `__nonnull`, etc. - none of these are attributes this
front end understands). Removed the unused include; the bootstrap
compile stage then completed cleanly.

## Fixed (stopgap): native `HOST=riscos` link, via symlinks

Found while trying to get a full desktop/Pyromaniac runtime test of the
linked `ncc,ff8` binary - not something introduced by this work, and not
really about Throwback at all, but it directly blocked verifying this
feature for real, so it was fixed as part of the same session rather than
left open:

- `HOST=riscos`'s chosen linker, `drlink`, isn't an installed tool in
  this environment at all (`make: drlink: No such file or directory`).
  Overriding `LD=riscos-link` on the command line gets past that (Make's
  command-line variable assignment overrides the Makefile's own `:=`).
- The link itself then failed: `riscos-link` reported `File
  build/obj/.../aetree.o not found` for an object file that demonstrably
  existed on disk (confirmed: real, valid AOF, correct size). Root-caused
  by direct reproduction, not guessed: `riscos-link` (and `drlink`) use
  the **real** CLX `fname` module, which does exactly the
  extension-as-directory inversion this session spent all day
  reimplementing a Norcroft-NG-specific equivalent of for the compiler
  itself (see
  [riscos-build/filenames-and-paths.md](riscos-build/filenames-and-paths.md))
  - unconditionally, with no literal-first fallback, for *any* recognised
  suffix including `.o` and `.a`. Confirmed directly: a bare `aetree.o`
  is looked up as `o/aetree`, and `stubs.a` as `a/stubs`; placing copies
  there resolves both immediately.
- Norcroft NG's own Makefile object/library layout (`build/obj/.../
  <name>.o`, `lib/stubs.a` - flat, extension-suffixed) is the outlier
  here, not the linker: "RISC OS code should largely use RISC OS format,
  and this project is an outlier" (Charles). Rather than restructure the
  whole build's layout now, `Makefile`'s link rules (`ensure_riscos_
  symlinks`, used by all six `HOST=riscos` binary targets) symlink every
  `.o`/`.a` prerequisite into a sibling `o/`/`a/` directory immediately
  before linking - eg `build/obj/.../mip/aetree.o` gets `build/obj/.../
  mip/o/aetree -> ../aetree.o` created alongside it. A no-op for the
  ordinary cross-build (`LD=$(CC)`, an ordinary host linker with no such
  quirk).
- This is explicitly a stopgap, not the real fix - see "Proposals" below.

## Proposals

- **Properly fix the `HOST=riscos` object/library layout**, rather than
  relying on the symlink stopgap above indefinitely: restructure
  `build/obj/.../<name>.o` into a genuine RISC-OS-style `build/obj/.../o/
  <name>` layout (and `lib/stubs.a` into `lib/a/stubs`) so the real
  linker's own lookup convention is satisfied directly, with no
  generated symlinks needed at all. Matches Charles's own framing -
  "this project is an outlier," not the linker - but is a larger,
  Makefile-wide layout change, not attempted as part of this session's
  Throwback work.
- Separately, find or install a real `drlink` (rather than always relying
  on the `LD=riscos-link` command-line override), if one is expected to
  exist in this environment at all - not investigated.
