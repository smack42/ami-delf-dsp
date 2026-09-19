/*****************************************************************************

    mhidelfina.library - MHI driver for Delfina DSP
    Copyright (C) 2026  Michael Henke

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, see <https://www.gnu.org/licenses/>.

*****************************************************************************/


#include <proto/exec.h>
#include <exec/resident.h>
#include <exec/initializers.h>
#include "mhidelfina.h"

/*** don't try to run this! ***/
LONG
returnError(void)
{
    return -1;
}



static const ULONG libInitTable[4]; /* prototype */

static const struct Resident romTag = 
{
    RTC_MATCHWORD,
    (struct Resident *)&romTag,
    (struct Resident *)libInitTable,
    RTF_AUTOINIT,
    VERSION,
    NT_LIBRARY,
    0,
    libname,
    idstring,
    (APTR)libInitTable
};



/* global vars */
struct ExecBase *SysBase = NULL;
const char libname[]  = LIBNAME;
const char idstring[] = IDSTRING;



/*** initialize library ***/
static struct Library* __asm
i_libInit(register __a0 BPTR seglist, register __d0 struct mhidelfinaBase *mybase, register __a6 struct ExecBase *sysbase)
{
    KPutStr("i_libInit()\n");
    mybase->segList = seglist;
    SysBase = sysbase;
    if (libInitLibraries() == FALSE)
    {
        KPutStr("ERROR: libInitLibraries() failed\n");
        return NULL;
    }
    return &mybase->libNode;   /* OK */
}



/*** called by OpenLibrary() ***/
static struct Library* __asm
i_libOpen(register __a6 struct mhidelfinaBase *mybase)
{
    KPutStr("i_libOpen()\n");
    mybase->libNode.lib_Flags &= ~LIBF_DELEXP;
    mybase->libNode.lib_OpenCnt++;
    return &mybase->libNode;
}



/*** remove lib from memory ***/
static BPTR __asm
i_libExpunge(register __a6 struct mhidelfinaBase *mybase)
{
    if (mybase->libNode.lib_OpenCnt == 0)
    {
        BPTR seglist = mybase->segList;
        libExpungeLibraries();
        Remove((struct Node *) mybase);
        FreeMem((STRPTR)mybase - mybase->libNode.lib_NegSize,
                (ULONG)(mybase->libNode.lib_NegSize + mybase->libNode.lib_PosSize));
        KPutStr("i_libExpunge() done\n");
        return seglist;
    }
    mybase->libNode.lib_Flags |= LIBF_DELEXP;
    KPutStr("i_libExpunge() delayed\n");
    return NULL;
}



/*** called by CloseLibrary() ***/
static BPTR __asm
i_libClose(register __a6 struct mhidelfinaBase *mybase)
{
    KPutStr("i_libClose()\n");
    if ((--mybase->libNode.lib_OpenCnt) == 0)
    {
#ifndef DEBUG
        if (mybase->libNode.lib_Flags & LIBF_DELEXP)
#endif
            return i_libExpunge(mybase);
    }
    return NULL;
}



/*** mandatory reserved library function ***/
static ULONG
i_libReserved(void)
{
    return 0;
}



static const APTR libVectors[] =
{
    (APTR) i_libOpen,
    (APTR) i_libClose,
    (APTR) i_libExpunge,
    (APTR) i_libReserved,
    (APTR) i_MHIAllocDecoder,
    (APTR) i_MHIFreeDecoder,
    (APTR) i_MHIQueueBuffer,
    (APTR) i_MHIGetEmpty,
    (APTR) i_MHIGetStatus,
    (APTR) i_MHIPlay,
    (APTR) i_MHIStop,
    (APTR) i_MHIPause,
    (APTR) i_MHIQuery,
    (APTR) i_MHISetParam,
    (APTR) -1
};



struct LibInitData
{
    UBYTE i_Type;     UBYTE o_Type;     UBYTE  d_Type;     UBYTE p_Type;
    UBYTE i_Name;     UBYTE o_Name;     STRPTR d_Name;
    UBYTE i_Flags;    UBYTE o_Flags;    UBYTE  d_Flags;    UBYTE p_Flags;
    UBYTE i_Version;  UBYTE o_Version;  UWORD  d_Version;
    UBYTE i_Revision; UBYTE o_Revision; UWORD  d_Revision;
    UBYTE i_IdString; UBYTE o_IdString; STRPTR d_IdString;
    ULONG endmark;
};

static const struct LibInitData libInitData =
{
    0xA0, (UBYTE) OFFSET(Node,    ln_Type),      NT_LIBRARY,                   0,
    0x80, (UBYTE) OFFSET(Node,    ln_Name),      libname,
    0xA0, (UBYTE) OFFSET(Library, lib_Flags),    LIBF_SUMUSED|LIBF_CHANGED,    0,
    0x90, (UBYTE) OFFSET(Library, lib_Version),  VERSION,
    0x90, (UBYTE) OFFSET(Library, lib_Revision), REVISION,
    0x80, (UBYTE) OFFSET(Library, lib_IdString), idstring,
    0
};



static const ULONG libInitTable[4] =
{
    (ULONG)sizeof(struct mhidelfinaBase),
    (ULONG)libVectors,
    (ULONG)&libInitData,
    (ULONG)i_libInit
};

