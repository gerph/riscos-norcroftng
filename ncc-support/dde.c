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

#include "dde.h"
#include "fname.h"
#include "throwback.h"

const char* dde_desktop_prefix = 0;
int dde_throwback_flag = 0;

#if defined(FOR_ACORN) && defined(COMPILING_ON_RISC_OS)
#include "globals.h"  // for 'sourcefile'
#include "compiler.h" // for FNAME_SUFFIXES

#include <stdlib.h>
#include <string.h>
#include <kernel.h>

#define DDEUtils_Prefix 0x42580

/* Sets the desktop's Throwback filename prefix to <directory of fname>
 * <dde_desktop_prefix>, so a receiver rewrites the paths this tool
 * reports through the prefix registered by '-desktop <prefix>'. Only
 * meaningful - and only called by the real cc tool this is modelled on -
 * when that option was actually given; a compile without '-desktop'
 * leaves the desktop's prefix untouched, matching cc's own dde.c intent
 * (not its code - see design/throwback.md).
 */
void dde_prefix_init(const char* fname)
{
    if (dde_desktop_prefix) {
        UnparsedName un;
        char* prefix;
        _kernel_swi_regs regs;

        fname_parse(fname, FNAME_SUFFIXES, &un);

        prefix = malloc(un.plen + strlen(dde_desktop_prefix) + 1);
        memcpy(prefix, un.path, un.plen);
        strcpy(prefix + un.plen, dde_desktop_prefix);

        regs.r[0] = (int)prefix;
        _kernel_swi(DDEUtils_Prefix, &regs, &regs);

        free(prefix);
    }
}

void dde_sourcefile_init(void)
{
}

void dde_throwback_send(unsigned int severity, unsigned int line, const char* msg)
{
    Throwback((seriousness_t)severity, (char*)sourcefile, (int)line, (char*)msg);
}

#endif
