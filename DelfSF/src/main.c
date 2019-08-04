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
#include <math.h>
#include <libraries/reqtools.h>
#ifndef off_t
#define off_t size_t
#endif
#include <libraries/sndfile.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/reqtools.h>
#include <proto/sndfile.h>


/* functions in delf.c */
extern int StartDelfina(unsigned int rate, unsigned int channels, LONG volume);
extern void StopDelfina(void);

/* number of samples per buffer */
/* note: must be equal to DELFINA_SAMPLES in delf.c !!! */
#define BUFFER_SAMPLES      2048

/* number of buffers (segments in ringbuffer) */
#define NUM_BUFFERS         44




static char version[]="$VER: DelfSF 0.3 (Sat 17-May-2003)";
static char template[]=
 "FILES/M,-N=NOPLAY/S,-NT=NOTIMER/S,-VOL=VOLUME/K/N,"
 "-R=RATE/K/N,-B=BITS/K/N,-C=CHANNELS/K/N,-BS=BYTESWAP/S";

static char *format1[9]=
    {"<noname>","WAV","AIFF","AU","AU_LE","RAW","PAF","SVX","NIST"};
static char *format2[14]=
    {"<noname>","PCM","FLOAT","ULAW","ALAW","IMA_ADPCM","MS_ADPCM",
     "PCM_BE","PCM_LE","PCM_S8","PCM_U8","SVX_FIB","SVX_EXP","GSM610"};

struct loadbuf {
    struct loadbuf *next;
    short *data;
};

struct loadbuf *buf_read, *buf_play;
long buffers_filled, buffers_played, pause;
    



struct Library *SndFileBase=NULL;
struct ReqToolsBase *ReqToolsBase=NULL;

static struct loadbuf bufstruct[NUM_BUFFERS];
static short bufmem[NUM_BUFFERS*BUFFER_SAMPLES*2];

static struct RDArgs *rdargs;
static LONG args[8]={0,0,0,0,0,0,0,0};
static LONG noplay, notimer, volume=100, raw_bits=0, raw_chan=0, raw_rate=0;
static LONG raw_bs=0, ende=0, i, playraw=0, read_eof, taskpri, samples;
static char **files_pt, *filename, rtfilename[128], *f1, *f2;
static SNDFILE *sndfile;
static SF_INFO sfi;
static struct rtFileRequester *rtfilereq=NULL;
static struct rtFileList *rtfilelist=NULL, *rtfilelist_curr=NULL;
static BPTR lock_newdir=NULL, lock_olddir=NULL;
static struct Task *mytask;
static ULONG sigs, prev_print, curr_print;
static double duration;
static int rc=0;





/* this function is just here to make the code slightly shorter... */
/* fills the current read buffer, increments buffers_filled and */
/* advances the buffer pointer; sets the read_eof flag */
static void fillBuffer(void)
{
    LONG r;
    short *pt;
    if((r=SF_ReadShort(sndfile,buf_read->data,(size_t)samples))>0)
    {
        pt=buf_read->data+r;
        for(r=samples-r;r>0;r--) (*pt++)=0;
        pt=buf_read->data;
        if(sfi.pcmbitwidth==8) for(r=samples;r>0;r--) (*pt++)<<=8;
        buffers_filled++;
        buf_read=buf_read->next;
    }
    else read_eof++;
}




int main(void)
{
    rdargs=ReadArgs(template,args,NULL);
    files_pt=(char**)args[0];
    noplay=args[1];
    notimer=args[2];
    if(args[3]) volume=(*(LONG*)args[3]);
    if(volume<0) volume=0; else if(volume>200) volume=200;
    if(args[4]) {raw_rate=(*(LONG*)args[4]); playraw++;}
    if(args[5]) {raw_bits=(*(LONG*)args[5]); playraw++;}
    if(args[6]) {raw_chan=(*(LONG*)args[6]); playraw++;}
    raw_bs=args[7]; if(raw_bs) playraw++;

    printf("\33[1m%s by Smack/Infect!\33[0m\n",&version[6]);

    if(!(SndFileBase=OpenLibrary("sndfile.library",1)))
    {
        printf("**unable to open sndfile.library v1\n");
        rc=20;
        goto exit_clean;
    }

    mytask=FindTask(NULL);

    for(i=0;i<NUM_BUFFERS;i++)
    {
        bufstruct[i].next= &bufstruct[i+1];
        bufstruct[i].data= &bufmem[i*2*BUFFER_SAMPLES];
    }
    bufstruct[NUM_BUFFERS-1].next= &bufstruct[0];

    if(!files_pt)
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
next_filereq:
    if(rtfilereq)
    {
        rtfilelist=rtFileRequest( rtfilereq, &rtfilename[0],
                                  "DelfSF: select audio files",
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

    do
    {
        if(rtfilelist)
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
        sfi.samplerate=(raw_rate?raw_rate:44100);
        sfi.pcmbitwidth=(raw_bits?raw_bits:16);
        sfi.channels=(raw_chan?raw_chan:2);
        if(raw_bits==8) i=SF_FORMAT_PCM_S8;
        else if(raw_bs) i=SF_FORMAT_PCM_LE; else i=SF_FORMAT_PCM_BE;
        sfi.format=i|SF_FORMAT_RAW;
        if((sndfile=SF_OpenRead(filename,&sfi)))
        {
            i=(LONG)((sfi.format>>16)&0x7fff);
            f1=format1[(i>=(sizeof(format1)/sizeof(char*)))?0:i];
            i=(LONG)(sfi.format&0xffff);
            f2=format2[(i>=(sizeof(format2)/sizeof(char*)))?0:i];
            printf("  type: %s %s, %d Hz, %d bits, %d channel%s\n",f1,f2,
                    sfi.samplerate,sfi.pcmbitwidth,sfi.channels,
                    ((sfi.channels==1)?"":"s") );
            duration=(double)sfi.samples/(double)sfi.samplerate;
            printf("  time: %02d min %06.3f sec\n",
                    (int)(duration/60.0),fmod(duration,60.0) );
            if(!noplay)
            {
                if(!playraw && ((sfi.format&SF_FORMAT_TYPEMASK)==SF_FORMAT_RAW))
                {
                    printf("**raw file will not be played\n"
                           "  (must specify RATE, BITS, CHANNELS and/or BYTESWAP options)\n");
                }
                else if(sfi.channels>2)
                {
                    printf("**unsupported number of channels\n");
                }
                else
                {
                    pause=0;
                    read_eof=0;
                    buffers_filled=buffers_played=0;
                    prev_print=curr_print=0;
                    buf_read=buf_play= &bufstruct[0];
                    samples=BUFFER_SAMPLES*sfi.channels;
                    fillBuffer();
                    fillBuffer();  /* fill two buffers for StartDelfina() */
                    fillBuffer();
                    if(StartDelfina(sfi.samplerate,sfi.channels,volume))
                    {
                        printf("  playing..."); fflush(stdout);
                        taskpri=SetTaskPri(mytask,3);
                        while(1)
                        {
                            sigs=CheckSignal(SIGBREAKF_CTRL_C|SIGBREAKF_CTRL_D|SIGBREAKF_CTRL_E|SIGBREAKF_CTRL_F);
                            if(sigs&SIGBREAKF_CTRL_C)
                            {
                                printf(" break CTRL-C\n");
                                break;
                            }
                            if(sigs&SIGBREAKF_CTRL_D)
                            {
                                ende=1;
                                printf(" break CTRL-D\n");
                                break;
                            }
                            if(sigs&SIGBREAKF_CTRL_E)
                            {
                                if(pause)
                                {
                                    pause=0;
                                    printf("\n  playing...");
                                }
                                else
                                {
                                    pause=1;
                                    printf(" pause");
                                }
                                fflush(stdout);
                            }
                            if(sigs&SIGBREAKF_CTRL_F)
                            {
                                pause++; i=10;
                                while((buffers_filled>0) && (i-- >0))
                                {
                                    buf_play=buf_play->next;
                                    buffers_filled--;
                                    buffers_played++;
                                }
                                pause--;
                            }
                            if(!read_eof && (buffers_filled<NUM_BUFFERS)) fillBuffer();
                            else
                            {
                                if(read_eof && (buffers_filled==0)) break;
                                if(!notimer)
                                {
                                    curr_print=(buffers_played*BUFFER_SAMPLES)/sfi.samplerate;
                                    if(prev_print!=curr_print)
                                    {
                                        prev_print=curr_print;
                                        printf("\r  playing... (%02d:%02d)",
                                            curr_print/60, curr_print%60);
                                        fflush(stdout);
                                    }
                                }
                                Delay(1);
                            }
                        }
                        SetTaskPri(mytask,taskpri);
                    }
                    StopDelfina();
                    if(read_eof) printf(" done.\n");
                }
            }
            SF_Close(sndfile);
        }
        else
        {
            printf("**unable to open file\n");
            SF_ErrorStr(sndfile,(char*)bufmem,4096);
            printf("%s\n",bufmem);
        }
    } while(!ende);

    if(rtfilelist)
    {
        if(lock_olddir) { CurrentDir(lock_olddir); lock_olddir=NULL; }
        if(lock_newdir) { UnLock(lock_newdir); lock_newdir=NULL; }
        rtFreeFileList(rtfilelist); rtfilelist=NULL;
        if(!ende) goto next_filereq;
    }

exit_clean:
    if(rdargs) FreeArgs(rdargs);
    if(rtfilereq) rtFreeRequest(rtfilereq);
    if(ReqToolsBase) CloseLibrary((struct Library*)ReqToolsBase);
    if(SndFileBase) CloseLibrary(SndFileBase);
    return(rc);
}

