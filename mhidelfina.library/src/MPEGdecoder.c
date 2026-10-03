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
#include <dos.h>

#include "mhidelfina.h"
#include "MPEG_PCM.h"
#include "MP2.h"
#include "MP3.h"

/** static UBYTE pow43tab[8207*3]; **/
#include "MP3_pow43tab.h"

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

static LONG __asm lev6_IntServer(void);
static LONG __asm soft_IntServer(void);

static UWORD GetBits(UWORD num);
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

static const STRPTR mpgModename[4] = { "stereo", "j-stereo", "dual-ch", "single-ch" };
static const STRPTR mpgfreqTxt[4]  = { "44.1", "48", "32", "0" };



/* PutChProc for RawDoFmt; SAS/C specific */
/* from Amiga ROM Kernel Reference Manual AmigaDOS by Thomas Richter */
static void prbuf(char c)
{
    __emit(0x16c0); /* move.b D0,(A3)+ */
}

void
MPEG_queryStreamInfo(void)
{
    ULONG args[5];
    if (theHandle->activedecoder != DEC_MPEG1_L2 && theHandle->activedecoder != DEC_MPEG1_L3) return;
    KPutStr("MPEG_queryStreamInfo\n");
    args[0] = (ULONG) theHandle->mpeg.txtLayer;
    args[1] = (ULONG) theHandle->mpeg.txtFreq;
    args[2] = (ULONG) theHandle->mpeg.txtMode;
    args[3] = theHandle->mpeg.frameCount == 0 ? 0 : theHandle->mpeg.frameBitrateSum / theHandle->mpeg.frameCount;
    args[4] = theHandle->mpeg.frameCount;
    RawDoFmt("MPEG-1 layer %s  %s kHz  %s  %ld kbps  frames=%ld",
        args, prbuf, theHandle->mhiExtStreamInfoBuffer);
}





void
MPEG_init(void)
{
    struct delfinaMPEG *u;
    enum Decoder decoder;
    if (theHandle->activedecoder != DEC_NONE) return;
    u = &theHandle->mpeg;
    while(!u->firstheader)
    {
        ULONG header, i;
        UBYTE *pt, *ptmax;
        KPutStr("MPEG_init...firstheader\n");
        /** find frame header and extract some info: layer, freqidx, mono **/
        if(!u->currnode)
        {
            if (!(u->currnode = getFilledBufferFromMHI()))
            {
                KPutStr("ERROR: no input data\n");
                return;
            }
            u->currpt  = u->currnode->buffer;
            u->currlen = u->currnode->size;
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
        if (i) /*found something!*/
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
            u->txtLayer = (u->layer == 3 ? "III" : "II");
            u->freqidx=(header>>10)&3;
            u->txtFreq = mpgfreqTxt[u->freqidx];
            u->mono=( (((header>>6)&3)==MPG_MD_MONO) ? 1 : 0 );
            u->txtMode = mpgModename[(header>>6)&3];

            u->forcemono= u->layer==2 ? u->II_forcemono : u->III_forcemono;
            u->dacrate= u->layer==2 ? u->II_dacrate : u->III_dacrate;
        }
        else /*not found*/
        {
            KPutStr("first header not found in this buffer\n");
            returnEmptyBufferToMHI(u->currnode);
            u->currnode=NULL; u->currpt=NULL; u->currlen=0;
        }
    }

    if(!u->prg_pcm)
    {
        KPutStr("MPEG_init...prg_pcm\n");
        if(!(u->prg_pcm=Delf_AddPrg(&DSP56K_MPEG_PCM)))
        {
            KPutStr("ERROR: unable to add prg_pcm\n");
            return;
        }
        Delf_Run(u->prg_pcm->prog+PROG_PCM_INIT, 0, 0,
                 (ULONG)(u->forcemono ? 1 : u->mono),
                 theHandle->delfVolumeLeft,
                 mpgfreq[u->freqidx],
                 theHandle->delfVolumeRight );
    }

    if(u->layer==2)
    {
        decoder = DEC_MPEG1_L2;
        if(!u->prg_mp2)
        {
            KPutStr("MPEG_init...prg_mp2\n");
            if(!u->mem_il_mp2) u->mem_il_mp2=Delf_AllocMem(INTL_MP2_DATL, DMEMF_LDATA|DMEMF_INTERNAL|DMEMF_ALIGN_64);
            if(!u->mem_ip_mp2) u->mem_ip_mp2=Delf_AllocMem(INTP_MP2_PROG, DMEMF_PROG|DMEMF_INTERNAL);
            if(!(u->prg_mp2=Delf_AddPrg(&DSP56K_MP2)))
            {
                KPutStr("ERROR: unable to add prg_mp2\n");
                return;
            }
            Delf_Run(u->prg_mp2->prog+PROG_MP2_INIT, 0, 0, u->mem_il_mp2, u->mem_ip_mp2, 0, 0);
            Delf_Poke(u->prg_mp2->ydata+DATY_MP2_FORCEMONO, DMEMF_YDATA, (ULONG)u->forcemono);
        }
    }
    else if(u->layer==3)
    {
        decoder = DEC_MPEG1_L3;
        if(!u->prg_mp3)
        {
            KPutStr("MPEG_init...prg_mp3\n");
            if(!u->mem_il_mp3) u->mem_il_mp3=Delf_AllocMem(INTL_MP3_DATL, DMEMF_LDATA|DMEMF_INTERNAL|DMEMF_ALIGN_64);
            if(!u->mem_ip_mp3) u->mem_ip_mp3=Delf_AllocMem(INTP_MP3_PROG, DMEMF_PROG|DMEMF_INTERNAL);
            if(!(u->prg_mp3=Delf_AddPrg(&DSP56K_MP3)))
            {
                KPutStr("ERROR: unable to add prg_mp3\n");
                return;
            }
            Delf_Run(u->prg_mp3->prog+PROG_MP3_INIT, 0, 0, u->mem_il_mp3, u->mem_ip_mp3, u->freqidx, 0);
            Delf_CopyMem(pow43tab, (void*)(u->prg_mp3->ydata+DATY_MP3_POW43TAB), 8207*3, DCPF_FROM_AMY|DCPF_YDATA|DCPF_24BIT);
            Delf_Poke(u->prg_mp3->ydata+DATY_MP3_FORCEMONO, DMEMF_YDATA, (ULONG)u->forcemono);
        }
    }
    else
    {
        KPutStr("ERROR: unsupported layer\n");
        return;
    }

    if(!u->intkey)
    {
        KPutStr("MPEG_init...AddIntServer\n");
        u->softint.is_Code=(void(*)(void))soft_IntServer;
        /*u->softint.is_Data=u;*/
        u->softint.is_Node.ln_Type=NT_INTERRUPT;
        u->softint.is_Node.ln_Pri=0;
        u->softint.is_Node.ln_Name="";
        u->softint.is_Node.ln_Succ=NULL;
        u->softint.is_Node.ln_Pred=NULL;
        u->delfint.is_Code=(void(*)(void))lev6_IntServer;
        /*u->delfint.is_Data=u;*/
        if(!(u->intkey=Delf_AddIntServer(u->prg_pcm->prog+PROG_PCM_INTKEY,&u->delfint)))
        {
            KPutStr("ERROR: AddIntServer failed\n");
            return;
        }
    }
    if(!u->mod_pcm)
    {
        KPutStr("MPEG_init...AddModule\n");
        if(!(u->mod_pcm=Delf_AddModule(DM_Inputs, 0, DM_Outputs, 1,
                            DM_Code, u->prg_pcm->prog+PROG_PCM_MODULE,
                            DM_Freq, u->dacrate ? u->dacrate*1000 : mpgfreq[u->freqidx],
                            DM_Name, libname,
                            0 )))
        {
            KPutStr("ERROR: AddModule failed\n");
            return;
        }
    }

    theHandle->activedecoder = decoder;
    KPutStr("MPEG_init...OK\n");
    return;
}





void
MPEG_close(void)
{
    struct delfinaMPEG *u;
    if (theHandle->activedecoder != DEC_MPEG1_L2 && theHandle->activedecoder != DEC_MPEG1_L3) return;
    u = &theHandle->mpeg;
    KPutStr("MPEG_close\n");
    if(u->mod_pcm)
    {
        /** wait until the currently running DSP routine has finished **/
        u->cleanup_flag=1;
        u->cleanup_count=2;
        while(u->cleanup_count>0) u->pause=1;
        Delf_RemModule(u->mod_pcm);
    }
    if(u->intkey)  {Delf_RemIntServer(u->intkey);}
    if(u->prg_mp2) {Delf_RemPrg(u->prg_mp2);}
    if(u->prg_mp3) {Delf_RemPrg(u->prg_mp3);}
    if(u->prg_pcm) {Delf_RemPrg(u->prg_pcm);}
    if(u->mem_il_mp2) {Delf_FreeMem(u->mem_il_mp2,DMEMF_LDATA|DMEMF_INTERNAL);}
    if(u->mem_ip_mp2) {Delf_FreeMem(u->mem_ip_mp2,DMEMF_PROG |DMEMF_INTERNAL);}
    if(u->mem_il_mp3) {Delf_FreeMem(u->mem_il_mp3,DMEMF_LDATA|DMEMF_INTERNAL);}
    if(u->mem_ip_mp3) {Delf_FreeMem(u->mem_ip_mp3,DMEMF_PROG |DMEMF_INTERNAL);}
    { /* clear the entire struct without using memset */
        UBYTE *pt = (UBYTE*) u;
        UWORD i = sizeof(struct delfinaMPEG);
        while (i--) *pt++ = 0;
    }
    theHandle->activedecoder = DEC_NONE;
}





static LONG __asm
lev6_IntServer(void)
{
    Cause(&theHandle->mpeg.softint);
    return 0;
}

static LONG __asm
soft_IntServer(void)
{
    struct delfinaMPEG *u = &theHandle->mpeg;
    if(u->pause || theHandle->mhistatus != MHIF_PLAYING || u->framebufstate!=FBS_FILLED)
    {
        if(u->cleanup_flag) u->cleanup_count--;
        Delf_Run(u->prg_pcm->prog+PROG_PCM_MUTE,0,DRUNF_ASYNCH,0,0,0,0);
    }
    else
    {
        if (theHandle->doChangeVolume)
        {
            Delf_Poke(u->prg_pcm->ydata+DATY_PCM_VOL_LEFT, DMEMF_YDATA, theHandle->delfVolumeLeft);
            Delf_Poke(u->prg_pcm->ydata+DATY_PCM_VOL_RIGHT, DMEMF_YDATA, theHandle->delfVolumeRight);
            theHandle->doChangeVolume = 0;
        }
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
            if(u->currnode)
            {
                if(u->currlen)
                {
                    ch=(ch<<8)|(ULONG)(*u->currpt);
                    u->currpt++; u->currlen--;
                }
                else
                {
                    returnEmptyBufferToMHI(u->currnode);
                    u->currnode=NULL; u->currpt=NULL; /*u->currlen=0;*/
                }
            }
            else
            {
                if (!(u->currnode = getFilledBufferFromMHI()))
                {
                    ch=0; break; /*ERROR*/
                }
                u->currpt = u->currnode->buffer;
                u->currlen= u->currnode->size;
            }
        }
        if(ch) /*found it!*/
        {
            ULONG bitrate = (ULONG)mpgbitrate[u->layer-1][(ch>>12)&15];
            u->frameBitrateSum += bitrate;
            u->frameCount++;
            u->framebufoffset=0;
            u->framebufleft=(bitrate*144000)/mpgfreq[(ch>>10)&3]+((ch>>9)&1)-4;
            u->II_translate=mp2translate[u->freqidx][u->mono][(ch>>12)&15];
            u->txtMode = mpgModename[(ch>>6)&3];
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
        while(u->framebufleft)
        {
            if(u->currnode)
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
                    returnEmptyBufferToMHI(u->currnode);
                    u->currnode=NULL; u->currpt=NULL; /*u->currlen=0;*/
                }
            }
            else
            {
                if (!(u->currnode = getFilledBufferFromMHI()))
                {
                    break; /*ERROR*/
                }
                u->currpt =u->currnode->buffer;
                u->currlen=u->currnode->size;
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
