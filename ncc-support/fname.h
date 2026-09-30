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

#pragma once

#include <stdio.h>

#define FNAME_ROOTED 0x1

/* Bit in UnparsedName.type recording which on-disk *shape* un->extn/
 * un->root describes, so fname_unparse knows which one to reconstruct,
 * and so a caller assembling its own candidate path (eg compiler.c's
 * #include search - see incl_search()'s own comment) can tell whether
 * un->extn already came from a directory component.
 *
 * Unset (default): un->extn came from a trailing ".ext" on the leafname
 *   (eg "main.c") - unparse as "root.ext".
 * Set: un->extn came from the path component immediately before the leaf
 *   (eg "c/main" - RISC OS's own on-disk convention, where the
 *   "extension" is a directory, not a dot-suffix) - unparse as
 *   "ext/root".
 */
#define FNAME_EXTN_ASDIR 0x100

typedef struct {
    const char* root;
    size_t rlen;
    const char* extn;
    size_t elen;
    const char* path;
    size_t plen;
    size_t un_pathlen;  // Seems to be length after unparsing.
    const char* vol;
    size_t vlen;
    size_t type;
} UnparsedName;

// I think path names are passed in (from the command line) in either the OS's
// native format, MS-DOS style, or unix style.
//
// fname_parse doesn't change anything, but sets pointers into the given string
// to point to the path, leafname ("root") and extension. root and extension
// can be missing.

extern void fname_parse(const char *file,
                        const char* suffixlist,
                        UnparsedName* un);

typedef enum { FNAME_AS_NAME, FNAME_AS_PATH } unparsedFNameType;

// fname_unparse takes the broken down path (which the compiler has probably
// modified, eg. to change the extension from 'c' to 'o'), and recreates an
// OS-format path.
//
// For RISC OS, this is joyous fun. The file extension is placed before the
// leafname, and I haven't figured out when we need to change slashes to dots.
extern int fname_unparse(UnparsedName*, unparsedFNameType,
                          char *new_file, size_t maxName);
