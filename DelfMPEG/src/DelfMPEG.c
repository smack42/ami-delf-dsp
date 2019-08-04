/*****************************************************************************

    DelfMPEG - MPEG audio player for Delfina DSP
    Copyright (C) 1999-2003  Michael Henke

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


#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/asyncio.h>
#include <proto/timer.h>
#include <proto/reqtools.h>
#include <exec/interrupts.h>
#include <exec/execbase.h>
#include <exec/memory.h>
#include <exec/libraries.h>
#include <libraries/asyncio.h>
#include <devices/timer.h>
#include <libraries/reqtools.h>
#include <stdlib.h>
#include <math.h>
#include <stdio.h>

static UBYTE version[]="$VER: DelfMPEG 1.0 (Sat 17-May-2003)";
static UBYTE template[]=
 "FILES/M,-V=VERBOSE/S,-N=NOPLAY/S,-T=SHOWTAG/S,-NL=NOFASTL/S,-NP=NOFASTP/S,"
 "-F=FRAMEBUF/K/N,-A=ASYNCBUF/K/N,-FF=FFSKIP/K/N,-NT=NOTIMER/S,-M=MONO/S,"
 "-S=STRICT/S,-D=DACRATE/K/N,-VOL=VOLUME/K/N,AREXX/S,-O=OUTFILE/K,-OI=OUTFILEINTEL/K";

#define DEFAULT_FRAMEBUF    100
#define DEFAULT_ASYNCBUF    128
#define DEFAULT_FFSKIP      10
#define DEFAULT_OUTFILE_ASYNCBUF 256
#define DEFAULT_OUTFILE_FRAMEBUF 40

struct loadbuf {
    ULONG header;
    UWORD framesize, delfcopysize, crc, III_main_data_size;
    UBYTE layer, mode, modext, freq, errprot, br_ind, II_jsbound, II_translate;
    struct loadbuf *next;
    UBYTE data[1728];   /* max: layer II, 384 kbps, 32 kHz -> 1728-4 bytes */
};

struct savebuf
{
    struct savebuf *next;
    UBYTE data[1152*2*2];
};

struct loadbuf *framebuf0=NULL, *curr_load, *curr_play;
LONG framebuf0size=0;
struct savebuf *savebuf0=NULL, *curr_write, *curr_decoded;

static ULONG mpg_freq[4]={44100,48000,32000,0};
static UBYTE *mpg_modename[4]={"stereo","j-stereo","dual-ch","single-ch"};
static UBYTE *mpg_layername[3]={"I","II","III"};
static UWORD mpg_bitrate[3][16]=
        { {0,32,64,96,128,160,192,224,256,288,320,352,384,416,448,0}, /* I */
          {0,32,48,56,64,80,96,112,128,160,192,224,256,320,384,0},   /* II */
          {0,32,40,48,56,64,80,96,112,128,160,192,224,256,320,0} }; /* III */
static UWORD mpg_translate[3][2][16] =
           { { { 0,2,2,2,2,2,2,0,0,0,1,1,1,1,1,0 } ,    /* 44100 stereo */
               { 0,2,2,0,0,0,1,1,1,1,1,1,1,1,1,0 } } ,  /* 44100 mono   */
             { { 0,2,2,2,2,2,2,0,0,0,0,0,0,0,0,0 } ,    /* 48000 stereo */
               { 0,2,2,0,0,0,0,0,0,0,0,0,0,0,0,0 } } ,  /* 48000 mono   */
             { { 0,3,3,3,3,3,3,0,0,0,1,1,1,1,1,0 } ,    /* 32000 stereo */
               { 0,3,3,0,0,0,1,1,1,1,1,1,1,1,1,0 } } }; /* 32000 mono   */
static UWORD mpg_sblimit[4]={27,30,8,12};

static ULONG ID3v1_TAG;
UBYTE ID3v1_buffer[142];

/** static LONG pow43tab[8206]; **/
#include "MP3_pow43tab.h"

static UWORD bitres_offset, bitres_ok;
static UBYTE bitres_buffer[4096];

#define MPG_MD_STEREO           0
#define MPG_MD_JOINT_STEREO     1
#define MPG_MD_DUAL_CHANNEL     2
#define MPG_MD_MONO             3
#define HDR_MPEG1               0xfff80000
#define HDR_CONSTANT            0x00060c00  /* layer, sampling frequency */
#define ID3V1                   0x54414700  /* TAG */
#define ID3V2                   0x49443300  /* ID3 */
#define XING_FLAG_FRAMES        1
#define XING_FLAG_BYTES         2
#define XING_FLAG_TOC           4

static UBYTE *delfina_name[8]=
    { "Classic","Lite","Pro","1200","Plus","Flipper","UNKNOWN_6","UNKNOWN_7" };

extern struct DelfObj DSP56K_PCM;
extern struct DelfObj DSP56K_MP2;
extern struct DelfObj DSP56K_MP3;

#include <libraries/delfina.h>
#include "PCM.h"
#include "MP2.h"
#include "MP3.h"

extern struct ExecBase *SysBase;
struct Library *DelfinaBase=NULL;
struct Library *AsyncIOBase=NULL;
struct Library *TimerBase=NULL;
struct ReqToolsBase *ReqToolsBase=NULL;

struct AsyncFile *file, *outfile=NULL;
struct FileInfoBlock *fib=NULL;
struct TagItem tag_done={TAG_DONE};
UBYTE rtfilename[128]={0}, *outfilename=NULL;
struct Task *mytask;
ULONG bytes_total, bytes_loaded, buffers_filled, buffers_missed, write_buffers_filled;
ULONG prevheader, currheader, frames_loaded, frames_played;
ULONG setpos_start, setpos_framesize;
ULONG decoder_busy, maindata_err;
ULONG trigger_irq; /* 0=running; 1=get+put; 2=put(no get); */
struct Interrupt delfint={0}, softint={0};
struct DelfPrg *prg_pcm=NULL, *prg_mp2=NULL, *prg_mp3=NULL;
struct DelfModule *mod_pcm=NULL;
DELFPTR mem_il_mp2=NULL, mem_ip_mp2=NULL;
DELFPTR mem_il_mp3=NULL, mem_ip_mp3=NULL;
ULONG key=0;

UWORD *gb_pt, gb_buf, gb_num;

UBYTE xing_toc[100];
ULONG xingvbr, xing_flags, xing_frames;
ULONG channels,mono,freq,layer,pause,noplay,showtag,rexxmode;
ULONG verbose, ende, havetag, nofastl, nofastp, notimer, forcemono, strict;
LONG framebuf, asyncbuf, ffskip, dacrate, volume;
int rc=0;

UBYTE playlist_in_use, playlist_curr_name[512], playlist_currdir;
struct MinList playlist;
struct playnode
{
    struct MinNode  minnode;
    UBYTE           filename[512];
};


/**
*** in arexx.c
**/
/* funtions */
char *initRexx(void);
void cleanupRexx(void);
void handleRexx(void);
/* variables */
extern char *rexxfilename[2];
extern char *portname;
extern WORD rexxstatus, rexxerror;
extern char rexxfiletypebuf[512];
extern ULONG rexxduration, rexxposition;



/*
**
** layer III - bit reservoir handling **
**
*/
UWORD BitReservoir(struct loadbuf *buf)
{
    UWORD main_data_begin, i;
    UBYTE *p0, *p1;
    p1= &buf->data[0];
    main_data_begin= (UWORD)(*p1)<<1 | (UWORD)(*(p1+1))>>7;
    /*
    ** copy side information **
    */
    p0= &bitres_buffer[0];
    for(i=32; i>0; i--) *p0++= *p1++;
    /*
    ** copy previous main_data **
    */
    p0++; /* &bitres_buffer[33] */
    if(bitres_offset>=main_data_begin)
    {
        bitres_ok=1;
        p1=p0+bitres_offset-main_data_begin;
        for(i=main_data_begin; i>0; i--) *p0++= *p1++;
        bitres_offset=main_data_begin;
    }
    else
    {
        bitres_ok=0;            /* not enough data in reservoir */
        p0+=bitres_offset;
    }
    /*
    ** copy current main_data **
    */
    i= mono ? 17 : 32;          /* side info length */
    p1= &buf->data[i];          /* begin of main_data area */
    i= buf->delfcopysize-i;     /* mainslots */
    bitres_offset+= i;
    for(; i>0; i--) *p0++= *p1++;
    return(bitres_ok);
}





/*
**
** Delfina interrupt server **
**
*/

/* Delfina hardware interrupt - M68K Level 6 IRQ. */
/* highest priority - interrupts all other system activity. */
/* problem: interferes with timing-critical things like serial I/O. */
/* solution: do actual processing in software interrupt (low priority). */

LONG __saveds lev6_IntServer(void)
{
    Cause(&softint);
    return 0;
}

LONG __saveds soft_IntServer(void)
{
    if(pause)
    {
        /*
        ** mute **
        */
        Delf_Run( prg_pcm->prog+PROG_PCM_MUTE, 0, DRUNF_ASYNCH, 0, 0, 0, 0 );
    }
    else
    {
        if(outfile)
        {
            if(trigger_irq!=2)
            {
                if(write_buffers_filled < DEFAULT_OUTFILE_FRAMEBUF)
                {
                    Delf_CopyMem( (APTR)(Delf_Peek(prg_pcm->ydata+DATY_PCM_BUFPTR,DMEMF_YDATA)),
                                  &curr_decoded->data[0],
                                  (ULONG)((forcemono?1:mono) ? 1152*2 : 1152*2*2),
                                  DCPF_TO_AMY|DCPF_16BITH|((forcemono?1:mono) ? DCPF_XDATA : DCPF_LDATA) );
                    curr_decoded=curr_decoded->next;
                    write_buffers_filled++;
                    frames_played++;
                    /* set trigger, in case something goes wrong before decoding (below) */
                    trigger_irq=2; /* 2=put(no get) */
                }
                else
                {
                    /* no write_buffer available -> try again later */
                    trigger_irq=1; /* 1=get+put */
                    return 0;
                }
            }
        }
        if(buffers_filled>0)
        {
            if(layer==2)
            {
                if( !Delf_Peek( prg_mp2->ydata+DATY_MP2_BUSY,DMEMF_YDATA ) )
                {
                    /* send framedata to Delfina */
                    Delf_CopyMem( &curr_play->data[0],
                                  (void*)(prg_mp2->xdata+DATX_MP2_INBUF),
                                  (ULONG)curr_play->delfcopysize,
                                  DCPF_FROM_AMY|DCPF_XDATA|DCPF_24BIT );
                    Delf_Run( (outfile) ? prg_pcm->prog+PROG_PCM_DECODE : prg_mp2->prog+PROG_MP2_DECODE,
                              0, DRUNF_ASYNCH,
                              mono,
                              Delf_Peek(prg_pcm->ydata+DATY_PCM_BUFPTR,DMEMF_YDATA),
                              (ULONG)curr_play->II_translate,
                              (ULONG)curr_play->II_jsbound );
                    if(!outfile) frames_played++;
                    buffers_filled--;
                    curr_play=curr_play->next;
                    trigger_irq=0; /* 0=running */
                }
                else decoder_busy++;
            }
            else if(layer==3)
            {
                if( !Delf_Peek( prg_mp3->ydata+DATY_MP3_BUSY,DMEMF_YDATA ) )
                {
                    /* send framedata to Delfina */
                    if(bitres_ok)
                    {
                        Delf_CopyMem( &bitres_buffer[0],
                                      (void*)(prg_mp3->xdata+DATX_MP3_INBUF),
                                      (ULONG)curr_play->III_main_data_size+33,
                                      DCPF_FROM_AMY|DCPF_XDATA|DCPF_24BIT );
                        Delf_Run( (outfile) ? prg_pcm->prog+PROG_PCM_DECODE : prg_mp3->prog+PROG_MP3_DECODE,
                                  0, DRUNF_ASYNCH,
                                  mono,
                                  Delf_Peek(prg_pcm->ydata+DATY_PCM_BUFPTR,DMEMF_YDATA),
                                  (ULONG)curr_play->modext,
                                  0 );
                    }
                    else maindata_err++;
                    if(!outfile) frames_played++;
                    buffers_filled--;
                    curr_play=curr_play->next;
                    if(buffers_filled>0) BitReservoir(curr_play);
                    trigger_irq=0; /* 0=running */
                }
                else decoder_busy++;
            }
        }
        else
        {
            if(frames_loaded>1) buffers_missed++;
        }
    }
    return 0;
}







void initPCM(void)
{
    if(!prg_pcm) return;
    Delf_Run(prg_pcm->prog+PROG_PCM_INIT, 0, 0,
            (ULONG)(forcemono?1:mono),
            (volume>100) ? (volume*0x7fffff)/200 : (volume*0x400000)/100,
            freq, 0 );
}


/*
**
** allocate Delfina resources **
**
*/
int InitDelfina(void)
{
    ULONG decoder_address;
    if(noplay) return(0);
    if(!(prg_pcm=Delf_AddPrg(&DSP56K_PCM)))
    {
        printf("**not enough Delfina memory\n");
        return(0);
    }
    switch(layer)
    {
        case 2:
            if(!nofastl)
                mem_il_mp2=Delf_AllocMem(INTL_MP2_DATL, DMEMF_LDATA|DMEMF_INTERNAL|DMEMF_ALIGN_64);
            if(!nofastp)
                mem_ip_mp2=Delf_AllocMem(INTP_MP2_PROG, DMEMF_PROG|DMEMF_INTERNAL);
            if(!(prg_mp2=Delf_AddPrg(&DSP56K_MP2)))
            {
                printf("**not enough Delfina memory\n");
                return(0);
            }
            Delf_Run(prg_mp2->prog+PROG_MP2_INIT, 0, 0,
                     mem_il_mp2, mem_ip_mp2, 0, 0);
            if((!mem_il_mp2 && !nofastl) || (!mem_ip_mp2 && !nofastp))
            {
                printf("**warning: not enough internal DSP memory\n"
                       "  (the decoder might run too slowly and produce \"jerky\" sound)\n");
            }
            Delf_Poke(prg_mp2->ydata+DATY_MP2_FORCEMONO, DMEMF_YDATA, forcemono);
            decoder_address = (ULONG)(prg_mp2->prog+PROG_MP2_DECODE);
            break;
        case 3:
            if(!nofastl)
                mem_il_mp3=Delf_AllocMem(INTL_MP3_DATL, DMEMF_LDATA|DMEMF_INTERNAL|DMEMF_ALIGN_64);
            if(!nofastp)
                mem_ip_mp3=Delf_AllocMem(INTP_MP3_PROG, DMEMF_PROG|DMEMF_INTERNAL);
            if(!(prg_mp3=Delf_AddPrg(&DSP56K_MP3)))
            {
                printf("**not enough Delfina memory\n");
                return(0);
            }
            Delf_CopyMem(pow43tab, (void*)(prg_mp3->ydata+DATY_MP3_POW43TAB),
                         8206*4, DCPF_FROM_AMY|DCPF_YDATA|DCPF_32BIT);
            Delf_Run(prg_mp3->prog+PROG_MP3_INIT, 0, 0,
                     mem_il_mp3, mem_ip_mp3,
                     (ULONG)curr_play->freq,
                     (ULONG)0);
            if((!mem_il_mp3 && !nofastl) || (!mem_ip_mp3 && !nofastp))
            {
                printf("**warning: not enough internal DSP memory\n"
                       "  (the decoder might run too slowly and produce \"jerky\" sound)\n");
            }
            Delf_Poke(prg_mp3->ydata+DATY_MP3_FORCEMONO, DMEMF_YDATA, forcemono);
            decoder_address = (ULONG)(prg_mp3->prog+PROG_MP3_DECODE);
            break;
        default:
            printf("**layer %d is not supported\n",layer);
            return(0);
    }
    /*initPCM();*/
    Delf_Run(prg_pcm->prog+PROG_PCM_INIT, 0, 0,
            (ULONG)(forcemono?1:mono),
            (volume>100) ? (volume*0x7fffff)/200 : (volume*0x400000)/100,
            freq,
            decoder_address );
    softint.is_Code=(void(*)(void))soft_IntServer;
    softint.is_Node.ln_Type=NT_INTERRUPT;
    softint.is_Node.ln_Pri=0;
    softint.is_Node.ln_Name="DelfMPEG_SoftInt";
    softint.is_Node.ln_Succ=NULL;
    softint.is_Node.ln_Pred=NULL;
    delfint.is_Code=(void(*)(void))lev6_IntServer;
    if (!(key=Delf_AddIntServer(prg_pcm->prog+PROG_PCM_INTKEY,&delfint)))
    {
        printf("**couldn't create interrupt server\n");
        return(0);
    }
    if(!outfile)
    {
        if(!(mod_pcm=Delf_AddModule(DM_Inputs, 0,
                                    DM_Outputs, 1,
                                    DM_Code, prg_pcm->prog+PROG_PCM_MODULE,
                                    DM_Freq, dacrate?dacrate*1000:freq,
                                    DM_Name, "DelfMPEG", 0)))
        {
            printf("**couldn't create DelfModule\n");
            return(0);
        }
    }
    return(1);
}







/*
**
** free Delfina resources **
**
*/
void CleanupDelfina(void)
{
    Delay(10);
    if(mod_pcm) {Delf_RemModule(mod_pcm); mod_pcm=NULL;}
    if(key)     {Delf_RemIntServer(key); key=0;}
    if(prg_mp2) {Delf_RemPrg(prg_mp2); prg_mp2=NULL;}
    if(prg_mp3) {Delf_RemPrg(prg_mp3); prg_mp3=NULL;}
    if(prg_pcm) {Delf_RemPrg(prg_pcm); prg_pcm=NULL;}
    if(mem_il_mp2) {Delf_FreeMem(mem_il_mp2,DMEMF_LDATA|DMEMF_INTERNAL); mem_il_mp2=NULL;}
    if(mem_ip_mp2) {Delf_FreeMem(mem_ip_mp2,DMEMF_PROG |DMEMF_INTERNAL); mem_ip_mp2=NULL;}
    if(mem_il_mp3) {Delf_FreeMem(mem_il_mp3,DMEMF_LDATA|DMEMF_INTERNAL); mem_il_mp3=NULL;}
    if(mem_ip_mp3) {Delf_FreeMem(mem_ip_mp3,DMEMF_PROG |DMEMF_INTERNAL); mem_ip_mp3=NULL;}
}






UWORD GetBits(UWORD num)
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






UWORD CheckHeader(struct loadbuf *lb)
{
    UWORD result=0;

    /** let's ignore these header infos (we don't need them here)
    *** ->version, ->extension, ->copyright, ->original, ->emphasis **/
    lb->header  = currheader;
    lb->layer   = 4-((currheader>>17)&0x3);
    lb->mode    = ((currheader>>6)&0x3);
    lb->modext  = ((currheader>>4)&0x3);
    lb->br_ind  = ((currheader>>12)&0xf);
    lb->freq    = ((currheader>>10)&0x3);
    lb->errprot = ((currheader>>16)&0x1)^0x1;

    if( (lb->layer==4)  ||
        (lb->br_ind==0) ||
        (lb->br_ind==15)||
        (lb->freq==3)   )   result+=1;
    if(prevheader)
        if( (currheader&HDR_CONSTANT)!=(prevheader&HDR_CONSTANT) ||
            ((lb->mode==MPG_MD_MONO?1:2)!=channels) ) result+=2;
    if((currheader&HDR_MPEG1)!=HDR_MPEG1) result+=4;
    return(result);
}






/*
**
** analyze frame header and load frame **
**
*/
int ReadFrame(struct loadbuf *lb)
{
    LONG s0,s1,i0,i1;
    UWORD md;

    if((md=CheckHeader(lb)))
    {
        if(!prevheader && strict) return(0);    /*error*/

        if((currheader&0xffffff00)==ID3V1)
        {
            if(verbose) printf(" (ID3v1 tag)\n");
        }
        else if(prevheader)
        {
            if(md&4)      printf(" (lost frame sync - non-audio data)\n");
            else if(md&1) printf(" (invalid frame header)\n");
            else if(md&2) printf(" (unsupported frame header change)\n");
            if(verbose)
                printf("**position=0x%08lx  currheader=0x%08lx  "
                   "prevheader=0x%08lx\n",bytes_loaded,currheader,prevheader);
        }

        if(!strict)
        {
            s0=0;
            while((s0<32*1024) && md)
            {
                bytes_loaded++; s0++;
                currheader<<=8;
                i0=ReadCharAsync(file);
                if(i0<0) break;
                else
                {
                    currheader|=i0;
                    md=CheckHeader(lb);
                }
            }
            if(verbose) printf("**skipped %d bytes - resync %s.\n",s0,md?"failed":"ok");
        }

        if(md) return(0);   /*error: resync failed */
    }


    if(channels==0) channels=(lb->mode==MPG_MD_MONO)?1:2;
    lb->II_translate= mpg_translate [lb->freq]
                                    [(lb->mode==MPG_MD_MONO)?1:0]
                                    [lb->br_ind];
    lb->II_jsbound= (lb->mode==MPG_MD_JOINT_STEREO) ?
                            (lb->modext<<2)+4 : mpg_sblimit[lb->II_translate];

    lb->framesize = (mpg_bitrate[lb->layer-1][lb->br_ind]*144000)
                    /mpg_freq[lb->freq] + ((currheader>>9)&0x1); /* padding */
    lb->delfcopysize = lb->framesize-4;

    s0=i0=2;
    if(lb->errprot)
    {
        i0=ReadAsync(file,&lb->crc,s0);
        lb->delfcopysize-=s0;
    }
    s1=i1=lb->delfcopysize;

    /** do not read layer I frame because it might be too large !! **/
    if(lb->layer>1) i1=ReadAsync(file,&lb->data[0],s1);


    /*** at beginning of file: try to recognize the second frame, too ***/
    if(!prevheader)
    {
        /** seek over first layer I frame (it has not been read) **/
        if(lb->layer==1) i1=SeekAsync(file,s1,MODE_CURRENT);
        if(PeekAsync(file,&currheader,4)==4)
        {
            ULONG ch=currheader;
            prevheader=currheader;
            if(CheckHeader(lb->next)) return(0); /*error*/
            prevheader=0;
            currheader=ch;
        }
    }


    if(!((i0==s0) && (i1==s1)))
    {
        if((i0==-1) || (i1==-1))
            printf(" (file read error)\n");
        else
            if(verbose) printf(" (unexpected EOF - got %d of %d bytes)\n",i1,s1);
        return(0);  /*error*/
    }

    if(lb->layer==3)
    {
        gb_pt= (UWORD*)&lb->data[2]; gb_num=0;
        if(lb->mode==MPG_MD_MONO)
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
        lb->III_main_data_size=(md+7)>>3;

        if(lb->mode!=MPG_MD_JOINT_STEREO) lb->modext=0;

        if(buffers_filled==0) BitReservoir(lb);
    }

    buffers_filled++;
    frames_loaded++;
    bytes_loaded+=lb->framesize;

    return(1);  /*ok*/
}






void setPosition(ULONG seconds)     /*** WARNING: quick & dirty hack !! ***/
{
    int loop,maxl;
    if(strict) return;

    pause++;
    if(seconds>rexxduration) seconds=rexxduration;
  /*printf("----setPosition %d\n",seconds);*/

    if(xing_flags&XING_FLAG_TOC)
    {
        /*interpolate in TOC to get file seek point in bytes*/
        int a;
        float fa, fb, fx;
        fx=100.0*(float)seconds/(float)rexxduration;
        a=(int)fx;  if(a>99) a=99;
        fa=xing_toc[a];  
        if(a<99) fb=xing_toc[a+1]; else fb=256.0;
        fx=(fa+(fb-fa)*(fx-a))/256.0;
        bytes_loaded=(int)(fx*bytes_total); 
        frames_loaded=frames_played=seconds*freq/1152;
      /*printf("----Xing VBR new pos: %ld of %ld bytes\n",bytes_loaded,bytes_total);*/
    }
    else
    {
        bytes_loaded=setpos_start+(seconds*(bytes_total-setpos_start))/rexxduration;
        frames_loaded=frames_played=bytes_loaded/setpos_framesize;
      /*printf("----plain new pos: %ld of %ld bytes\n",bytes_loaded,bytes_total);*/
    }

    curr_load=curr_play=framebuf0;
    buffers_filled=0;
    bitres_offset=0;

    SeekAsync(file,bytes_loaded,MODE_START);
    maxl=((framebuf>8) ? 8 : framebuf);
    for(loop=0; loop<maxl; loop++)
    {
        ReadAsync(file,&currheader,4);
        if(ReadFrame(curr_load))
        {
            prevheader=currheader;
            curr_load=curr_load->next;
        }
        else {bytes_loaded=bytes_total; break;}
    }
    pause--;
}





    


int main(void)
{
    struct RDArgs *rdargs;
    LONG args[17]={0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}, r1, i, taskpri, outintel=0;
    UBYTE **files_pt, *filename;
    double duration, duration2;
    ULONG sigs, minutes, seconds, millisec, curr_print_pos, prev_print_pos;
    struct timeval time1,time2;
    struct rtFileRequester *rtfilereq=NULL;
    struct rtFileList *rtfilelist=NULL, *rtfilelist_curr=NULL;
    BPTR lock_newdir=NULL, lock_olddir=NULL;

    /*
    **
    ** read args **
    **
    */
    rdargs=ReadArgs(template,args,NULL);
    printf("\33[1m%s by Smack/Infect!\33[0m\n",&version[6]);
    files_pt=(char**)args[0];
    verbose=args[1];
    noplay=args[2];
    showtag=args[3];
    nofastl=args[4];
    nofastp=args[5];
    if(args[6]) framebuf=(*(LONG*)args[6]); else framebuf=DEFAULT_FRAMEBUF;
    if(args[7]) asyncbuf=(*(LONG*)args[7]); else asyncbuf=DEFAULT_ASYNCBUF;
    if(args[8]) ffskip=(*(LONG*)args[8]); else ffskip=DEFAULT_FFSKIP;
    notimer=args[9];
    forcemono=args[10];
    strict=args[11];
    if(args[12]) dacrate=(*(LONG*)args[12]); else dacrate=0;
    if(args[13]) volume=(*(LONG*)args[13]); else volume=100;
    rexxmode=args[14];
    outfilename=(UBYTE*)args[15];
    if(args[16]!=NULL)
    {
        outfilename=(UBYTE*)args[16];
        outintel=1;
    }

    if(verbose)
        printf(
        "\n"
        "  DelfMPEG - MPEG audio player for Delfina DSP\n"
        "  Copyright (C) 1999-2003  Michael Henke\n"
        "\n"
        "  This program is free software; you can redistribute it and/or modify\n"
        "  it under the terms of the GNU General Public License as published by\n"
        "  the Free Software Foundation; either version 2 of the License, or\n"
        "  (at your option) any later version.\n"
        "\n"
        "  This program is distributed in the hope that it will be useful,\n"
        "  but WITHOUT ANY WARRANTY; without even the implied warranty of\n"
        "  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the\n"
        "  GNU General Public License for more details.\n"
        "\n"
        "  You should have received a copy of the GNU General Public License\n"
        "  along with this program; if not, write to the Free Software\n"
        "  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA\n"
        "\n"  );


    /*
    **
    ** open libraries & stuff **
    **
    */
    playlist_in_use=0;
    NewList((struct List*)&playlist);
    if(!noplay)
    {
        ULONG delfina_model;
        double delfina_dspclock;
        if (!(DelfinaBase=OpenLibrary("delfina.library",4)))
        {
            printf("**unable to open delfina.library V4\n");
            rc=20;
            goto exit_clean;
        }
        delfina_model=Delf_GetAttr(DA_HWInfo,0);
        delfina_model&=0xff; i=0;
        while(delfina_model) { delfina_model>>=1; i++; }
        delfina_model=i ? (i-1) : 7;
        delfina_dspclock=(double)Delf_GetAttr(DA_DSPClock,0)/1000000.0;
        if(verbose)
            printf("\n  Delfina %s: delfina.library v%d.%d, %d K memory, %.1f MHz DSP\n",
                   delfina_name[delfina_model],
                   DelfinaBase->lib_Version,
                   DelfinaBase->lib_Revision,
                   (Delf_AvailMem(DMEMF_PROG|DMEMF_TOTAL)+Delf_AvailMem(DMEMF_YDATA|DMEMF_TOTAL)+1023)>>10,
                   delfina_dspclock );
    }
    if (!(AsyncIOBase=OpenLibrary("asyncio.library",39))) {
        printf("**unable to open asyncio.library V39\n");
        rc=20;
        goto exit_clean;
    }
    TimerBase=(struct Library*)FindName(&SysBase->DeviceList,"timer.device");
    mytask=FindTask(NULL);
    if(!(fib=AllocDosObject(DOS_FIB,&tag_done)))
    {
        rc=20;
        goto exit_clean;
    }
    if(rexxmode)
    {
        char *res;
        if((res=initRexx()))
        {
            printf("**initRexx failed (%s)\n",res);
            rc=20;
            goto exit_clean;
        }
    }
        

    /*
    **
    ** init load buffers **
    **
    */
    if(framebuf<4) framebuf=4;
    if(asyncbuf<4) asyncbuf=4;
    if(ffskip<1) ffskip=1;
    if(dacrate<0) dacrate=0;
    if(dacrate>48) dacrate=48;
    if(dacrate) dacrate=Delf_GetAttr(DA_Freq,dacrate*1000)/1000;
    if(volume<0) volume=0;
    if(volume>200) volume=200;
    framebuf0size=framebuf*(LONG)sizeof(struct loadbuf);
    if(verbose)
        printf( "\n   reader: framebuf=%d (%d K), asyncbuf=%d K, ffskip=%d, strict=o%s\n"
                "  decoder: forcemono=o%s, dacrate=%d kHz, volume=%d%%\n",
                framebuf, (framebuf0size+1023)>>10, asyncbuf, ffskip, strict?"n":"ff",
                forcemono?"n":"ff", dacrate, volume );
    if(!(framebuf0=(struct loadbuf*)AllocMem(framebuf0size,MEMF_PUBLIC)))
    {
        printf("**not enough memory for \"framebuf\"\n");
        rc=20;
        goto exit_clean;
    }
    for(i=0;i<framebuf-1;i++) framebuf0[i].next= &framebuf0[i+1];
    framebuf0[framebuf-1].next=framebuf0;

    if((outfilename!=NULL) && (outfilename[0]!=0)) /* non-empty string */
    {
        if(!(outfile=OpenAsync(outfilename,MODE_WRITE,DEFAULT_OUTFILE_ASYNCBUF*1024)))
        {
            printf("**unable to open output file: %s\n",outfilename);
            rc=20;
            goto exit_clean;
        }
        if(!(savebuf0=(struct savebuf*)AllocMem(DEFAULT_OUTFILE_FRAMEBUF*(LONG)sizeof(struct savebuf),MEMF_PUBLIC)))
        {
            printf("**not enough memory for \"savebuf\"\n");
            rc=20;
            goto exit_clean;
        }
        for(i=0;i<DEFAULT_OUTFILE_FRAMEBUF-1;i++) savebuf0[i].next= &savebuf0[i+1];
        savebuf0[DEFAULT_OUTFILE_FRAMEBUF-1].next=savebuf0;
    }


    /*
    **
    ** reqtools filerequester if no files specified **
    **
    */
    if(!files_pt && !rexxmode)
    {
        if((ReqToolsBase=(struct ReqToolsBase*)OpenLibrary("reqtools.library",38)))
        {
            if(!(rtfilereq=rtAllocRequest(RT_FILEREQ,NULL)))
            {
                rc=10;
                goto exit_clean;
            }
        }
        else
        {
            rc=5;
            goto exit_clean;
        }
    }

    rexxduration=rexxposition=0;
    pause=1; rexxerror=0;   /* initialize */

next_filereq:
    if(rtfilereq)
    {
        rtfilelist=rtFileRequest( rtfilereq, &rtfilename[0],
                                  "DelfMPEG: select MPEG audio files",
                                  RTFI_Flags, FREQF_MULTISELECT|FREQF_PATGAD,
                                  TAG_DONE );
        if(rtfilelist)
        {
            rtfilelist_curr=rtfilelist;
            if(rtfilereq->Dir)
            {
                lock_newdir=Lock(rtfilereq->Dir,SHARED_LOCK);
                lock_olddir=CurrentDir(lock_newdir);
            }
        }
        else goto exit_clean;
    }

next_rexxloop:
    if(rexxmode)
    {
        ende=1; rexxstatus=0;   /* status: stop */
        rexxfilename[0]=NULL;
        printf("\n  ARexx mode: waiting for 'PLAY <file>' on port '%s'\n",portname);
        while(!rexxfilename[0])
        {
            handleRexx();
            if(CheckSignal(SIGBREAKF_CTRL_D)) goto exit_clean;
            if(ende>1) goto exit_clean;     /* QUIT command */
            Delay(1);
        }
        files_pt=rexxfilename;
        rexxduration=rexxposition=0;
        rexxfiletypebuf[0]=0;
        rexxerror=0;                /* PLAY command clears rexxerror */
        rexxstatus=1;               /* status: play */
        if(pause>1) rexxstatus=2;   /* status: pause */
    }
    ende=0;

    /*
    **
    ** files loop **
    **
    */
    do
    {
        /*
        **
        ** get next filename **
        **
        */
next_file:
        if(playlist_in_use)
        {
            struct playnode *node = (struct playnode*) RemHead((struct List*)&playlist);
            if(node)
            {
                CopyMem(node->filename,playlist_curr_name,sizeof(playlist_curr_name));
                filename=playlist_curr_name;
                FreeMem(node,sizeof(struct playnode));
            }
            else
            {
                if(playlist_currdir)
                {
                    if(lock_olddir) { CurrentDir(lock_olddir); lock_olddir=NULL; }
                    if(lock_newdir) { UnLock(lock_newdir); lock_newdir=NULL; }
                    playlist_currdir=0;
                }
                playlist_in_use=0;
                goto next_file;
            }
        }
        else if(rtfilelist)
        {
            if(rtfilelist_curr)
            {
                filename=rtfilelist_curr->Name;
                rtfilelist_curr=rtfilelist_curr->Next;
            }
            else filename=NULL;
        }
        else filename=(*files_pt++);

        if(!filename) break;
        printf("\n  file: %s\n",filename);

        havetag = 0;
        setpos_start = prev_print_pos = curr_print_pos = 0;
        curr_load = curr_play = framebuf0;
        curr_write = curr_decoded = savebuf0;
        trigger_irq = 2;
        bytes_loaded = buffers_filled = buffers_missed = write_buffers_filled = 0;
        prevheader = currheader = channels = 0;
        frames_loaded = frames_played = 0;
        decoder_busy = maindata_err = 0;
        bitres_offset = 0;

        /*
        **
        ** open file **
        **
        */
        rexxerror=20;  /* file not found */
        if((file=OpenAsync(filename,MODE_READ,asyncbuf<<10)))
        {
            /*
            **
            ** read first frame + output some info **
            **
            */
            ExamineFH(file->af_File,fib);
            bytes_total=fib->fib_Size;
            ReadAsync(file,&currheader,4);

            if(currheader==0x52494646)          /* RIFF WAVE header */
            {
                UBYTE b[8];
                ReadAsync(file,&b[0],8);
                r1=20;  /* 12+8 bytes */
                while(1)
                {
                    if(ReadAsync(file,&b[0],8)!=8)   /* chunk name + length */
                        break;  /* EOF -> ReadFrame() detects this */
                    r1=((LONG)b[7]<<24)|((LONG)b[6]<<16)|((LONG)b[5]<<8)|(LONG)b[4];
                    if((b[0]=='d')&&(b[1]=='a')&&(b[2]=='t')&&(b[3]=='a'))
                        break;                       /* found data chunk */
                    SeekAsync(file,r1,MODE_CURRENT); /* skip unknown chunk */
                }
                bytes_total=r1;
                if(verbose)
                    printf("  found RIFF header (data chunk: %ld bytes)\n",r1);
                ReadAsync(file,&currheader,4);
            }

            if((currheader&0xFFFFFF00)==ID3V2)  /* found ID3V2 tag */
            {
                UBYTE b[6];
                ReadAsync(file,&b[0],6);
                r1=(LONG)b[5]|((LONG)b[4]<<7)|((LONG)b[3]<<14)|((LONG)b[2]<<21);
                SeekAsync(file,r1,MODE_CURRENT);
                r1+=10;
                if(verbose)
                    printf("  skipped ID3v2.%d.%d tag (%d bytes)\n",
                            currheader&0xFF, b[0], r1);
                bytes_loaded+=r1;
                ReadAsync(file,&currheader,4);
            }

            rexxerror=30;  /* file not recognized as MPEG-1 audio */
            if(!ReadFrame(curr_load))   /* recognize the MPEG audio file */
            {
                printf("**not recognized as MPEG-1 audio\n");
                /*
                ** try to read the file as playlist **************************
                */
                if(!playlist_in_use)
                {
                    struct playnode *node=NULL;
                    int lines_read=0, lines_ok=0, len, i;
                    BPTR fh;
                    SeekAsync(file,0,MODE_START);
                    if(verbose) printf("  interpreting file as playlist.  valid entries:\n");
                    if(!lock_newdir)
                    {
                        i=PathPart(filename)-filename;
                        if(i>0)
                        {
                            UBYTE temp=filename[i];
                            filename[i]=0;
                            lock_newdir=Lock(filename,SHARED_LOCK);
                            lock_olddir=CurrentDir(lock_newdir);
                            if(verbose) printf("  playlist current dir: %s\n",filename);
                            filename[i]=temp;
                            playlist_currdir=1;
                        }
                    }
                    for(;;)
                    {
                        if(!node) node=AllocMem(sizeof(struct playnode),MEMF_PUBLIC|MEMF_CLEAR);
                        if(!node) break;    /* mem error */
                        len=ReadLineAsync(file,node->filename,sizeof(node->filename));
                        if(len<=0) break;   /* EOF or read error */
                        if(node->filename[0]!='#')
                        {
                            lines_read++;
                            for(i=0;i<len;i++) if(node->filename[i]<' ') node->filename[i]=0;
                            fh=Open(node->filename,MODE_OLDFILE);
                            if(fh)
                            {
                                Close(fh); lines_ok++;
                                if(verbose) printf("    %s\n",node->filename);
                                AddTail((struct List*)&playlist,(struct Node*)node);
                                node=NULL;
                                playlist_in_use=1;
                            }
                        }
                        if((lines_read>=32) && (lines_ok==0)) break; /* error: no valid lines */
                    }
                    if((lines_ok>0) || verbose) printf("  playlist:  %d valid entries  (%d entries were read)\n",lines_ok,lines_read);
                    if(lines_ok==0)
                    {
                        printf("**not usable as playlist\n");
                        if(playlist_currdir)
                        {
                            if(lock_olddir) { CurrentDir(lock_olddir); lock_olddir=NULL; }
                            if(lock_newdir) { UnLock(lock_newdir); lock_newdir=NULL; }
                            playlist_currdir=0;
                        }
                    }
                    if(node) FreeMem(node,sizeof(struct playnode));
                }
                CloseAsync(file);
                goto next_file;
            }
            else    /* ReadFrame ok */
            {
                if(showtag)
                {
                    LONG oldpos;
                    oldpos=SeekAsync(file, -128, MODE_END);
                    ID3v1_TAG=0; havetag=0;
                    ReadAsync(file, &ID3v1_TAG, 3);
                    if((ID3v1_TAG&0xffffff00)==ID3V1)
                    {
                        ReadAsync(file,&ID3v1_buffer[00],30); /* title  */
                        ReadAsync(file,&ID3v1_buffer[31],30); /* artist */
                        ReadAsync(file,&ID3v1_buffer[62],30); /* album  */
                        ReadAsync(file,&ID3v1_buffer[93],04); /* year   */
                        ReadAsync(file,&ID3v1_buffer[98],30); /* comment*/
                        ReadAsync(file,&ID3v1_buffer[141],1); /* genre  */
                        for(i=0;i<140;i++)
                        {
                            if(ID3v1_buffer[i]<0x20) ID3v1_buffer[i]=0x20;
                        }
                        ID3v1_buffer[30]=0x00;
                        ID3v1_buffer[30+1+30]=0x00;
                        ID3v1_buffer[30+1+30+1+30]=0x00;
                        ID3v1_buffer[30+1+30+1+30+1+4]=0x00;
                        ID3v1_buffer[30+1+30+1+30+1+4+1+30]=0x00;
                        havetag=1;
                    }
                    SeekAsync(file, oldpos, MODE_START);
                }

                {   /* look for Xing VBR header */
                    UBYTE *xpt;
                    xingvbr=xing_flags=0;
                    xpt= &curr_load->data[0];
                    xpt+=(curr_load->mode==MPG_MD_MONO) ? 17 : 32;
                    if((xpt[0]=='X') &&
                       (xpt[1]=='i') &&
                       (xpt[2]=='n') &&
                       (xpt[3]=='g') )
                    {
                        xingvbr++;
                        xpt+=4;
                        xing_flags= (ULONG)xpt[0]<<24 | (ULONG)xpt[1]<<16 |
                                    (ULONG)xpt[2]<<8  | (ULONG)xpt[3];
                        xpt+=4;
                        if(xing_flags&XING_FLAG_FRAMES)
                        {
                            xing_frames=(ULONG)xpt[0]<<24 | (ULONG)xpt[1]<<16 |
                                        (ULONG)xpt[2]<<8  | (ULONG)xpt[3];
                            xpt+=4;
                        }
                        else xing_frames=0;
                        if(xing_flags&XING_FLAG_BYTES) xpt+=4;
                        if(xing_flags&XING_FLAG_TOC)
                        {
                            for(i=0;i<100;i++) xing_toc[i]=xpt[i];
                        }
                        if(verbose) printf("  Xing VBR header found (info: %d frames, TOC=%s)\n",xing_frames,((xing_flags&XING_FLAG_TOC)?"yes":"no"));

                    }
                }

                freq=mpg_freq[curr_load->freq];
                mono=(curr_load->mode==MPG_MD_MONO) ? 1 : 0;
                layer=curr_load->layer;
                setpos_framesize=curr_load->framesize;

                duration=(double)bytes_total/((double)freq/1152.0)/(double)curr_load->framesize;
                r1=mpg_bitrate[layer-1][curr_load->br_ind];
                if(xingvbr && xing_frames)
                {
                    duration=(double)xing_frames*1152.0/(double)freq;
                    r1=(LONG)((double)bytes_total/duration/125.0);
                }
                sprintf(rexxfiletypebuf,
                        "layer %s  %s%03d kbps  %ld Hz  %s",
                        mpg_layername[layer-1],
                        (xingvbr && xing_frames) ? "VBR avg. " : "",
                        r1,
                        freq,
                        mpg_modename[curr_load->mode] );
                printf("  type: %s\n",rexxfiletypebuf);
                rexxduration=(ULONG)(duration+0.5);
                millisec=(ULONG)(1000.0*modf(duration,&duration2));
                minutes=(ULONG)(duration2/60.0);
                seconds=(ULONG)(duration2-minutes*60.0);
                printf("  time: %02ld min %02ld.%03ld sec\n",minutes,seconds,millisec);
                if(verbose) printf("  filesize / framesize: %ld / %ld bytes\n",bytes_total,curr_load->framesize);
                if(outfile) printf("  output file: %s\n",outfilename);

                if(showtag && havetag)
                {
                    printf( "  TAG ID3v1\n      title: %s\n     artist: %s\n"
                            "      album: %s\n    comment: %s\n"
                            "       year: %s    genre: %d\n",
                            &ID3v1_buffer[00], /* title  */
                            &ID3v1_buffer[31], /* artist */
                            &ID3v1_buffer[62], /* album  */
                            &ID3v1_buffer[98], /* comment*/
                            &ID3v1_buffer[93], /* year   */
                            (int)ID3v1_buffer[141] ); /* genre  */
                }

                prevheader=currheader;
                curr_load=curr_load->next;

                rexxerror=10;  /* initDelfina failed */
                if(InitDelfina())
                {
                    rexxerror=0;    /* ok */
                    if(outfile) printf("  decoding...");
                    else        printf("  playing...");
                    fflush(stdout);
                    if(!outfile) taskpri=SetTaskPri(mytask,3);
                    GetSysTime(&time1);
                    if(pause>0) pause--;
                    /*
                    **
                    ** frames loop **
                    **
                    */
                    while(!ende)
                    {
                        if(rexxmode) handleRexx();
                        sigs=CheckSignal(SIGBREAKF_CTRL_C|SIGBREAKF_CTRL_D|SIGBREAKF_CTRL_E|SIGBREAKF_CTRL_F);
                        if(sigs&SIGBREAKF_CTRL_C)
                        {
                            printf(" break CTRL-C\n");
                            break;  /* exit frames loop */
                        }
                        if(sigs&SIGBREAKF_CTRL_D)
                        {
                            ende=2;
                            printf(" break CTRL-D\n");
                            break;  /* exit frames loop */
                        }
                        if(outfile)
                        {
                            if(trigger_irq) Delf_Run( prg_pcm->prog+PROG_PCM_INTERRUPT, 0, DRUNF_ASYNCH, 0, 0, 0, 0 );
                        }
                        else
                        {
                            if(sigs&SIGBREAKF_CTRL_E)
                            {
                                if(pause)
                                {
                                    pause=0;
                                    rexxstatus=1;   /* play */
                                    printf("\n  playing...");
                                }
                                else
                                {
                                    pause=1;
                                    rexxstatus=2;   /* pause */
                                    printf(" pause");
                                }
                                fflush(stdout);
                            }
                            if(sigs&SIGBREAKF_CTRL_F)
                            {
                                pause++;
                                i=ffskip;
                                while((buffers_filled>0) && (i>0))
                                {
                                    curr_play=curr_play->next;
                                    buffers_filled--; i--;
                                    frames_played++;
                                    if((layer==3) && (buffers_filled>0))
                                        BitReservoir(curr_play);
                                }
                                pause--;
                            }
                        }
                        if( (buffers_filled<framebuf) &&
                            (bytes_loaded<bytes_total) )
                        {
                            if(ReadAsync(file,&currheader,4)==4)
                                if(ReadFrame(curr_load))
                                {
                                    prevheader=currheader;
                                    curr_load=curr_load->next;
                                }
                                else bytes_loaded=bytes_total; /* force EOF */
                            else bytes_loaded=bytes_total;
                        }
                        else if(write_buffers_filled>0)
                        {
                            if(forcemono?1:mono) i=1152*2;
                            else i=1152*2*2;
                            if(outintel)
                            {
                                LONG l;
                                UBYTE *p, *q, b;
                                p=q=curr_write->data;
                                for(l=i/2;l>0;l--)
                                {
                                    b = *p++;
                                    *q++ = *p++;
                                    *q++ = b;
                                }
                            }
                            if(WriteAsync(outfile,curr_write->data,i)!=i)
                            {
                                printf("**error writing to output file.\n");
                                rc=20;
                                break; /* exit frames loop */
                            }
                            curr_write=curr_write->next;
                            write_buffers_filled--;
                        }
                        else
                        {
                            if( (bytes_loaded>=bytes_total) &&
                                (buffers_filled==0) &&
                                (frames_loaded==frames_played) )
                            {
                                printf(" done.\n");
                                break;  /* exit frames loop */
                            }
                            if(!notimer)    /* print timer */
                            {
                                curr_print_pos=(frames_played*1152)/freq;
                                if(prev_print_pos!=curr_print_pos)
                                {
                                    rexxposition=curr_print_pos;
                                    prev_print_pos=curr_print_pos;
                                    if(outfile) printf("\r  decoding... (%02d:%02d)", curr_print_pos/60, curr_print_pos%60);
                                    else        printf("\r  playing... (%02d:%02d)", curr_print_pos/60, curr_print_pos%60);
                                    fflush(stdout);
                                }
                            }
                            Delay(1);
                        }
                    }
                    pause++; /* mute output */
                    GetSysTime(&time2);
                    if(!outfile) SetTaskPri(mytask,taskpri);
                    SubTime(&time2,&time1);
                    millisec=(ULONG)((double)time2.tv_micro/1000.0);
                    minutes=(ULONG)((double)time2.tv_secs/60.0);
                    seconds=(ULONG)((double)time2.tv_secs-minutes*60.0);
                    duration2=(double)time2.tv_secs+(double)time2.tv_micro/1000000.0;
                    if(verbose)
                    {
                        if(buffers_missed>5) /* prevent "unnecessary" warnings */
                            printf("**note: detected NO_INPUT_DATA %d times\n",buffers_missed);
                        if(decoder_busy>0)
                            printf("**note: detected DECODER_BUSY %d times (%d%%)\n",decoder_busy,decoder_busy*100/frames_played);
                        if(maindata_err>0)
                            printf("**note: detected MP3_MAINDATA_ERROR %d times\n",maindata_err);
                        printf("  frames played / loaded: %ld / %ld\n",frames_played,frames_loaded);
                        printf("  elapsed time: %02ld min %02ld.%03ld sec\n",minutes, seconds, millisec);
                    }
                    if(outfile)
                    {
                        if(!verbose) printf("  elapsed time: %02ld min %02ld.%03ld sec\n",minutes, seconds, millisec);
                        ende=2;
                    }
                }
                CleanupDelfina();
            }
            CloseAsync(file);
        }
        else printf("**unable to open file\n");
    } while(!ende);


    if(playlist_in_use)
    {
        APTR p;
        while((p=RemHead((struct List*)&playlist))) FreeMem(p,sizeof(struct playnode));
        if(playlist_currdir)
        {
            if(lock_olddir) { CurrentDir(lock_olddir); lock_olddir=NULL; }
            if(lock_newdir) { UnLock(lock_newdir); lock_newdir=NULL; }
            playlist_currdir=0;
        }
        playlist_in_use=0;
    }
    if(rtfilelist)
    {
        if(lock_olddir) { CurrentDir(lock_olddir); lock_olddir=NULL; }
        if(lock_newdir) { UnLock(lock_newdir); lock_newdir=NULL; }
        rtFreeFileList(rtfilelist); rtfilelist=NULL;
        if(ende<2) goto next_filereq;
    }
    if(rexxmode && (ende<2)) goto next_rexxloop;

exit_clean:
    cleanupRexx();
    if(outfile) CloseAsync(outfile);
    if(rdargs) FreeArgs(rdargs);
    if(framebuf0) FreeMem((APTR)framebuf0,framebuf0size);
    if(savebuf0) FreeMem((APTR)savebuf0,DEFAULT_OUTFILE_FRAMEBUF*(LONG)sizeof(struct savebuf));
    if(fib) FreeDosObject(DOS_FIB,fib);
    if(rtfilereq) rtFreeRequest(rtfilereq);
    if(ReqToolsBase) CloseLibrary((struct Library*)ReqToolsBase);
    if(AsyncIOBase) CloseLibrary(AsyncIOBase);
    if(DelfinaBase) CloseLibrary(DelfinaBase);
    return(rc);
}
