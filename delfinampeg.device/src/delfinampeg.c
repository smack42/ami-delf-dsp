/*****************************************************************************

    delfinampeg.device - mpeg.device for Delfina DSP
    Copyright (C) 2000-2003  Michael Henke

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

*****************************************************************************/


/*#define DEBUG - enable debug output to serial port (4sushi)*/
/*#define DEBUG*/

#include <proto/exec.h>
#include <proto/dos.h>
#include <exec/memory.h>
#include <exec/devices.h>
#include <exec/errors.h>
#include <exec/interrupts.h>
#include <devices/melodympeg.h>
#include <dos/var.h>

#ifdef DEBUG
extern void KPutStr(STRPTR);
extern LONG KPutChar(LONG);
#pragma **note: DEBUG mode is enabled
/* force SAS/C to print a warning */
#endif

extern struct DelfObj DSP56K_PCM;
extern struct DelfObj DSP56K_MP2;
extern struct DelfObj DSP56K_MP3;
#include <libraries/delfina.h>
#include "PCM.h"
#include "MP2.h"
#include "MP3.h"

/** static LONG pow43tab[8206]; **/
#include "MP3_pow43tab.h"

extern char DevName;    /* "delfinampeg.device" */
extern struct Library *DelfinaBase; /* initialized by device Init routine */

struct devunit
{
    struct Unit unit;
    ULONG   unitnum;
    struct List ioreqlist;
    struct IOMPEGReq *currioreq;
    UBYTE   *currpt;
    ULONG   currlen;
    ULONG   layer, freqidx, mono, firstheader;
    ULONG   volumeleft, volumeright, pause, initdelf_ok;
    BYTE    cleanup_flag, cleanup_count;
    struct DelfPrg *prg_pcm, *prg_mp2, *prg_mp3;
    struct DelfModule *mod_pcm;
    DELFPTR mem_il_mp2, mem_ip_mp2;
    DELFPTR mem_il_mp3, mem_ip_mp3;
    struct Interrupt delfint, softint;
    ULONG   intkey;
    UBYTE   framebuf[4096], bitresbuf[4096];
    UWORD   bitresoffset, bitresok, framebufstate;
    UWORD   framebufoffset, framebufleft, III_main_data_size;
    UWORD   delfcopysize, II_translate, II_jsbound, modext;
    UWORD   II_forcemono, III_forcemono, forcemono;
    ULONG   II_dacrate, III_dacrate, dacrate;
    UBYTE   *delfcopypt;
};

/* possible values for 'framebufstate' */
#define FBS_GETHEADER           0
#define FBS_GETFRAMEDATA        1
#define FBS_FILLED              2

#define MPG_MD_STEREO           0
#define MPG_MD_JOINT_STEREO     1
#define MPG_MD_DUAL_CHANNEL     2
#define MPG_MD_MONO             3
#define HDR_MPEG1               0xfff80000
#define HDR_CONSTANT            0xfffe0c00  /* layer, sampling frequency */

void* C_initunit(ULONG unitnum);
ULONG C_expungeunit(struct devunit *unit);
void  C_setpause(struct devunit *unit, struct IOMPEGReq *iomr);
void  C_setvolume(struct devunit *unit, struct IOMPEGReq *iomr);
void  C_reset(struct devunit *unit);
void  C_flush(struct devunit *unit);
ULONG C_write(struct devunit *unit, struct IOMPEGReq *iomr);
static ULONG initDelfina(struct devunit *unit);
static void  cleanupDelfina(struct devunit *unit);
static LONG __asm lev6_IntServer(register __a1 struct devunit *unit);
static LONG __asm soft_IntServer(register __a1 struct devunit *unit);
static UWORD GetBits(UWORD num);

static struct TagItem tagdone={TAG_DONE,0};
static ULONG volumeleft=0x400000, volumeright=0x400000;  /* default: 100% */
static UWORD *gb_pt=NULL, gb_buf=0, gb_num=0;

static const ULONG mpgfreq[4]={44100,48000,32000,0};
static const UWORD mpgbitrate[3][16]=
        { {0,32,64,96,128,160,192,224,256,288,320,352,384,416,448,0}, /* I */
          {0,32,48,56,64,80,96,112,128,160,192,224,256,320,384,0},   /* II */
          {0,32,40,48,56,64,80,96,112,128,160,192,224,256,320,0} }; /* III */
static const UBYTE mp2translate[3][2][16] =
        { { { 0,2,2,2,2,2,2,0,0,0,1,1,1,1,1,0 } ,    /* 44100 stereo */
            { 0,2,2,0,0,0,1,1,1,1,1,1,1,1,1,0 } } ,  /* 44100 mono   */
          { { 0,2,2,2,2,2,2,0,0,0,0,0,0,0,0,0 } ,    /* 48000 stereo */
            { 0,2,2,0,0,0,0,0,0,0,0,0,0,0,0,0 } } ,  /* 48000 mono   */
          { { 0,3,3,3,3,3,3,0,0,0,1,1,1,1,1,0 } ,    /* 32000 stereo */
            { 0,3,3,0,0,0,1,1,1,1,1,1,1,1,1,0 } } }; /* 32000 mono   */
static const UBYTE mp2sblimit[4]={27,30,8,12};



/** called by OpenDevice() **/
void*
C_initunit(ULONG unitnum)
{
    struct devunit *u;
#ifdef DEBUG
KPutStr("C_initunit\n");
#endif
    u=(struct devunit*)AllocVec(sizeof(struct devunit),MEMF_PUBLIC|MEMF_CLEAR);
    if(!u) return(NULL);
    u->unit.unit_MsgPort.mp_Node.ln_Type=NT_MSGPORT;
    u->unit.unit_MsgPort.mp_Node.ln_Name= &DevName;
    u->unit.unit_MsgPort.mp_Flags=PA_IGNORE;
    NewList(&u->unit.unit_MsgPort.mp_MsgList);
    u->unitnum=unitnum;
    u->volumeleft=volumeleft;
    u->volumeright=volumeright;
    NewList(&u->ioreqlist);
    {
        UBYTE *varbuf;
        LONG  varlen, args[4]={0,0,0,0};
        struct RDArgs *rdargs, *rdargs2;
        struct DosLibrary *DOSBase;
        if((DOSBase=(struct DosLibrary*)OpenLibrary("dos.library",37)))
        {
            if((varbuf=AllocVec(1024,MEMF_PUBLIC|MEMF_CLEAR)))
            {
                if((varlen=GetVar("DELFINAMPEG",varbuf,1024,LV_VAR))>0)
                {
#ifdef DEBUG
KPutStr("DELFINAMPEG var: '"); KPutStr(varbuf); KPutStr("'\n");
#endif
                    if((rdargs=AllocDosObject(DOS_RDARGS,&tagdone)))
                    {
                        rdargs->RDA_Source.CS_Buffer=varbuf;
                        rdargs->RDA_Source.CS_Length=varlen;
                        rdargs->RDA_Source.CS_CurChr=0;
                        rdargs->RDA_Flags=RDAF_NOPROMPT;
                        rdargs->RDA_DAList=0;
                        rdargs2=ReadArgs("L2MONO/S,L2RATE/K/N,L3MONO/S,L3RATE/K/N",args,rdargs);
                        if(args[0]) u->II_forcemono=1;
                        if(args[1]) u->II_dacrate=(*(LONG*)args[1]);
                        if(args[2]) u->III_forcemono=1;
                        if(args[3]) u->III_dacrate=(*(LONG*)args[3]);
                        FreeArgs(rdargs2);
                        FreeDosObject(DOS_RDARGS,rdargs);
                    }
                }
                FreeVec(varbuf);
            }
            CloseLibrary((struct Library*)DOSBase);
        }
    }
    return(u);
}



/** called by CloseDevice() **/
ULONG
C_expungeunit(struct devunit *u)
{
    ULONG unitnum=u->unitnum;
#ifdef DEBUG
KPutStr("C_expungeunit\n");
#endif
    C_flush(u);
    FreeVec(u);
    return(unitnum);
}



/** called by MPEGCMD_PAUSE **/
void
C_setpause(struct devunit *u, struct IOMPEGReq *iomr)
{
#ifdef DEBUG
KPutStr("C_setpause\n");
#endif
    u->pause=iomr->iomr_Arg1;
}



/** called by MPEGCMD_SETAUDIOPARAMS **/
void
C_setvolume(struct devunit *u, struct IOMPEGReq *iomr)
{
    struct MPEGAudioParams *ap;
#ifdef DEBUG
KPutStr("C_setvolume\n");
#endif
    ap=(struct MPEGAudioParams*)iomr->iomr_Req.io_Data;
    if(ap)
    {
        u->volumeleft =volumeleft =(0x400000*((ap->map_VolumeLeft >>8)&0xff))/0xff;
        u->volumeright=volumeright=(0x400000*((ap->map_VolumeRight>>8)&0xff))/0xff;
        if(u->prg_pcm)
        {
            Delf_Run(u->prg_pcm->prog+PROG_PCM_INIT, 0, 0,
                 (ULONG)(u->forcemono ? 1 : u->mono),
                 u->volumeleft,
                 mpgfreq[u->freqidx],
                 u->volumeright );
        }
    }
}



/** called by CMD_RESET **/
void
C_reset(struct devunit *u)
{
#ifdef DEBUG
KPutStr("C_reset--");
#endif
    u->volumeleft=u->volumeright=volumeleft=volumeright=0x400000;
    u->pause=0;
    C_flush(u);
}



/** called by CMD_FLUSH **/
void
C_flush(struct devunit *u)
{
    struct IOMPEGReq *iomr;
#ifdef DEBUG
KPutStr("C_flush\n");
#endif
    cleanupDelfina(u);
    u->currlen=0; u->currpt=NULL; 
    u->layer=u->freqidx=u->mono=u->firstheader=0;
    u->bitresoffset=u->bitresok=0;
    u->framebufstate=FBS_GETHEADER;
    if((iomr=u->currioreq))
    {
        iomr->iomr_Req.io_Error=IOERR_ABORTED;
        ReplyMsg(&iomr->iomr_Req.io_Message);
        u->currioreq=NULL;
    }
    while((iomr=(struct IOMPEGReq*)RemHead(&u->ioreqlist)))
    {
        iomr->iomr_Req.io_Error=IOERR_ABORTED;
        ReplyMsg(&iomr->iomr_Req.io_Message);
    }
}



/** called by CMD_WRITE **/
ULONG
C_write(struct devunit *u, struct IOMPEGReq *iomr)
{
#ifdef DEBUG
/*KPutStr("C_write\n");*/
#endif
    if( (iomr->iomr_Req.io_Length==0) ||
        (iomr->iomr_Req.io_Data==NULL) ||
        (iomr->iomr_StreamType!=MPEGSTREAM_AUDIO) )
    {
        iomr->iomr_Req.io_Error=MPEGERR_BAD_PARAMETER;
        return(1); /*ERROR*/
    }
    Disable();
    AddTail(&u->ioreqlist, (struct Node*)iomr);
    Enable();
#ifdef DEBUG
KPutStr("C_write...AddTail\n");
#endif
    /*even if initDelfina() returns an error we must not return it  */
    /*here because the request is already queued in our ioreqlist.  */
    /*(the requests in ioreqlist will be replied by C_flush() later)*/
    if(!u->initdelf_ok) initDelfina(u);
    return(0);
}



/** called by C_write() **/
static ULONG
initDelfina(struct devunit *u)
{
    while(!u->firstheader)
    {
        ULONG header, i;
        UBYTE *pt, *ptmax;
#ifdef DEBUG
KPutStr("initDelfina...firstheader\n");
#endif
        /** find frame header and extract some info: layer, freqidx, mono **/
        if(!u->currioreq)
        {
            if(!(u->currioreq=(struct IOMPEGReq*)RemHead(&u->ioreqlist))) return(1); /*ERROR*/
            u->currpt=u->currioreq->iomr_Req.io_Data;
            u->currlen=u->currioreq->iomr_Req.io_Length;
        }
        pt=u->currpt; ptmax=pt+u->currlen; header=0;
retry_firstheader:
        i=0;
        while(pt<ptmax)
        {
            header=(header<<8)|(ULONG)(*pt++);
            if( ((header&HDR_MPEG1)==HDR_MPEG1) &&  /*sync*/
                (((header>>17)&3)!=0) &&            /*layer!=4*/
                (((header>>17)&3)!=3) &&            /*layer!=1*/
                (((header>>12)&15)!=0) &&           /*bitrate!=0*/
                (((header>>12)&15)!=15) &&          /*bitrate!=15*/
                (((header>>10)&3)!=3)               /*freqidx!=3*/
              ) {i=1; break;} /*pattern match*/
        }
        if(i) /*found something!*/
        {
            /**check next header (for safer recognition!)**/
            i=((ULONG)mpgbitrate[3-((header>>17)&3)][(header>>12)&15]*144000)/mpgfreq[(header>>10)&3]+((header>>9)&1)-4;
            if((pt+i)>=(ptmax-4)) goto retry_firstheader; /*beyond this buffer!*/
            i=((ULONG)(*(pt+i))<<24)|((ULONG)(*(pt+i+1))<<16)|((ULONG)(*(pt+i+2))<<8)|(ULONG)(*(pt+i+3));
            if( ((i&HDR_CONSTANT)!=(header&HDR_CONSTANT)) ||
                (((i>>12)&15)==0) ||
                (((i>>12)&15)==15) ) goto retry_firstheader; /*header mismatch!*/
            /**now we are quite sure that it really is an MPEG frame header**/
            u->currpt=pt-4;
            u->currlen=ptmax-pt+4;
            u->firstheader=header;
            u->layer=4-((header>>17)&3);
            u->freqidx=(header>>10)&3;
            u->mono=( (((header>>6)&3)==MPG_MD_MONO) ? 1 : 0 );

            u->forcemono= u->layer==2 ? u->II_forcemono : u->III_forcemono;
            u->dacrate= u->layer==2 ? u->II_dacrate : u->III_dacrate;
        }
        else /*not found*/
        {
#ifdef DEBUG
KPutStr("initDelfina...firstheader...MPEGERR_CMD_FAILED\n");
#endif
            u->currioreq->iomr_Req.io_Error=MPEGERR_CMD_FAILED;
            u->currioreq->iomr_MPEGError=MPEGEXTERR_STREAM_MISMATCH;
            ReplyMsg(&u->currioreq->iomr_Req.io_Message);
            u->currioreq=NULL; u->currpt=NULL; u->currlen=0;
        }
    }


    if(!u->prg_pcm)
    {
#ifdef DEBUG
KPutStr("initDelfina...prg_pcm\n");
#endif
        if(!(u->prg_pcm=Delf_AddPrg(&DSP56K_PCM))) return(1); /*ERROR*/
        Delf_Run(u->prg_pcm->prog+PROG_PCM_INIT, 0, 0,
                 (ULONG)(u->forcemono ? 1 : u->mono),
                 u->volumeleft,
                 mpgfreq[u->freqidx],
                 u->volumeright );
    }

    if(u->layer==2)
    {
        if(!u->prg_mp2)
        {
#ifdef DEBUG
KPutStr("initDelfina...prg_mp2\n");
#endif
            if(!u->mem_il_mp2) u->mem_il_mp2=Delf_AllocMem(INTL_MP2_DATL, DMEMF_LDATA|DMEMF_INTERNAL|DMEMF_ALIGN_64);
            if(!u->mem_ip_mp2) u->mem_ip_mp2=Delf_AllocMem(INTP_MP2_PROG, DMEMF_PROG|DMEMF_INTERNAL);
            if(!(u->prg_mp2=Delf_AddPrg(&DSP56K_MP2))) return(1); /*ERROR*/
            Delf_Run(u->prg_mp2->prog+PROG_MP2_INIT, 0, 0, u->mem_il_mp2, u->mem_ip_mp2, 0, 0);
            Delf_Poke(u->prg_mp2->ydata+DATY_MP2_FORCEMONO, DMEMF_YDATA, (ULONG)u->forcemono);
        }
    }
    else if(u->layer==3)
    {
        if(!u->prg_mp3)
        {
#ifdef DEBUG
KPutStr("initDelfina...prg_mp3\n");
#endif
            if(!u->mem_il_mp3) u->mem_il_mp3=Delf_AllocMem(INTL_MP3_DATL, DMEMF_LDATA|DMEMF_INTERNAL|DMEMF_ALIGN_64);
            if(!u->mem_ip_mp3) u->mem_ip_mp3=Delf_AllocMem(INTP_MP3_PROG, DMEMF_PROG|DMEMF_INTERNAL);
            if(!(u->prg_mp3=Delf_AddPrg(&DSP56K_MP3))) return(1); /*ERROR*/
            Delf_Run(u->prg_mp3->prog+PROG_MP3_INIT, 0, 0, u->mem_il_mp3, u->mem_ip_mp3, u->freqidx, 0);
            Delf_CopyMem(pow43tab, (void*)(u->prg_mp3->ydata+DATY_MP3_POW43TAB), 8206*4, DCPF_FROM_AMY|DCPF_YDATA|DCPF_32BIT);
            Delf_Poke(u->prg_mp3->ydata+DATY_MP3_FORCEMONO, DMEMF_YDATA, (ULONG)u->forcemono);
        }
    }
    else return(1); /*ERROR: unsupported layer*/

    if(!u->intkey)
    {
#ifdef DEBUG
KPutStr("initDelfina...AddIntServer\n");
#endif
        u->softint.is_Code=(void(*)(void))soft_IntServer;
        u->softint.is_Data=u;
        u->softint.is_Node.ln_Type=NT_INTERRUPT;
        u->softint.is_Node.ln_Pri=0;
        u->softint.is_Node.ln_Name="delfinampeg_SoftInt";
        u->softint.is_Node.ln_Succ=NULL;
        u->softint.is_Node.ln_Pred=NULL;
        u->delfint.is_Code=(void(*)(void))lev6_IntServer;
        u->delfint.is_Data=u;
        if(!(u->intkey=Delf_AddIntServer(u->prg_pcm->prog+PROG_PCM_INTKEY,&u->delfint))) return(1); /*ERROR*/
    }
    if(!u->mod_pcm)
    {
#ifdef DEBUG
KPutStr("initDelfina...AddModule\n");
#endif
        if(!(u->mod_pcm=Delf_AddModule(DM_Inputs, 0, DM_Outputs, 1,
                            DM_Code, u->prg_pcm->prog+PROG_PCM_MODULE,
                            DM_Freq, u->dacrate ? u->dacrate*1000 : mpgfreq[u->freqidx],
                            DM_Name, &DevName,
                            0 ))) return(1); /*ERROR*/
    }

    u->initdelf_ok=1;
#ifdef DEBUG
KPutStr("initDelfina...ok\n");
#endif
    return(0);
}



/** called by C_flush() **/
static void
cleanupDelfina(struct devunit *u)
{
    ULONG pause_tmp;
#ifdef DEBUG
KPutStr("cleanupDelfina\n");
#endif
    pause_tmp=u->pause;
    if(u->mod_pcm)
    {
        u->cleanup_flag=1;
        u->cleanup_count=2;
        /** wait until the currently running DSP routine has finished **/
        /** this avoids the nasty deadlock with delfina.library 4.14  **/
        while(u->cleanup_count>0) u->pause=1;
        Delf_RemModule(u->mod_pcm); u->mod_pcm=NULL;
    }
    if(u->intkey)  {Delf_RemIntServer(u->intkey); u->intkey=0;}
    if(u->prg_mp2) {Delf_RemPrg(u->prg_mp2); u->prg_mp2=NULL;}
    if(u->prg_mp3) {Delf_RemPrg(u->prg_mp3); u->prg_mp3=NULL;}
    if(u->prg_pcm) {Delf_RemPrg(u->prg_pcm); u->prg_pcm=NULL;}
    if(u->mem_il_mp2) {Delf_FreeMem(u->mem_il_mp2,DMEMF_LDATA|DMEMF_INTERNAL); u->mem_il_mp2=NULL;}
    if(u->mem_ip_mp2) {Delf_FreeMem(u->mem_ip_mp2,DMEMF_PROG |DMEMF_INTERNAL); u->mem_ip_mp2=NULL;}
    if(u->mem_il_mp3) {Delf_FreeMem(u->mem_il_mp3,DMEMF_LDATA|DMEMF_INTERNAL); u->mem_il_mp3=NULL;}
    if(u->mem_ip_mp3) {Delf_FreeMem(u->mem_ip_mp3,DMEMF_PROG |DMEMF_INTERNAL); u->mem_ip_mp3=NULL;}
    u->cleanup_flag=0;
    u->pause=pause_tmp;
    u->initdelf_ok=0;
}



/* Delfina hardware interrupt - M68K Level 6 IRQ. */
/* highest priority - interrupts all other system activity. */
/* problem: interferes with timing-critical things like serial I/O. */
/* solution: do actual processing in software interrupt (low priority). */

static LONG __asm
lev6_IntServer(register __a1 struct devunit *u)
{
    Cause(&u->softint);
    return 0;
}


static LONG __asm
soft_IntServer(register __a1 struct devunit *u)
{
    if((u->pause) || (u->framebufstate!=FBS_FILLED))
    {
        if(u->cleanup_flag) u->cleanup_count--;
        else Delf_Run(u->prg_pcm->prog+PROG_PCM_MUTE,0,DRUNF_ASYNCH,0,0,0,0);
    }
    else
    {
        if(u->layer==2)
        {
            if(!Delf_Peek(u->prg_mp2->ydata+DATY_MP2_BUSY,DMEMF_YDATA))
            {
                Delf_CopyMem( u->delfcopypt,
                              (void*)(u->prg_mp2->xdata+DATX_MP2_INBUF),
                              (ULONG)u->delfcopysize,
                              DCPF_FROM_AMY|DCPF_XDATA|DCPF_24BIT );
                Delf_Run( u->prg_mp2->prog+PROG_MP2_DECODE, 0, DRUNF_ASYNCH,
                          u->mono,
                          Delf_Peek(u->prg_pcm->ydata+DATY_PCM_BUFPTR,DMEMF_YDATA),
                          (ULONG)u->II_translate,
                          (ULONG)u->II_jsbound );
                u->framebufstate=FBS_GETHEADER;
            }
        }
        else if(u->layer==3)
        {
            if(!Delf_Peek(u->prg_mp3->ydata+DATY_MP3_BUSY,DMEMF_YDATA))
            {
                if(u->bitresok)
                {
                    Delf_CopyMem( &u->bitresbuf[0],
                                  (void*)(u->prg_mp3->xdata+DATX_MP3_INBUF),
                                  (ULONG)(u->III_main_data_size+33),
                                  DCPF_FROM_AMY|DCPF_XDATA|DCPF_24BIT );
                    Delf_Run( u->prg_mp3->prog+PROG_MP3_DECODE, 0, DRUNF_ASYNCH,
                              u->mono,
                              Delf_Peek(u->prg_pcm->ydata+DATY_PCM_BUFPTR,DMEMF_YDATA),
                              (ULONG)u->modext,
                              0 );
                }
                u->framebufstate=FBS_GETHEADER;
            }
        }
    }

    if(u->framebufstate==FBS_GETHEADER)
    {
        /*find next valid frame header*/
        ULONG fh=u->firstheader&HDR_CONSTANT, ch=0;
        while( ((ch&HDR_CONSTANT)!=fh) ||   /*sync, layer, freqidx*/
               (((ch>>12)&15)==0) ||        /*bitrate!=0*/
               (((ch>>12)&15)==15) )        /*bitrate!=15*/
        {
            if(u->currioreq)
            {
                if(u->currlen)
                {
                    ch=(ch<<8)|(ULONG)(*u->currpt);
                    u->currpt++; u->currlen--;
                }
                else
                {
                    u->currioreq->iomr_Req.io_Actual=u->currioreq->iomr_Req.io_Length;
                    ReplyMsg(&u->currioreq->iomr_Req.io_Message);
                    u->currioreq=NULL; u->currpt=NULL; /*u->currlen=0;*/
                }
            }
            else
            {
                if(!(u->currioreq=(struct IOMPEGReq*)RemHead(&u->ioreqlist)))
                {
                    ch=0; break; /*ERROR*/
                }
                u->currpt=u->currioreq->iomr_Req.io_Data;
                u->currlen=u->currioreq->iomr_Req.io_Length;
            }
        }
        if(ch) /*found it!*/
        {
            u->framebufoffset=0;
            u->framebufleft=((ULONG)mpgbitrate[u->layer-1][(ch>>12)&15]*144000)/mpgfreq[(ch>>10)&3]+((ch>>9)&1)-4;
            u->II_translate=mp2translate[u->freqidx][u->mono][(ch>>12)&15];
            if(((ch>>6)&3)==MPG_MD_JOINT_STEREO)
            {
                u->modext=(ch>>4)&3;
                u->II_jsbound=(u->modext<<2)+4;
            }
            else
            {
                u->modext=0;
                u->II_jsbound=mp2sblimit[u->II_translate];
            }
            u->delfcopysize=u->framebufleft;
            u->delfcopypt= &u->framebuf[0];
            if(!((ch>>16)&1))
            {
                u->delfcopysize-=2;
                u->delfcopypt+=2;
            }
            u->framebufstate=FBS_GETFRAMEDATA;
        }
    }

    if(u->framebufstate==FBS_GETFRAMEDATA)
    {
        /*fetch frame data*/
#ifdef DEBUG
KPutChar('i');
#endif
        while(u->framebufleft)
        {
            if(u->currioreq)
            {
                if(u->currlen)
                {
                    ULONG i=u->currlen;
                    if(u->framebufleft<i) i=u->framebufleft;
                    CopyMem(u->currpt, &u->framebuf[u->framebufoffset], i);
                    u->currpt+=i; u->currlen-=i;
                    u->framebufoffset+=i; u->framebufleft-=i;
                    if(u->framebufleft==0) u->framebufstate=FBS_FILLED;
                }
                else
                {
                    u->currioreq->iomr_Req.io_Actual=u->currioreq->iomr_Req.io_Length;
                    ReplyMsg(&u->currioreq->iomr_Req.io_Message);
                    u->currioreq=NULL; u->currpt=NULL; /*u->currlen=0;*/
                }
            }
            else
            {
                if(!(u->currioreq=(struct IOMPEGReq*)RemHead(&u->ioreqlist)))
                {
                    break; /*ERROR*/
                }
                u->currpt=u->currioreq->iomr_Req.io_Data;
                u->currlen=u->currioreq->iomr_Req.io_Length;
            }
        }
        if((u->framebufstate==FBS_FILLED) && (u->layer==3))
        {
            UWORD md;
            /**extract main data size**/
            gb_pt=(UWORD*)(u->delfcopypt+2); gb_num=0;
            if(u->mono)
            {
                GetBits(9+5+4-16);
                md=GetBits(12);
            }
            else
            {
                GetBits(9+3+8-16);
                md=GetBits(12);
                GetBits(59-12);
                md+=GetBits(12);
                GetBits(59-12);
                md+=GetBits(12);
            }
            GetBits(59-12);
            md+=GetBits(12);
            u->III_main_data_size=(md+7)>>3;
            /**bit reservoir handling**/
            {
                UWORD main_data_begin, i;
                UBYTE *p0, *p1;
                p1=u->delfcopypt;
                main_data_begin= (UWORD)(*p1)<<1 | (UWORD)(*(p1+1))>>7;
                /* copy side information */
                p0= &u->bitresbuf[0];
                for(i=32; i>0; i--) *p0++= *p1++;
                /* copy previous main_data */
                p0++; /* &u->bitresbuf[33] */
                if(u->bitresoffset>=main_data_begin)
                {
                    u->bitresok=1;
                    p1=p0+u->bitresoffset-main_data_begin;
                    for(i=main_data_begin; i>0; i--) *p0++= *p1++;
                    u->bitresoffset=main_data_begin;
                }
                else
                {
                    u->bitresok=0; /* not enough data in reservoir */
                    p0+=u->bitresoffset;
                }
                /* copy current main_data */
                i=(u->mono) ? 17 : 32; /* side info length */
                p1=u->delfcopypt+i;    /* begin of main_data area */
                i=u->delfcopysize-i;   /* mainslots */
                u->bitresoffset+=i;
                for(; i>0; i--) *p0++= *p1++;
            }
        }
    }

    return 0;
}



static UWORD
GetBits(UWORD num)
{
    UWORD val=0;
    for(; num>0; num--)
    {
        if(gb_num==0)
        {
            gb_buf= *gb_pt++;
            gb_num= 16;
        }
        gb_num--;
        val+= val;
        val|= (gb_buf>>gb_num)&1;
    }
    return(val);
}
