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


#include <exec/libraries.h>
#include <exec/interrupts.h>
#include <dos/dos.h>
#include <libraries/mhi.h>
#include <libraries/delfina.h>


/* #define DEBUG = enable auto-expunge and debug output = larger and slower code! */
/*#define DEBUG*/


#define VERSION     1
#define REVISION    0
#define DATETXT     "06.10.2026"
#define VERSTXT     "1.1_dev"
#define LIBNAME     "mhidelfina.library"
#define IDSTRING    LIBNAME " " VERSTXT " (" DATETXT ")"

#define DELF_MAX_VOLUME 0x00400000



/* just a standard Library base */
struct mhidelfinaBase
{
    struct Library      libNode;
    BPTR                segList;
};

/* implemented audio decoders */
enum Decoder
{
    DEC_NONE = 0,
    DEC_MPEG1_L2,
    DEC_MPEG1_L3
};

/* used by MPEG decoder */
struct delfinaMPEG
{
    struct mhibufferNode *currnode;
    UBYTE   *currpt;
    ULONG   currlen;
    ULONG   layer, freqidx, mono, firstheader;
    BYTE    cleanup_flag, cleanup_count;
    struct DelfPrg *prg_pcm, *prg_mp2, *prg_mp3;
    struct DelfModule *mod_pcm;
    DELFPTR mem_il_mp2, mem_ip_mp2;
    DELFPTR mem_il_mp3, mem_ip_mp3;
    struct Interrupt delfint, softint;
    ULONG   intkey;
    UBYTE   framebuf[4096], bitresbuf[4096];
    UWORD   bitresoffset, bitresok, framebufstate, pause;
    UWORD   framebufoffset, framebufleft, III_main_data_size;
    UWORD   delfcopysize, II_translate, II_jsbound, modext;
    UWORD   II_forcemono, III_forcemono, forcemono;
    ULONG   II_dacrate, III_dacrate, dacrate;
    UBYTE   *delfcopypt;
    STRPTR  txtLayer, txtFreq, txtMode;
    ULONG   frameCount, frameBitrateSum;
};

/* used by MHI API-functions and internally as global vars */
struct mhidelfinaHandle
{
    struct MinList      list_queued, list_getempty, list_unused;
    struct Task         *mhitask;
    ULONG               mhisignal, mhivolume, mhipanning;
    ULONG               delfVolumeLeft, delfVolumeRight;
    UBYTE               mhistatus, mhioutofdata, doChangeVolume;
    enum Decoder        activedecoder;
    UBYTE               mhiExtStreamInfoBuffer[255], mhiExtDoStreamInfo;
    struct delfinaMPEG  mpeg;
};

/* manage MHI data buffers in list_queued, list_getempty, list_unused */
struct mhibufferNode
{
    struct MinNode minNode;
    APTR buffer;
    ULONG size;
};


#ifdef DEBUG
extern void KPutStr(STRPTR);
#pragma **note: DEBUG mode is enabled
/* force SAS/C to print a warning */
#else
#define KPutStr(STRPTR)
#endif


/* global vars */
extern const char libname[];
extern const char idstring[];
extern struct ExecBase *SysBase;
extern struct DosLibrary *DOSBase;
extern struct Library *DelfinaBase;
extern struct mhidelfinaHandle *theHandle;


/* functions in mhidelfina.c */
extern BOOL libInitLibraries(void);
extern void libExpungeLibraries(void);
extern struct mhibufferNode* getFilledBufferFromMHI(void);
extern void returnEmptyBufferToMHI(struct mhibufferNode *n);
extern APTR __asm   i_MHIAllocDecoder(register __a0 struct Task *task, register __d0 ULONG mhisignal);
extern void __asm   i_MHIFreeDecoder(register __a3 APTR handle);
extern BOOL __asm   i_MHIQueueBuffer(register __a3 APTR handle, register __a0 APTR buffer, register __d0 ULONG size);
extern APTR __asm   i_MHIGetEmpty(register __a3 APTR handle);
extern UBYTE __asm  i_MHIGetStatus(register __a3 APTR handle);
extern void __asm   i_MHIPlay(register __a3 APTR handle);
extern void __asm   i_MHIStop(register __a3 APTR handle);
extern void __asm   i_MHIPause(register __a3 APTR handle);
extern ULONG __asm  i_MHIQuery(register __d1 ULONG query);
extern void __asm   i_MHISetParam(register __a3 APTR handle, register __d0 UWORD param, register __d1 ULONG value);

/* functions in MPEGdecoder.c */
extern void MPEG_init(void);
extern void MPEG_close(void);
extern void MPEG_queryStreamInfo(void);
extern void MPEG_discardBuffers(void);
