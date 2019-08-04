/*****************************************************************************

    DelfSF - sndfile player for Delfina DSP
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


#include <stdio.h>
#include <stdlib.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <exec/interrupts.h>
#include <libraries/delfina.h>



/* we need these variables from main.c */
struct loadbuf {
    struct loadbuf *next;
    short *data;
};
extern struct loadbuf *buf_play;
extern long buffers_filled, buffers_played, pause;

/* number of samples per buffer */
/* note: must be equal to BUFFER_SAMPLES in main.c !!! */
#define DELFINA_SAMPLES     2048




extern struct DelfObj DSP56K_PCM;
/** offsets in PROGRAM section **/
#define PROG_PCM_INIT       0
#define PROG_PCM_INTKEY     0
#define PROG_PCM_MUTE       2
#define PROG_PCM_MODULE     4
/** offsets in DATA sections (X/Y/L memory) **/
#define DATY_PCM_BUFPTR     0
/** additional DSP-memory allocations (internal/external, P/X/Y/L memory) **/
/* none */


struct Library *DelfinaBase=NULL;
static struct Interrupt delfint={0}, softint={0};
static struct DelfPrg *prg_pcm=NULL;
static struct DelfModule *mod_pcm=NULL;
static ULONG key=0, delfmemtype, delfcopysize;
static LONG trigger_exit;




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
        trigger_exit--;
        Delf_Run(prg_pcm->prog+PROG_PCM_MUTE, 0, DRUNF_ASYNCH, 0,0,0,0);
    }
    else
    {
        if(buffers_filled>0)
        {
            Delf_CopyMem( buf_play->data,
                          (void*)Delf_Peek(prg_pcm->ydata+DATY_PCM_BUFPTR,DMEMF_YDATA),
                          delfcopysize,
                          DCPF_FROM_AMY|DCPF_16BITH|delfmemtype );
            buffers_filled--;
            buffers_played++;
            buf_play=buf_play->next;
        }
    }
    return 0;
}




int StartDelfina(unsigned int rate, unsigned int channels, LONG volume)
{
    LONG j, p;
    if(!(DelfinaBase=OpenLibrary("delfina.library",4)))
    {
        printf("**unable to open delfina.library v4\n");
        return(0);
    }
    if(!(prg_pcm=Delf_AddPrg(&DSP56K_PCM)))
    {
        printf("**not enough Delfina memory\n");
        return(0);
    }
    Delf_Run(prg_pcm->prog+PROG_PCM_INIT, 0, 0,
            (ULONG)((channels==1)?1:0),
            (volume>100) ? (volume*0x7fffff)/200 : (volume*0x400000)/100,
            (ULONG)rate, 0 );
    delfmemtype=(channels==1)?DMEMF_XDATA:DMEMF_LDATA;
    delfcopysize=(channels==1)?DELFINA_SAMPLES*2:DELFINA_SAMPLES*4;
    p=Delf_Peek(prg_pcm->ydata+DATY_PCM_BUFPTR,DMEMF_YDATA);
    j=2;
    while((buffers_filled>0) && (j-- >0))
    {
        Delf_CopyMem( buf_play->data,
                      (void*)p,
                      delfcopysize,
                      DCPF_FROM_AMY|DCPF_16BITH|delfmemtype );
        buffers_filled--;
        p+=DELFINA_SAMPLES;
        buf_play=buf_play->next;
    }

    softint.is_Code=(void(*)(void))soft_IntServer;
    softint.is_Node.ln_Type=NT_INTERRUPT;
    softint.is_Node.ln_Pri=0;
    softint.is_Node.ln_Name="DelfSF_SoftInt";
    softint.is_Node.ln_Succ=NULL;
    softint.is_Node.ln_Pred=NULL;
    delfint.is_Code=(void(*)(void))lev6_IntServer;
    if(!(key=Delf_AddIntServer(prg_pcm->prog+PROG_PCM_INTKEY,&delfint)))
    {
        printf("**couldn't create interrupt server\n");
        return(0);
    }
    if(!(mod_pcm=Delf_AddModule(DM_Inputs, 0,
                                DM_Outputs, 1,
                                DM_Code, prg_pcm->prog+PROG_PCM_MODULE,
                                DM_Freq, rate,
                                DM_Name, "DelfSF", 0)))
    {
        printf("**couldn't create DelfModule\n");
        return(0);
    }
    return(1);  /* ok */
}



void StopDelfina(void)
{
    if(mod_pcm)
    {
        pause++; trigger_exit=1;
        while(trigger_exit>0) Delay(1); /* wait until playback has finished */
        pause--;
        Delf_RemModule(mod_pcm); mod_pcm=NULL;
    }
    if(key) {Delf_RemIntServer(key); key=0;}
    if(prg_pcm) {Delf_RemPrg(prg_pcm); prg_pcm=NULL;}
    if(DelfinaBase) {CloseLibrary(DelfinaBase); DelfinaBase=NULL;}
}

