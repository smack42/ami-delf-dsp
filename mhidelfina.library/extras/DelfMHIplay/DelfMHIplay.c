/*****************************************************************************
 *
 *    DelfMHIplay - MHI player with some extra features for Delfina DSP
 *    Copyright (C) 2026  Michael Henke
 *
 *    This program is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2 of the License, or
 *    (at your option) any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License along
 *    with this program; if not, see <https://www.gnu.org/licenses/>.
 *
 *****************************************************************************/


#include <proto/exec.h>
#include <exec/libraries.h>
#include <exec/memory.h>
#include <proto/dos.h>
#include <dos/dos.h>
#include <proto/asl.h>
#include <libraries/asl.h>
#include <clib/asyncio_protos.h>
#include <libraries/asyncio.h>
#include <proto/mhi.h>
#include <libraries/mhi.h>

/* proposed MHI extensions - experimental implementation */
#include "//src/mhi_extensions.h"

UBYTE version[] = "\0$VER: DelfMHIplay 0.2_dev (03.10.2026)";

long __oslibversion = 37; /* require OS 2.04+ */

UBYTE **files_pt = NULL, *filename = NULL, ende = 0, verbose = 0;
LONG asyncBufSize = 64 * 1024, aslfileindex = 0;
struct FileRequester *aslfilereq = NULL;
BPTR lock_newdir = NULL, lock_olddir = NULL;
struct AsyncFile *file = NULL;

struct Library *MHIBase = NULL;
BYTE mhiSignal = -1, anim4[] = "-\\|/";
ULONG mhiSigMask = 0, mhiNumBuffers = 4, mhiBufSize = 8 * 1024;
APTR mhiHandle = NULL, *mhiBuffers = NULL;

UBYTE mhidelfina[] = "MHI/mhidelfina.library", *mhiDriverName = mhidelfina;
UBYTE argTemplate[] = "FILES/M,-D=DRIVER/K,-V=VERBOSE/S,-H=HELP/S";
LONG args[4] = { 0 };
struct RDArgs *rdargs = NULL;

void usage(void)
{
    Printf(
        "\nusage: DelfMHIplay [options] [files]\n"
        "  if no files are specified then a file requester appears\n"
        "options are:\n"
        "  -d or DRIVER <library> ... use this MHI driver\n"
        "      default: %s\n"
        "  -v or VERBOSE ............ display more information\n"
        "  -h or HELP ............... display this text\n"
        "use these keys during playback:\n"
        "  Ctrl-C ... skip to the next file\n"
        "  Ctrl-D ... quit\n\n"
        , mhidelfina
    );
}





BOOL initMHI(void)
{
    ULONG i;
    Printf("  using driver %s\n", mhiDriverName);
    if (NULL == (MHIBase = OpenLibrary(mhiDriverName, 0)))
    {
        Printf("ERROR: MHI OpenLibrary failed\n");
        return FALSE;
    }
    if (-1 == (mhiSignal = AllocSignal(-1)))
    {
        Printf("ERROR: MHI AllocSignal failed\n");
        return FALSE;
    }
    mhiSigMask = 1L << mhiSignal;
    if (NULL == (mhiHandle = MHIAllocDecoder(FindTask(NULL), mhiSigMask)))
    {
        Printf("ERROR: MHIAllocDecoder failed\n");
        return FALSE;
    }
    if (NULL == (mhiBuffers = AllocVec(mhiNumBuffers * sizeof(APTR), MEMF_PUBLIC | MEMF_CLEAR)))
    {
        Printf("ERROR: MHI memory allocation failed\n");
        return FALSE;
    }
    for (i = 0;  i < mhiNumBuffers;  ++i)
    {
        if (NULL == (mhiBuffers[i] = AllocVec(mhiBufSize, MEMF_PUBLIC)))
        {
            Printf("ERROR: MHI memory allocation failed\n");
            return FALSE;
        }
    }
    if (verbose)
    {
        Printf( "\n  driver name: %s\n"
                "       author: %s\n"
                "      version: %s\n",
                MHIQuery(MHIQ_DECODER_NAME),
                MHIQuery(MHIQ_AUTHOR),
                MHIQuery(MHIQ_DECODER_VERSION) );
        SetVar(MHI_EXT_SETPARAM_STREAMINFO, "1", -1, GVF_LOCAL_ONLY);
    }
    return TRUE; /* success */
}





void closeMHI(void)
{
    if (mhiHandle) { MHIFreeDecoder(mhiHandle); }
    if (mhiBuffers)
    {
        ULONG i;
        for (i = 0;  i < mhiNumBuffers;  ++i)
        {
            if (mhiBuffers[i]) { FreeVec(mhiBuffers[i]); }
        }
        FreeVec(mhiBuffers);
    }
    if (mhiSignal >= 0) { FreeSignal((LONG) mhiSignal); }
    if (MHIBase) { CloseLibrary(MHIBase); }
    DeleteVar(MHI_EXT_SETPARAM_STREAMINFO, GVF_LOCAL_ONLY);
}





UBYTE readAndQueue(APTR buffer)
{
    ULONG readLen;
    if (0 != (readLen = ReadAsync(file, buffer, mhiBufSize)))
    {
        MHIQueueBuffer(mhiHandle, buffer, readLen);
    }
    return (UBYTE)(readLen != mhiBufSize  ?  1  :  0);  /* eof */

}





UBYTE playFile(void)
{
    UBYTE result = 0, eof = 0, anim = 0;
    ULONG i, signals = 0;
    APTR buffer;
    Printf("\n  file: %s\n", filename);
    if (NULL == (file = OpenAsync(filename, MODE_READ, asyncBufSize)))
    {
        Printf("ERROR: unable to open file\n");
        return result;
    }
    /* startup: pre-fill buffers */
    for (i = 0;  !eof && i < mhiNumBuffers;  ++i)
    {
        eof = readAndQueue(mhiBuffers[i]);
    }
    MHIPlay(mhiHandle);
    /* streaming: refill buffers as needed */
    while (!eof)
    {
        signals = Wait(mhiSigMask | SIGBREAKF_CTRL_C | SIGBREAKF_CTRL_D);
        if (signals & SIGBREAKF_CTRL_C) { break; }
        if (signals & SIGBREAKF_CTRL_D) { result = 2; break; }
        if (signals & mhiSigMask)
        {
            while (!eof && (buffer = MHIGetEmpty(mhiHandle)))
            {
                eof = readAndQueue(buffer);
            }
            if (MHIGetStatus(mhiHandle) == MHIF_OUT_OF_DATA)
            {
                MHIPlay(mhiHandle);
            }
        }
        if (verbose)
        {
            UBYTE si[80];
            si[0] = 0;
            GetVar(MHI_EXT_QUERY_STREAMINFO, si, sizeof(si), GVF_LOCAL_ONLY);
            Printf("\r  %s  playing %lc ", si, anim4[anim++ & 3]);
            Flush(Output());
        }
    }
    /* end: wait for buffers to drain */
    while (MHIGetStatus(mhiHandle) == MHIF_PLAYING)
    {
        if (signals & SIGBREAKF_CTRL_C) { break; }
        if (signals & SIGBREAKF_CTRL_D) { result = 2; break; }
        signals = Wait(mhiSigMask | SIGBREAKF_CTRL_C | SIGBREAKF_CTRL_D);
        while ((buffer = MHIGetEmpty(mhiHandle))); /* do nothing */
    }
    if (verbose) { Printf("stop\n"); }
    MHIStop(mhiHandle);
    CloseAsync(file);
    return result;
}





int main(void)
{
    int rc = RETURN_OK;
    Printf("\33[1m%s\33[0m\n", &version[7]);

    if (!(rdargs = ReadArgs(argTemplate, args, NULL)))
    {
        Printf("ERROR: ReadArgs failed\n");
        goto exit_error;
    }
    if (args[3]) { usage(); goto exit_clean; }
    if (args[2]) { verbose = 1; }
    if (args[1]) { mhiDriverName = (UBYTE*) args[1]; }
    if ( ! initMHI())
    {
        goto exit_error;
    }

    files_pt = (char**)args[0];
    if ( ! files_pt)
    {
        if( ! (aslfilereq = (struct FileRequester*) AllocAslRequestTags(
            ASL_FileRequest,
            ASL_Hail, (ULONG)"DelfMHIplay: play audio files",
            ASL_FuncFlags, FILF_MULTISELECT | FILF_PATGAD,
            TAG_DONE)))
        {
            Printf("ERROR: AllocAslRequestTags failed\n");
            goto exit_error;
        }
    }

    do /* file requesters loop */
    {
        if (aslfilereq)
        {
            if (AslRequest(aslfilereq, NULL))
            {
                aslfileindex = 0;
                if (aslfilereq->rf_Dir)
                {
                    Printf("\n  dir:  %s\n", aslfilereq->rf_Dir);
                    lock_newdir = Lock(aslfilereq->rf_Dir, SHARED_LOCK);
                    lock_olddir = CurrentDir(lock_newdir);
                }
            }
            else { goto exit_clean; }
        }
        do /* files loop */
        {
            if (aslfilereq)
            {
                if (aslfileindex == 0 && aslfilereq->rf_NumArgs == 0)
                {
                    filename = aslfilereq->rf_File;
                }
                else if (aslfileindex < aslfilereq->rf_NumArgs)
                {
                    filename = aslfilereq->rf_ArgList[aslfileindex].wa_Name;
                }
                else filename = NULL;
                ++aslfileindex;
            }
            else { filename = *files_pt++; }
            if (filename) { ende = playFile(); }
            else          { ende = 1; }
        }
        while (ende == 0);
        if (aslfilereq)
        {
            if (lock_olddir) { CurrentDir(lock_olddir); lock_olddir = NULL; }
            if (lock_newdir) { UnLock(lock_newdir); lock_newdir = NULL; }
        }
    }
    while (aslfilereq && ende < 2);

    goto exit_clean;
exit_error:
    rc = RETURN_FAIL;
exit_clean:
    closeMHI();
    if (aslfilereq) { FreeAslRequest(aslfilereq); }
    if (rdargs) { FreeArgs(rdargs); }
    return rc;
}
