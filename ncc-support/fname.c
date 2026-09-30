/* Copyright 2025 Piers Wombwell
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "fname.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int is_sep(int c) { return c=='/' || c=='\\'; }
static int match_suffix(const char *ext, size_t elen, const char *list) {
    const char *p;

    if (!ext || elen==0) return 0;
    // list like: "c C h H c++ ..." — space-separated tokens

    for (p = list; *p;) {
        const char *q;
        size_t n;

        while (*p==' ') ++p;

        q = p;

        while (*q && *q!=' ') ++q;
        n = (size_t)(q-p);
        if (n==elen && strncasecmp(ext, p, elen)==0)
            return 1;
        p = q;
    }
    return 0;
}

/* A bare "NAME:" prefix (no slash before the colon, and not at position 0 -
 * so a leading "/" is never mistaken for one) is RISC OS's own
 * library-search-variable convention, used directly by this environment's
 * own Makefiles (eg "-IC:"). Only recognised when there's no slash before
 * it, matching real CLX's own volume-prefix detection - a colon appearing
 * after a '/' (eg inside a genuine path component) is never a volume. */
static int find_volume_prefix(const char *s, size_t *vlen)
{
    const char *p;
    for (p = s; *p && !is_sep(*p); ++p) {
        if (*p == ':' && p != s) {
            *vlen = (size_t)(p - s);
            return 1;
        }
    }
    return 0;
}

static void unix_fname_parse(const char *file,
                             const char *suffixlist,
                             UnparsedName *un)
{
    const char *s, *last_sep, *p, *leaf, *last_dot;
    size_t len, vlen;

    memset(un, 0, sizeof *un);
    if (!file)
        return;

    s = file;

    if (find_volume_prefix(s, &vlen)) {
        un->vol = s;
        un->vlen = vlen;
        un->type |= FNAME_ROOTED;
        s = s + vlen + 1;
    }

    len = strlen(s);

    // Rooted?
    if (len && (is_sep(s[0]))) {
        un->type |= FNAME_ROOTED;
    }

    // Split path / leaf at last separator
    last_sep = NULL;
    p = NULL;
    for (p = s; *p; ++p) {
        if (is_sep(*p))
            last_sep = p;
    }

    leaf = last_sep ? last_sep+1 : s;
    un->path = s;
    un->plen = (size_t)(leaf - s);

    // Split root/ext at last '.'
    last_dot = NULL;
    for (p = leaf; *p; ++p) {
        if (*p == '.')
            last_dot = p;
    }

    if (last_dot && last_dot != leaf && last_dot[1] != '\0') {
        un->root = leaf;
        un->rlen = (size_t)(last_dot - leaf);
        un->extn = last_dot + 1;
        un->elen = strlen(un->extn);
    } else {
        un->root = leaf;
        un->rlen = strlen(leaf);
        un->extn = NULL;
        un->elen = 0;
    }

    // Optionally: only “recognised” include suffixes count as extn
    if (suffixlist && un->extn && !match_suffix(un->extn, un->elen, suffixlist)) {
        // treat as “no extension” from the compiler’s POV
        un->root = leaf;
        un->rlen = strlen(leaf);
        un->extn = NULL;
        un->elen = 0;
    }

    // RISC OS's own on-disk convention writes the "extension" as the
    // directory immediately containing the leaf (eg "c/main", "h/foo")
    // rather than a dot-suffix. Recognise it whenever the leaf itself had
    // no dot-suffix extension, by checking the last path component
    // against the same suffix list - this is what makes a real AMU
    // Makefile's "c/main"/"h/foo" arguments and #include candidates work
    // (see design/riscos-build/filenames-and-paths.md). Doesn't need an
    // existence check: unlike "main.c" (ambiguous - might genuinely mean
    // the literal file, see driver.c/compiler.c's own fallback handling),
    // a path already written in this directory-first shape is
    // unambiguous.
    if (!un->extn && un->plen > 0 && suffixlist) {
        const char *dirleaf = un->path + un->plen - 1; /* the separator just before the leaf */
        const char *prev_sep = NULL;
        const char *q;
        for (q = un->path; q < dirleaf; ++q)
            if (is_sep(*q)) prev_sep = q;
        {
            const char *comp = prev_sep ? prev_sep + 1 : un->path;
            size_t complen = (size_t)(dirleaf - comp);
            if (match_suffix(comp, complen, suffixlist)) {
                un->extn = comp;
                un->elen = complen;
                un->type |= FNAME_EXTN_ASDIR;
                un->plen = (size_t)(comp - un->path);
            }
        }
    }

    un->un_pathlen = 0;
}

#ifndef COMPILING_ON_RISC_OS
void fname_parse(const char *fname, const char *suffixlist, UnparsedName *un)
{
    return unix_fname_parse(fname, suffixlist, un);
}

/* Uppercase a "NAME"/"Name" volume/variable reference into an environment
 * variable name, turning '$' into '_' the same way this environment's own
 * shared Makefiles already do for eg "Lib$Dir" -> "LIB_DIR" (see
 * crosscompile/help/makefiles.md's variable-naming convention). */
static void env_name_from_ref(const char *ref, size_t len, char *out, size_t outsz)
{
    size_t i;
    for (i = 0; i < len && i + 1 < outsz; ++i) {
        char c = ref[i];
        out[i] = (c == '$') ? '_' : (char)toupper((unsigned char)c);
    }
    out[i] = '\0';
}

/* Expand "<VAR>" references embedded anywhere in a RISC OS style path
 * argument (eg "<Lib$Dir>.GetOpt."), writing the result (with every
 * remaining '.' converted to '/', matching RISC OS's own dot-as-directory-
 * separator convention for a path with no real slashes) into out. A
 * reference to an unset variable is left as literal "<VAR>" text, which
 * will simply fail to open later - graceful, not a crash. */
static size_t expand_path_text(const char *text, size_t len, char *out, size_t n, size_t maxName)
{
    const char *s = text, *end = text + len;
    while (s < end && n + 1 < maxName) {
        if (*s == '<') {
            const char *close = memchr(s, '>', (size_t)(end - s));
            if (close) {
                char envname[64], value[256];
                env_name_from_ref(s + 1, (size_t)(close - s - 1), envname, sizeof envname);
                {
                    const char *v = getenv(envname);
                    size_t vlen;
                    if (!v) { value[0] = '\0'; vlen = 0; }
                    else { strncpy(value, v, sizeof value - 1); value[sizeof value - 1] = '\0'; vlen = strlen(value); }
                    if (v) {
                        size_t m = vlen;
                        if (m > maxName - 1 - n) m = maxName - 1 - n;
                        memcpy(out + n, value, m);
                        n += m;
                        s = close + 1;
                        /* RISC OS: "<Var>.tail" means value/tail - the dot
                         * right after the reference is a separator, not
                         * literal text. */
                        if (s < end && *s == '.') {
                            if (n + 1 < maxName) out[n++] = '/';
                            ++s;
                        }
                        continue;
                    }
                }
            }
            /* No '>' found, or variable unset: copy the '<' literally and
             * carry on - see the function comment. */
        }
        out[n++] = (*s == '.') ? '/' : *s;
        ++s;
    }
    return n;
}

int fname_unparse(UnparsedName *un,
                  unparsedFNameType how,
                  char *out,
                  size_t maxName)
{
    size_t n = 0;
    if (!out || maxName==0) return 0;

    /* "-IC:"-style volume/variable prefix - expand a single-directory
     * environment value directly; a genuinely multi-directory value (or
     * an unset one) is left as literal "NAME:" text instead. This matches
     * the real riscos-cc's own behaviour, including its same limitation
     * for a multi-directory value (confirmed empirically against the
     * installed tool, not assumed - see design/riscos-build/
     * filenames-and-paths.md), not a gap this reimplementation adds. */
    if (un->vol) {
        char envname[64];
        const char *value;
        env_name_from_ref(un->vol, un->vlen, envname, sizeof envname);
        value = getenv(envname);
        if (value && !strchr(value, ',')) {
            size_t m = strlen(value);
            if (m > maxName-1) m = maxName-1;
            memcpy(out, value, m);
            n = m;
            if (n && out[n-1] != '/' && n < maxName-1)
                out[n++] = '/';
        } else {
            size_t m = un->vlen;
            if (m > maxName-1) m = maxName-1;
            memcpy(out, un->vol, m);
            n = m;
            if (n < maxName-1) out[n++] = ':';
        }
    }

    // path
    if (un->plen) {
        size_t m = un->plen;
        if (m > maxName-1-n) m = maxName-1-n;
        memcpy(out+n, un->path, m);
        n += m;
    }
    if (how == FNAME_AS_PATH) {
        /* If it looks like a directory (no extension), keep the leaf too.
         * The leaf itself may still be a dotted RISC OS path fragment (eg
         * "<Lib$Dir>.GetOpt.") rather than a single plain name, so run it
         * through expand_path_text() rather than a plain copy. */
        if (un->elen == 0 && un->rlen > 0) {
            int is_dot_marker = (un->rlen == 1 && un->root[0] == '.')
                              || (un->rlen == 2 && un->root[0] == '.' && un->root[1] == '.');
            if (n && out[n-1] != '/' && out[n-1] != '\\' && n < maxName-1)
                out[n++] = '/';
            if (is_dot_marker) {
                /* A bare "." or ".." is the ordinary Unix relative-
                 * directory marker (eg from "-I." or "-I.."), not a
                 * RISC OS dotted path fragment - leave its dot(s) alone
                 * rather than running it through expand_path_text(),
                 * which would otherwise turn "-I." into "-I/". */
                size_t m = un->rlen;
                if (m > maxName-1-n) m = maxName-1-n;
                memcpy(out+n, un->root, m); n += m;
            } else {
                n = expand_path_text(un->root, un->rlen, out, n, maxName);
            }
            if (n < maxName-1) out[n++] = '/';
        }

        out[n] = '\0';
        return (int)n;
    }

    // name = ext "/" root (RISC OS on-disk shape), or root "." ext (literal)
    if (un->type & FNAME_EXTN_ASDIR) {
        if (un->elen && n < maxName-1) {
            size_t m = un->elen; if (m > maxName-1-n) m = maxName-1-n;
            memcpy(out+n, un->extn, m); n += m;
            if (n < maxName-1) out[n++] = '/';
        }
        if (un->rlen && n < maxName-1) {
            size_t m = un->rlen; if (m > maxName-1-n) m = maxName-1-n;
            memcpy(out+n, un->root, m); n += m;
        }
    } else {
        if (un->rlen && n < maxName-1) {
            size_t m = un->rlen; if (m > maxName-1-n) m = maxName-1-n;
            memcpy(out+n, un->root, m); n += m;
        }
        if (un->elen && n+1 < maxName-1) {
            size_t m;
            out[n++] = '.';
            m = un->elen; if (m > maxName-1-n) m = maxName-1-n;
            memcpy(out+n, un->extn, m); n += m;
        }
    }
    out[n] = '\0';

    // Tell callers where the path ends in THIS buffer
    un->un_pathlen = un->plen > n ? n : un->plen;
    return (int)n;
}

#else

// RISC OS file parsing. Probably somewhat broken as I'm doing it from memory.
void fname_parse(const char *fname, const char *suffixlist, UnparsedName *un)
{
    const char *p, *last_sep = NULL, *prev_sep = NULL;
    size_t len;

    memset(un, 0, sizeof *un);
    if (!fname)
        return;

    if (fname[0] == '/' || fname[0] == '\\') {
        unix_fname_parse(fname, suffixlist, un);
        return;
    }

    len = strlen(fname);
    if (len == 0)
        return;

    p = fname + len - 1;
    while (p >= fname) {
        if (*p == '.' || *p == ':') {
            if (last_sep == NULL) {
                last_sep = p;
                if (*p == ':')
                    break;
            } else {
                prev_sep = p;
                break; // got all we're looking for.
            }
        }

        p--;
    }
    // Note: if either separator is a colon, that colon needs preserving.

    // Determine if rooted. This will be a '$', or '@' either at the start
    // of the path, or after the last colon.
    if (len && (fname[0] == '$' || fname[0] == '@')) {
        un->type |= FNAME_ROOTED;
    }

    if (!last_sep) {
        /* No separator at all: no path, no extn, whole string is root */
        un->root = fname;
        un->rlen = len;

#if 0
        fprintf(stderr, "fname_parse: %s p'%.*s' f'%.*s' e'%.*s' t:%d\n",
                fname, un->plen, un->path, un->rlen, un->root, un->elen, un->extn, un->type);
#endif
        return;
    }

    // Leaf root is after last_sep
    un->root = last_sep + 1;
    un->rlen = (size_t)(fname + len - (last_sep + 1));

    // adjust last_sep if it's a colon to preseve it. It now points at the char
    // after that segment regardless of it it's a colon or dot. In the case
    // of it being a colon, there can't be an extension.
    if (*last_sep == ':') {
        last_sep++;
    } else {
        // A filename consists of leaf + ext. We don't know which is which
        // yet.

        // First component (p) is either the string from prev_sep to last_sep,
        // or from the start of the passed in fname to last_sep.
        p = prev_sep ? prev_sep + 1 : fname;

        // adjust prev_sep if it's a colon to preserve it (after suffix matching)
        if (prev_sep && *prev_sep == ':')
            prev_sep++;

        if (suffixlist && match_suffix(p, last_sep - p, suffixlist)) {
            // ext.filename
            un->extn = p;
            un->elen = last_sep - p;

            un->path = fname;
            un->plen = prev_sep ? prev_sep - fname : 0;

#if 0
            fprintf(stderr, "fname_parse: %s p'%.*s' f'%.*s' e'%.*s' t:%d\n",
                    fname, un->plen, un->path, un->rlen, un->root, un->elen, un->extn, un->type);
#endif
            return;
        } else if (suffixlist && match_suffix(un->root, un->rlen, suffixlist)) {
            // filename.ext: What we thought was the leafname is a valid suffix.
            un->extn = un->root;
            un->elen = un->rlen;

            un->root = p;
            un->rlen = last_sep - p;

            un->path = fname;
            un->plen = prev_sep ? prev_sep - fname : 0;

#if 0
            fprintf(stderr, "fname_parse: %s p'%.*s' f'%.*s' e'%.*s' t:%d\n",
                    fname, un->plen, un->path, un->rlen, un->root, un->elen, un->extn, un->type);
#endif
            return;
        }
    }

    // No extension.
    un->path = fname;
    un->plen = last_sep - fname;

#if 0
    fprintf(stderr, "fname_parse: %s p'%.*s' f'%.*s' e'%.*s' t:%d\n",
            fname, un->plen, un->path, un->rlen, un->root, un->elen, un->extn, un->type);
#endif
}

#define APPEND(c) do { *t++ = (c); if (t == t_max) goto end; } while (0);

// driver.c gives this delightful example:
/*  ^ -> .. (several times) + up to 2 extra path separators + a NUL. */
int fname_unparse(UnparsedName *un,
                  unparsedFNameType how,
                  char *out,
                  size_t maxName)
{
    const char *s, *s_end, *t_max = out + maxName;
    char *t = out;

    un->un_pathlen = 0;

    // Path. Map ['/' to '.'], ['.' to '/'], ['^' to '..']
    s = un->path;
    s_end = s + un->plen;

    while (s < s_end) {
        char c = *s++;

#if 0
        if (c == '/') {
            APPEND('/');
        } else if (c == '.') {
            APPEND('.');
        } else if (c == '^') {
            APPEND('.');
            APPEND('.');
        } else {
            APPEND(c);
        }
#endif
        APPEND(c);

    }

    // If last character (if there was one) wasn't a colon, append '.' then
    // leafname (root)
    if (t - out > 0 && t[-1] != ':')
        APPEND('.');

    // Append any extension.
    if (un->elen > 0) {
        s = un->extn;
        s_end = s + un->elen;

        while (s < s_end) {
            char c = *s++;

            APPEND(c);
        }

        APPEND('.');
    }

    s = un->root;
    s_end = s + un->rlen;

    while (s < s_end) {
        char c = *s++;

        if (c == '/') {
            APPEND('/');
        } else if (c == '.') {
            APPEND('.');
        } else {
            APPEND(c);
        }
    }

    if (how == FNAME_AS_PATH || un->rlen == 0) {
        APPEND('\0');

#if 0
        fprintf(stderr, "fname_unparse %d: p%u'%.*s' f%u'%.*s' e%u'%.*s' munged to: %s\n",
                how, un->plen, un->plen, un->path, un->rlen, un->rlen, un->root, un->elen, un->elen, un->extn, out);
#endif

        un->un_pathlen = (int)(t-out);

        return un->un_pathlen;
    }

    APPEND('\0');

end:
    out[maxName - 1] = '\0';

    if (t >= t_max)
        return -1;

#if 0
    fprintf(stderr, "fname_unparse: p%u'%.*s' f%u'%.*s' e%u'%.*s' munged to: %s\n",
            un->plen, un->plen, un->path, un->rlen, un->rlen, un->root, un->elen, un->elen, un->extn, out);
#endif

    un->un_pathlen = (int)(t-out);

    return un->un_pathlen;
}
#endif
