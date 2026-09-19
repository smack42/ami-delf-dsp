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
#include <exec/memory.h>
#include "mhidelfina.h"

/* global vars */
struct DosLibrary *DOSBase = NULL;  /* init -> expunge */
struct Library *DelfinaBase = NULL; /* init -> expunge */
struct mhidelfinaHandle *theHandle = NULL; /* MHIAllocDecoder -> MHIFreeDecoder */



BOOL
libInitLibraries(void)
{
    KPutStr("libInitLibraries()\n");
    DOSBase = (struct DosLibrary*) OpenLibrary("dos.library", 37);
    DelfinaBase = OpenLibrary("delfina.library", 4);
    return (BOOL) (DOSBase != NULL && DelfinaBase != NULL);
}



void
libExpungeLibraries(void)
{
    KPutStr("libExpungeLibraries()\n");
    if (DOSBase)
    {
        CloseLibrary((struct Library*) DOSBase);
        DOSBase = NULL;
    }
    if (DelfinaBase)
    {
        CloseLibrary(DelfinaBase);
        DelfinaBase = NULL;
    }
}



static void
decoderInit(void)
{
    MPEG_init();
}

static void
decoderClose(void)
{
    MPEG_close();
}





struct mhibufferNode*
getFilledBufferFromMHI(void)
{
    struct mhibufferNode *n = (struct mhibufferNode*) RemHead((struct List*) &theHandle->list_queued);
    theHandle->mhioutofdata = (n == NULL);
    return n;
}

void
returnEmptyBufferToMHI(struct mhibufferNode *n)
{
    AddTail((struct List*) &theHandle->list_getempty, (struct Node*) n);
    Signal(theHandle->mhitask, theHandle->mhisignal);
}





APTR __asm
i_MHIAllocDecoder(register __a0 struct Task *task, register __d0 ULONG mhisignal)
{
    struct mhidelfinaHandle *h;
    KPutStr("i_MHIAllocDecoder()\n");
    if (task == NULL)
    {
        KPutStr("ERROR: null task\n");
        return NULL;
    }
    Forbid(); /* theHandle -> Permit() */
    if (theHandle != NULL)
    {
        Permit(); /* theHandle <- Forbid() */
        KPutStr("ERROR: already allocated\n");
        return NULL;
    }
    h = (struct mhidelfinaHandle*) AllocMem(sizeof(struct mhidelfinaHandle), MEMF_PUBLIC|MEMF_CLEAR);
    if (h == NULL)
    {
        Permit(); /* theHandle <- Forbid() */
        KPutStr("ERROR: no memory for handle\n");
        return NULL;
    }
    theHandle = h;
    Permit(); /* theHandle <- Forbid() */
    h->mhitask = task;
    h->mhisignal = mhisignal;
    h->mhistatus = MHIF_STOPPED;
    h->mhioutofdata = 0;
    h->mhivolume = 100;
    h->mhipanning = 50;
    h->delfVolumeLeft = h->delfVolumeRight = DELF_MAX_VOLUME;
    NewList((struct List*) &h->list_queued);
    NewList((struct List*) &h->list_getempty);
    NewList((struct List*) &h->list_unused);
    return h;
}





void __asm
i_MHIFreeDecoder(register __a3 APTR handle)
{
    struct mhidelfinaHandle *h = (struct mhidelfinaHandle*) handle;
    APTR pt;
    KPutStr("i_MHIFreeDecoder()\n");
    if (h == NULL || h != theHandle)
    {
        KPutStr("ERROR: null or wrong handle\n");
        return;
    }
    i_MHIStop(h);
    while ((pt = RemHead((struct List*) &h->list_unused)))
    {
        FreeMem(pt, sizeof(struct mhibufferNode));
    }
    theHandle = NULL;
    FreeMem(h, sizeof(struct mhidelfinaHandle));
}





BOOL __asm
i_MHIQueueBuffer(register __a3 APTR handle, register __a0 APTR buffer, register __d0 ULONG size)
{
    struct mhidelfinaHandle *h = (struct mhidelfinaHandle*) handle;
    struct mhibufferNode *node;
    KPutStr("i_MHIQueueBuffer()\n");
    if (h == NULL || h != theHandle || buffer == NULL || size == 0)
    {
        KPutStr("ERROR: null or wrong handle or null buffer or zero size\n");
        return FALSE;
    }
    node = (struct mhibufferNode*) RemHead((struct List*) &h->list_unused);
    if (node == NULL)
    {
        node = (struct mhibufferNode*) AllocMem(sizeof(struct mhibufferNode), MEMF_PUBLIC);
        if (node == NULL)
        {
            KPutStr("ERROR: no memory for node\n");
            return FALSE;
        }
    }
    node->buffer = buffer;
    node->size = size;
    Disable();
    AddTail((struct List*) &h->list_queued, (struct Node*) node);
    Enable();
    decoderInit();
    return TRUE;
}





APTR __asm
i_MHIGetEmpty(register __a3 APTR handle)
{
    struct mhidelfinaHandle *h = (struct mhidelfinaHandle*) handle;
    struct mhibufferNode *node;
    KPutStr("i_MHIGetEmpty()\n");
    if (h == NULL || h != theHandle)
    {
        KPutStr("ERROR: null or wrong handle\n");
        return NULL;
    }
    Disable();
    node = (struct mhibufferNode*) RemHead((struct List*) &h->list_getempty);
    Enable();
    if (node != NULL)
    {
        AddTail((struct List*) &h->list_unused, (struct Node*) node);
        return node->buffer;
    }
    return NULL;
}





UBYTE __asm
i_MHIGetStatus(register __a3 APTR handle)
{
    struct mhidelfinaHandle *h = (struct mhidelfinaHandle*) handle;
    KPutStr("i_MHIGetStatus()\n");
    if (h == NULL || h != theHandle)
    {
        KPutStr("ERROR: null or wrong handle\n");
        return MHIF_STOPPED;
    }
    return (UBYTE) (h->mhioutofdata ? MHIF_OUT_OF_DATA : h->mhistatus);
}





void __asm
i_MHIPlay(register __a3 APTR handle)
{
    struct mhidelfinaHandle *h = (struct mhidelfinaHandle*) handle;
    KPutStr("i_MHIPlay()\n");
    if (h == NULL || h != theHandle)
    {
        KPutStr("ERROR: null or wrong handle\n");
        return;
    }
    h->mhistatus = MHIF_PLAYING;
    decoderInit();
}





void __asm
i_MHIStop(register __a3 APTR handle)
{
    struct mhidelfinaHandle *h = (struct mhidelfinaHandle*) handle;
    APTR pt;
    KPutStr("i_MHIStop()\n");
    if (h == NULL || h != theHandle)
    {
        KPutStr("ERROR: null or wrong handle\n");
        return;
    }
    h->mhistatus = MHIF_STOPPED;
    decoderClose();
    while ((pt = RemHead((struct List*) &h->list_getempty)))
    {
        AddTail((struct List*) &h->list_unused, (struct Node*) pt);
    }
    while ((pt = RemHead((struct List*) &h->list_queued)))
    {
        AddTail((struct List*) &h->list_unused, (struct Node*) pt);
    }
    h->mhioutofdata = 0;
}





void __asm
i_MHIPause(register __a3 APTR handle)
{
    struct mhidelfinaHandle *h = (struct mhidelfinaHandle*) handle;
    KPutStr("i_MHIPause()\n");
    if (h == NULL || h != theHandle)
    {
        KPutStr("ERROR: null or wrong handle\n");
        return;
    }
    if      (h->mhistatus == MHIF_PLAYING) h->mhistatus = MHIF_PAUSED;
    else if (h->mhistatus == MHIF_PAUSED)  h->mhistatus = MHIF_PLAYING;
}





ULONG __asm
i_MHIQuery(register __d1 ULONG query)
{
    KPutStr("i_MHIQuery()\n");
    switch(query)
    {
    case MHIQ_DECODER_NAME:
        return (ULONG) "Delfina DSP";
    case MHIQ_DECODER_VERSION:
        return (ULONG) idstring;
    case MHIQ_AUTHOR:
        return (ULONG) "Michael Henke";
    case MHIQ_IS_HARDWARE:
        return MHIF_TRUE;
    case MHIQ_MPEG1:
    case MHIQ_LAYER2:
    case MHIQ_LAYER3:
    case MHIQ_VARIABLE_BITRATE:
    case MHIQ_JOINT_STEREO:
        return MHIF_SUPPORTED;
    case MHIQ_VOLUME_CONTROL:
    case MHIQ_PANNING_CONTROL:
        return MHIF_SUPPORTED;
    default:
        return MHIF_UNSUPPORTED;
    }
}





void __asm
i_MHISetParam(register __a3 APTR handle, register __d0 UWORD param, register __d1 ULONG value)
{
    struct mhidelfinaHandle *h = (struct mhidelfinaHandle*) handle;
    ULONG volume = DELF_MAX_VOLUME;
    KPutStr("i_MHISetParam()\n");
    if (h == NULL || h != theHandle)
    {
        KPutStr("ERROR: null or wrong handle\n");
        return;
    }
    /* Overall sound volume. 100 is max, 0 is silence. */
    if (param == MHIP_VOLUME) h->mhivolume = (value > 100 ? 100 : value);
    /* Sound panning. 50 is centre (default), 0 is full left and 100 is full right. */
    else if (param == MHIP_PANNING) h->mhipanning = (value > 100 ? 50 : value);
    else return;
    /* compute values for Delfina */
    switch (h->mhivolume)
    {
        case 0: volume = 0;
        case 100: break;
        default: volume = (volume * h->mhivolume) / 100;
    }
    h->delfVolumeLeft = h->delfVolumeRight = volume;
    switch (h->mhipanning)
    {
        case 0: h->delfVolumeRight = 0;
        case 50: break;
        case 100: h->delfVolumeLeft = 0; break;
        default:
            if (h->mhipanning < 50) h->delfVolumeRight = (volume * h->mhipanning) / 50;
            if (h->mhipanning > 50) h->delfVolumeLeft = (volume * (100 - h->mhipanning)) / 50;
    }
    h->doChangeVolume = 1;
}
