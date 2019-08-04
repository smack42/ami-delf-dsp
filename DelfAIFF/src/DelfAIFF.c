/*******************************************************************

    DelfAIFF

    plays AIFF files (16bit SSND and ADP4) using Delfina DSP


    code by Smack/Infect!
    based on the example program DelfPlay


    v0.3  Fri 01-Oct-1999

*******************************************************************/



#define DELFINA_SAMPLES 2046    /* multiple of 6 (for ADPCM decoder) */
#define LOADBUFFERS     32      /* number of segments in ring buffer */



#define __stdargs /* vbcc <-- get rid of warnings on asyncio's declarations */

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/asyncio.h>  /* SAS/C */
/*#include <clib/asyncio_protos.h>*/  /* vbcc */
#include <proto/timer.h>
#include <libraries/reqtools.h>
#include <proto/reqtools.h> /* SAS/C */
/*#include <clib/reqtools_protos.h>*/ /* vbcc */
#include <exec/interrupts.h>
#include <exec/execbase.h>
#include <libraries/delfina.h>
#include <libraries/asyncio.h>
#include <devices/timer.h>
#include <math.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

#define ID_FORM 0x464F524D
#define ID_AIFF 0x41494646
#define ID_COMM 0x434F4D4D
#define ID_SSND 0x53534E44
#define ID_ADP4 0x41445034

double ConvertFromIeeeExtended(unsigned char* bytes);

static UBYTE version[]="$VER: DelfAIFF 0.3 (Fri 01-Oct-1999)";
static UBYTE template[]="FILES/M,NOPLAY/S";

struct loadbuf {
    UBYTE *ptr;
    struct loadbuf *next;
};

struct loadbuf load_buf[LOADBUFFERS];
struct loadbuf *curr_load, *curr_play;
far UBYTE load_memory[DELFINA_SAMPLES*4*LOADBUFFERS];

UBYTE get_delfina_48khz[32];    /* input buffer for GetVar() */
ULONG delfina_48khz=0;

extern struct DelfObj DSP56K_PCM;
extern struct DelfObj DSP56K_ADPCM4;

extern struct ExecBase *SysBase;
struct Library *DelfinaBase=NULL;
struct Library *AsyncIOBase=NULL;
struct Library *TimerBase=NULL;
struct ReqToolsBase *ReqToolsBase=NULL;

UBYTE rtfilename[128]={0};
struct Task *mytask;
ULONG loadsize, aiffbytes, bytes_loaded, bytes_played;
ULONG buffers_filled, buffers_missed, slow_adpcm4;
struct Interrupt delfint={0};
struct DelfPrg *prg_pcm=NULL, *prg_adpcm4=NULL;
struct DelfModule *mod_pcm=NULL;
int key=0;
int adp4,mono,freq,noplay,pause;
ULONG delfmemtype;
int rc=0;



/*
**
** interrupt server (supplies Delfina with data) **
**
*/
void int_server(void)
{
    if(pause)
    {
        /*
        ** mute **
        */
        Delf_Run( prg_pcm->prog+2, 99, DRUNF_ASYNCH, 0, 0, 0, 0);
    }
    else
    {
        if(buffers_filled>0)
        {
            int data2delfina=1;
            if(adp4)
            {
                /*
                ** check if ADPCM decoder is still busy **
                */
                if(!Delf_Peek(prg_adpcm4->ydata,DMEMF_YDATA))
                {
                    /*
                    ** copy ADPCM data to Delfina and start decoder **
                    */
                    Delf_CopyMem( curr_play->ptr,
                                  (void*)(prg_adpcm4->ydata+1),
                                  loadsize,
                                  DCPF_FROM_AMY|DCPF_YDATA|DCPF_24BIT );
                    Delf_Run( prg_adpcm4->prog+2, 99, DRUNF_ASYNCH,
                              mono,
                              Delf_Peek(prg_pcm->ydata,DMEMF_YDATA),
                              0,0);
                }
                else
                {
                    data2delfina=0;
                    slow_adpcm4++;
                }
            }
            else
            {
                /*
                ** copy PCM data to Delfina **
                */
                Delf_CopyMem( curr_play->ptr,
                              (void*)Delf_Peek(prg_pcm->ydata,DMEMF_YDATA),
                              loadsize,
                              DCPF_FROM_AMY|delfmemtype|DCPF_16BITH );
            }
            if(data2delfina)
            {
                bytes_played+=loadsize;
                buffers_filled--;
                curr_play=curr_play->next;
            }
        }
        else buffers_missed++;
    }
}



/*
**
** allocate Delfina resources **
**
*/
int init_delfina(void)
{
    if(noplay) return(0);

    delfmemtype= mono ? DMEMF_XDATA : DMEMF_LDATA;

    if (!(prg_pcm=Delf_AddPrg(&DSP56K_PCM))) {
        printf("**not enough Delfina memory\n");
        return(0);
    }

    delfint.is_Code=(void(*)(void))int_server;
    if (!(key=Delf_AddIntServer(prg_pcm->prog,&delfint))) {
        printf("**couldn't create interrupt server\n");
        return(0);
    }

    Delf_Run(prg_pcm->prog,0,0, mono,0,freq,0);

    if (!(mod_pcm=Delf_AddModule(   DM_Inputs, 0,
                                DM_Outputs, 1,
                                DM_Code, prg_pcm->prog+4,
                                DM_Freq, 48000,     /* always 48kHz output */
                                DM_Name, "DelfAIFF", 0)))
    {
        printf("**couldn't create DelfModule\n");
        return(0);
    }

    if(adp4)
    {
        prg_adpcm4=Delf_AddPrg(&DSP56K_ADPCM4);
        if(!prg_adpcm4) {
            printf("**not enough Delfina memory\n");
            return(0);
        }
        Delf_Run( prg_adpcm4->prog, 0, 0, 0, 0, 0, 0);
    }

    return(1);
}



/*
**
** free Delfina resources **
**
*/
void cleanup_delfina(void)
{
    Disable();
    if (key)        { Delf_RemIntServer(key); key=0; }
    if (mod_pcm)    { Delf_RemModule(mod_pcm); mod_pcm=NULL; }
    if (prg_adpcm4) { Delf_RemPrg(prg_adpcm4); prg_adpcm4=NULL; }
    if (prg_pcm)    { Delf_RemPrg(prg_pcm); prg_pcm=NULL; }
    Enable();
}



    


int main(void)
{
    struct RDArgs *rdargs;
    LONG args[3]={0,0,0};
    ULONG header_l[3], i, r1, taskpri;
    ULONG sigs, aiffbits=0, aifftype=0;
    UBYTE **files_pt, *filename, ende=0;
    struct AsyncFile *file;
    double duration, duration2;
    ULONG minutes, seconds, millisec;
    struct timeval time1,time2;
    struct rtFileRequester *rtfilereq=NULL;
    struct rtFileList *rtfilelist=NULL, *rtfilelist_curr=NULL;
    BPTR lock_newdir=(BPTR)NULL, lock_olddir=(BPTR)NULL;
    
    /*
    **
    ** read args **
    **
    */
    printf("\33[1m%s by Smack/Infect!\33[0m\n",&version[6]);
    rdargs=ReadArgs(template,args,NULL);
    files_pt=(UBYTE**)args[0];
    noplay=args[1];

    /*
    **
    ** open libraries **
    **
    */
    if (!(DelfinaBase=OpenLibrary("delfina.library",4))) {
        printf("**unable to open delfina.library V4\n");
        rc=20;
        goto exit_clean;
    }
    if (!(AsyncIOBase=OpenLibrary("asyncio.library",39))) {
        printf("**unable to open asyncio.library V39\n");
        rc=20;
        goto exit_clean;
    }
    TimerBase=(struct Library*)FindName(&SysBase->DeviceList,"timer.device");

    if(0<(GetVar("DELFINA_48KHZ",get_delfina_48khz,32,LV_VAR)))
    {
        delfina_48khz=strtoul(get_delfina_48khz,NULL,10);
        printf("  ////read ENV: DELFINA_48KHZ = '%s' (%ldHz)\n",get_delfina_48khz,(long)delfina_48khz);
        if(delfina_48khz==ULONG_MAX) delfina_48khz=0;
    }
    mytask=FindTask(NULL);

    /*
    **
    ** init load buffers **
    **
    */
    for(i=0;i<LOADBUFFERS;i++)
    {
        load_buf[i].ptr =&load_memory[i*DELFINA_SAMPLES*4];
        load_buf[i].next=&load_buf[i+1];
    }
    load_buf[LOADBUFFERS-1].next=&load_buf[0];

    /*
    **
    ** reqtools filerequester if no files specified **
    **
    */
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
                                  "DelfAIFF: select AIFF sound files",
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

        /*
        **
        ** open file **
        **
        */
        if((file=OpenAsync(filename,MODE_READ,128*1024)))
        {
            /*
            **
            ** is it a "FORM AIFF" file? **
            **
            */
            r1=ReadAsync(file,&header_l[0],3*4);
            if( (header_l[0]==ID_FORM) &&
                (header_l[2]==ID_AIFF) &&
                (r1==3*4) )
            {
                /*
                **
                ** read AIFF features **
                **
                */
                WORD aiff_error=0, header_ok=0, sound_ok=0;
                UWORD buf2;
                UBYTE buf10[10];
                do
                {
                    /*
                    ** IFF chunk header **
                    */
                    r1=ReadAsync(file,&header_l[0],2*4);
                    if( r1==2*4 )
                    {
                        switch(header_l[0])
                        {
                            case ID_COMM:
                                /*
                                ** get features from COMM chunk **
                                */
                                if(header_l[1]==18)
                                {
                                    ReadAsync(file,&buf2,2);     /* channels */
                                    mono= buf2>1 ? 0 : 1;
                                    ReadAsync(file,&buf10[0],4); /* frames */
                                    ReadAsync(file,&buf2,2);     /* bits */
                                    aiffbits=(ULONG)buf2;
                                    ReadAsync(file,&buf10[0],10);/* rate */
                                    freq=(ULONG)ConvertFromIeeeExtended(buf10);
                                    header_ok=1;
                                }
                                else aiff_error=1; /* invalid COMM chunk */
                                break;
                            case ID_SSND:
                            case ID_ADP4:
                                /*
                                ** found sound data **
                                */
                                aifftype =header_l[0];
                                aiffbytes=header_l[1];
                                sound_ok=1;
                                if(!header_ok) aiff_error=1; /* no COMM chunk */
                                break;
                            default:
                                /*
                                ** skip unknown chunk **
                                */
                                SeekAsync(file,(long)header_l[1],MODE_CURRENT);
                        }
                    }
                    else aiff_error=1;
                } while(!sound_ok && !aiff_error);

                if(!aiff_error && sound_ok && header_ok && aiffbits==16)
                {
                    printf("  type: AIFF %s, %ldHz, %s\n",
                        aifftype==ID_SSND?"SSND":"ADP4", (long)freq, mono?"mono":"stereo");
                    /*
                    **
                    ** setup playback
                    **
                    */
                    duration=(double)aiffbytes/2.0/(double)freq;    /* SSND */
                    loadsize=DELFINA_SAMPLES*2;
                    if(!mono)
                    {
                        duration/=2.0;
                        loadsize*=2;
                    }
                    adp4=0;
                    if(aifftype==ID_ADP4)    /* ADP4 */
                    {
                        adp4=1;
                        duration*=4.0;
                        loadsize>>=2;
                    }
                    millisec=(ULONG)(1000.0*modf(duration,&duration2));
                    minutes=(ULONG)(duration2/60.0);
                    seconds=(ULONG)(duration2-minutes*60.0);
                    printf("  time: %02ld min %02ld.%03ld sec\n",
                                (long)minutes, (long)seconds, (long)millisec);
                    if(delfina_48khz)
                    {
                        freq=(ULONG)((double)freq*48000.0/(double)delfina_48khz);
                        printf("  ////adjusted freq:   %ldHz\n",(long)freq);
                    }

                    /*
                    **
                    ** play file
                    **
                    */
                    curr_load=curr_play=&load_buf[0];
                    bytes_loaded=bytes_played=buffers_filled=buffers_missed=slow_adpcm4=0;
                    pause=0;
                    if(init_delfina())
                    {
                        printf("  playing..."); fflush(stdout);
                        taskpri=SetTaskPri(mytask,9);
                        GetSysTime(&time1);

                        while(1)
                        {
                            sigs=mytask->tc_SigRecvd;
                            if(sigs&SIGBREAKF_CTRL_C)
                            {
                                printf(" break CTRL-C\n");
                                mytask->tc_SigRecvd&=!SIGBREAKF_CTRL_C;
                                pause=1;
                                break;  /* exit loading-loop */
                            }
                            if(sigs&SIGBREAKF_CTRL_D)
                            {
                                printf(" break CTRL-D\n");
                                mytask->tc_SigRecvd&=!SIGBREAKF_CTRL_D;
                                pause=1;
                                ende=1;
                                break;  /* exit loading-loop */
                            }
                            if(sigs&SIGBREAKF_CTRL_E)
                            {
                                mytask->tc_SigRecvd&=!SIGBREAKF_CTRL_E;
                                if(pause)
                                {
                                    printf("\n  playing..."); fflush(stdout);
                                    pause=0;
                                }
                                else
                                {
                                    pause=1;
                                    printf(" pause"); fflush(stdout);
                                }
                            }
                            if( (buffers_filled<LOADBUFFERS) &&
                                (bytes_loaded<aiffbytes) )
                            {
                                ReadAsync(file,curr_load->ptr,loadsize);
                                curr_load=curr_load->next;
                                bytes_loaded+=loadsize;
                                buffers_filled++;
                            }
                            else
                            {
                                if( (bytes_loaded>=aiffbytes) &&
                                    (bytes_played>=aiffbytes) )
                                {
                                    printf(" done\n");
                                    pause=1;
                                    break;  /* exit loading-loop */
                                }
                                Delay(1);
                            }
                        }

                        GetSysTime(&time2);
                        SetTaskPri(mytask,taskpri);
                        if(buffers_missed>1)
                        {
                            printf("  note: irq was missing data %ld times\n",(long)buffers_missed-1);
                        }
                        if(slow_adpcm4)
                        {
                            printf("  note: ADPCM4 decoder finished late %ld times\n",(long)slow_adpcm4);
                        }
                        SubTime(&time2,&time1);
                        millisec=(ULONG)((double)time2.tv_micro/1000.0);
                        minutes=(ULONG)((double)time2.tv_secs/60.0);
                        seconds=(ULONG)((double)time2.tv_secs-minutes*60.0);
                        duration2=(double)time2.tv_secs+(double)time2.tv_micro/1000000.0;
                        if(delfina_48khz)
                        {
                            printf("  ////elapsed time:    %02ld:%02ld.%03ld\n",
                                    (long)minutes, (long)seconds, (long)millisec);
                            printf("  ////measured: DELFINA_48KHZ = %ldHz\n",(long)(48000.0*duration/duration2));
                        }
                    }
                    cleanup_delfina();
                }
                else printf("**file type not supported\n");
            }
            else printf("**not an AIFF file\n");
            CloseAsync(file);
        }
        else printf("**unable to open file\n");
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
    if(AsyncIOBase) CloseLibrary(AsyncIOBase);
    if(DelfinaBase) CloseLibrary(DelfinaBase);
    return(rc);
}





/*
 * C O N V E R T   F R O M   I E E E   E X T E N D E D  
 */

/* 
 * Copyright (C) 1988-1991 Apple Computer, Inc.
 * All rights reserved.
 *
 * Machine-independent I/O routines for IEEE floating-point numbers.
 *
 * NaN's and infinities are converted to HUGE_VAL or HUGE, which
 * happens to be infinity on IEEE machines.  Unfortunately, it is
 * impossible to preserve NaN's in a machine-independent way.
 * Infinities are, however, preserved on IEEE machines.
 *
 * These routines have been tested on the following machines:
 *    Apple Macintosh, MPW 3.1 C compiler
 *    Apple Macintosh, THINK C compiler
 *    Silicon Graphics IRIS, MIPS compiler
 *    Cray X/MP and Y/MP
 *    Digital Equipment VAX
 *
 *
 * Implemented by Malcolm Slaney and Ken Turkowski.
 *
 * Malcolm Slaney contributions during 1988-1990 include big- and little-
 * endian file I/O, conversion to and from Motorola's extended 80-bit
 * floating-point format, and conversions to and from IEEE single-
 * precision floating-point format.
 *
 * In 1991, Ken Turkowski implemented the conversions to and from
 * IEEE double-precision format, added more precision to the extended
 * conversions, and accommodated conversions involving +/- infinity,
 * NaN's, and denormalized numbers.
 */

#ifndef HUGE_VAL
# define HUGE_VAL HUGE
#endif /*HUGE_VAL*/

# define UnsignedToFloat(u)         (((double)((long)(u - 2147483647L - 1))) + 2147483648.0)

/****************************************************************
 * Extended precision IEEE floating-point conversion routine.
 ****************************************************************/

double ConvertFromIeeeExtended(unsigned char* bytes /* LCN */)
{
    double    f;
    int    expon;
    unsigned long hiMant, loMant;
    
    expon = ((bytes[0] & 0x7F) << 8) | (bytes[1] & 0xFF);
    hiMant    =    ((unsigned long)(bytes[2] & 0xFF) << 24)
            |    ((unsigned long)(bytes[3] & 0xFF) << 16)
            |    ((unsigned long)(bytes[4] & 0xFF) << 8)
            |    ((unsigned long)(bytes[5] & 0xFF));
    loMant    =    ((unsigned long)(bytes[6] & 0xFF) << 24)
            |    ((unsigned long)(bytes[7] & 0xFF) << 16)
            |    ((unsigned long)(bytes[8] & 0xFF) << 8)
            |    ((unsigned long)(bytes[9] & 0xFF));

    if (expon == 0 && hiMant == 0 && loMant == 0) {
        f = 0;
    }
    else {
        if (expon == 0x7FFF) {    /* Infinity or NaN */
            f = HUGE_VAL;
        }
        else {
            expon -= 16383;
            f  = ldexp(UnsignedToFloat(hiMant), expon-=31);
            f += ldexp(UnsignedToFloat(loMant), expon-=32);
        }
    }

    if (bytes[0] & 0x80)
        return -f;
    else
        return f;
}
