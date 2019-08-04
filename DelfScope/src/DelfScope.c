/*****************************************************************************

    DelfScope - oscilloscope/analyzer for Delfina DSP
    Copyright (C) 2000  Michael Henke

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


#define RTGMODE__not

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/timer.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/utility.h>
#include <proto/reqtools.h>
#include <exec/interrupts.h>
#include <hardware/intbits.h>
#include <exec/nodes.h>
#include <exec/memory.h>
#include <exec/libraries.h>
#include <exec/execbase.h>
#include <graphics/gfx.h>
#include <graphics/rastport.h>
#include <graphics/text.h>
#include <graphics/displayinfo.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <devices/timer.h>
#include <libraries/delfina.h>
#include <libraries/reqtools.h>
#include <math.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "scope_asm.h"
#include "scope_dsp.h"
extern struct DelfObj DSP56K_SCOPE;

static UBYTE version[]="$VER: DelfScope 0.1 (Tue 03-Oct-2000)";
#ifdef RTGMODE
static UBYTE template[]="-M=MODEID/K,-I=INPUT/K,RTG/S";
#else
static UBYTE template[]="-M=MODEID/K,-I=INPUT/K";
#endif

static struct MsgPort *delfport=NULL;
static struct DelfPrg *prg_scope=NULL;
static struct DelfModule *mod_scope=NULL;
struct Library *DelfinaBase=NULL;
struct ReqToolsBase *ReqToolsBase=NULL;
extern struct ExecBase *SysBase;    /*auto-init fills this!*/
void *GFXBASE;  /*used by the ASM plot routines in RTG-mode*/

int vblank_count(void);
int vblank_plot(void);
int softint_plot(void);
int check_idcmp(void);
static struct Interrupt vblank_int=
{
    NULL,NULL, NT_INTERRUPT, 127, "DelfScope_VBLANK",   /* struct Node */
    NULL,NULL
};
static struct Interrupt vblank_softint=
{
    NULL,NULL, NT_UNKNOWN, 32, NULL,    /* struct Node */
    NULL,(void(*)(void))softint_plot
};
static struct Interrupt window_int=
{
    NULL,NULL, NT_UNKNOWN, 0, NULL,     /* struct Node */
    NULL,(void(*)(void))check_idcmp
};

static UBYTE *chipmem=NULL;
static struct BitMap bmap=
{
    640/8, 480,         /* BytesPerRow, Rows */
    0, 2, 0,            /* Flags, Depth, pad */
    0,0,0,0,0,0,0,0     /* Planes */
};
static struct TextFont *textfont;
static struct TextAttr textattr=
{
    "topaz.font", 8, FS_NORMAL, FPF_ROMFONT
};
static struct Screen *myscreen=NULL;
static struct RastPort *rp;
static struct NewScreen newscr=
{
    0, 0, 640, 480, 2,  /* LeftEdge, TopEdge, Width, Height, Depth */
    0, 0, 0,            /* DetailPen, BlockPen, ViewModes */
    AUTOSCROLL|SCREENQUIET|SCREENHIRES|CUSTOMBITMAP|CUSTOMSCREEN,   /* Type */
    0, 0, 0, &bmap      /* Font, DefaultTitle, Gadgets, CustomBitMap */
};
static struct Window *mywindow=NULL;
static struct NewWindow newwin=
{
    0, 0, 640, 480,     /* LeftEdge, TopEdge, Width, Height */
    0, 0,               /* DetailPen, BlockPen */
    IDCMP_VANILLAKEY|IDCMP_INACTIVEWINDOW|IDCMP_ACTIVEWINDOW, /* IDCMPFlags */
    WFLG_RMBTRAP|WFLG_NOCAREREFRESH|WFLG_ACTIVATE|WFLG_BORDERLESS|WFLG_BACKDROP,    /* Flags */
    0, 0, 0,            /* FirstGadget, CheckMark, Title */
    0, 0,               /* Screen, BitMap */
    0, 0, 0, 0,         /* MinWidth, MinHeight, MaxWidth, MaxHeight */
    CUSTOMSCREEN        /* Type */
};
static ULONG palette[]=
{
    4<<16,              /* (count<<16)+first */
    0x00000000,0x40404040,0x00000000,
    0x60606060,0xb0b0b0b0,0x60606060,
    0x30303030,0x70707070,0x30303030,
    0x50505050,0x90909090,0x50505050,
    0                   /* end marker */
};

static UBYTE pcmbuf[2048], txtbuf[128], *inputmodule=NULL;
static ULONG modeid=0x8004, rtgmode=0, inputclip=0;
static LONG countvblank= -1, countmod, fps, spf, too_slow=0, inputgain=0;
static WORD ende=0, addint=0, pause=0, softint_busy=0, inputconnected=0, inputreconnect=0;
static WORD peak_l=0, peakhold_l=0, peak_r=0, peakhold_r=0;
static WORD redraw_count=0, redraw=2, plot_type=0;
static LONG plot_leftbound, plot_rightbound;
static WORD newstatus=0, newmain=0;








int __saveds vblank_count(void)
{
    if(countvblank<0)   /** start simultaneously **/
    {
        countvblank=0;
        Delf_Poke(prg_scope->ydata+DATY_SCOPE_MODCOUNT,DMEMF_YDATA,0);
    }
    else                /** count **/
    {
        countvblank++;
        countmod=Delf_Peek(prg_scope->ydata+DATY_SCOPE_MODCOUNT,DMEMF_YDATA);
    }
    return(0);
}

int __saveds vblank_plot(void)
{
    if(!pause && !inputreconnect)
    {
        if(--redraw_count<=0)
        {
            redraw_count=redraw;
            if(!softint_busy)
            {
                softint_busy=1;
                Cause(&vblank_softint);
            }
            else {too_slow++; redraw_count=1;}
        }
    }
    return(0);
}




WORD __saveds plot_bar(UBYTE *p, ULONG avg, WORD peak)
{
    UBYTE *q=p-((peak>0)?peak*640/8:0);
    double l;
    WORD i,v=0;
    if(avg)
    {
        l=log((double)avg/(double)spf/(double)(0x2000))*0.1447648;  /* 60dB */
        v=(UWORD)((l+1.0)*255.0+0.5);
        v=(v>255)?255:((v<0)?0:v);
    }
    for(i=v; i>0; i--)      { (*p)=0xff; p-=640/8; }
    for(i=255-v; i>=0; i--) { (*p)=0x00; p-=640/8; }
    (*q)=0xff;
    return(v);
}

int __saveds softint_plot(void)
{
    WORD v;

    ASM_prepare_plot();

    /*while(Delf_Peek(prg_scope->xdata+DATX_SCOPE_BUSY,DMEMF_XDATA));*/

    v=plot_bar(chipmem+2+640/8*448,Delf_Peek(prg_scope->xdata+DATX_SCOPE_AVGL,DMEMF_XDATA),peak_l);
    if(v>peak_l) {peak_l=v; peakhold_l=50;}
    else if(peakhold_l>0) peakhold_l-=redraw; else peak_l-=redraw;
    v=plot_bar(chipmem+4+640/8*448,Delf_Peek(prg_scope->xdata+DATX_SCOPE_AVGR,DMEMF_XDATA),peak_r);
    if(v>peak_r) {peak_r=v; peakhold_r=50;}
    else if(peakhold_r>0) peakhold_r-=redraw; else peak_r-=redraw;

    if(plot_type==1)    /* oscilloscope */
    {
        Delf_CopyD2A(prg_scope->ldata+DATL_SCOPE_PCMBUF,pcmbuf,spf>>1,DCPF_XDATA|DCPF_24BIT);
        Delf_CopyD2A(prg_scope->ldata+DATL_SCOPE_PCMBUF,pcmbuf+1024,spf>>1,DCPF_YDATA|DCPF_24BIT);
        Delf_Run(prg_scope->prog+PROG_SCOPE_CONVERT, 0, DRUNF_ASYNCH,
                 spf, (ULONG)plot_type, 0, 0);
        ASM_pcm_plot(pcmbuf,chipmem+640/8*48+(plot_leftbound>>3),spf>>1);
    }
    if(plot_type==2)    /* spectrum analyzer */
    {
        Delf_CopyD2A(prg_scope->ldata+DATL_SCOPE_PCMBUF,pcmbuf,416*2,DCPF_XDATA|DCPF_16BITH);
        Delf_Run(prg_scope->prog+PROG_SCOPE_CONVERT, 0, DRUNF_ASYNCH,
                 spf, (ULONG)plot_type, 0, 0);
        ASM_fft_plot((UWORD*)pcmbuf,chipmem+640/8*113+192/8,(rtgmode)?rp:NULL);
    }

    softint_busy=0;
    return(0);
}






int __saveds check_idcmp(void)
{
    struct IntuiMessage *msg;
    while((msg=(struct IntuiMessage*)GetMsg(mywindow->UserPort)))
    {
        switch(msg->Class)
        {
        case IDCMP_VANILLAKEY:
            newstatus++;
            switch(msg->Code)
            {
            case 27:    /*ESC*/
                ende++;
                break;
            case 32:    /*Space*/
                pause=pause?0:1;
                break;
            case '1':   /*Numbers*/
            case '2':
            case '3':
            case '4':
            case '5':
            case '6':
            case '7':
            case '8':
            case '9':
                redraw=msg->Code-'0';
                break;
            case '+':
                if(inputgain<=210)
                    Delf_SetAttrs(DA_InputGain,((inputgain/15+1)<<12)|0xfff,TAG_END,0);
                break;
            case '-':
                if(inputgain>=15)
                    Delf_SetAttrs(DA_InputGain,((inputgain/15-1)<<12)|0xfff,TAG_END,0);
                break;
            case 'o':
            case 'O':
                plot_type=1; newmain++;
                break;
            case 's':
            case 'S':
                plot_type=2; newmain++;
                break;
            default:
                break;
            }
            break;
        case IDCMP_INACTIVEWINDOW:
            pause++;
            break;
        case IDCMP_ACTIVEWINDOW:
            if(pause>0) pause--;
            break;
        }
        ReplyMsg((struct Message*)msg);
    }
    return(0);
}




void printstatus(void)
{
    WORD fake_pause=0;
    if(pause==0) {pause=99; fake_pause++;}
    SetDrMd(rp,JAM2);
    SetAPen(rp,1);
    SetBPen(rp,0);

    Move(rp,8,40+4+8);
    sprintf(txtbuf,"fps=%d",fps);
    Text(rp,txtbuf,(ULONG)strlen(txtbuf));
    Move(rp,8,40+4+8+8);
    sprintf(txtbuf,"redraw=%d",redraw);
    Text(rp,txtbuf,(ULONG)strlen(txtbuf));
    Move(rp,8,40+4+8+8+8+8);
    sprintf(txtbuf,"pause=o%s",(!fake_pause)?"n ":"ff");
    Text(rp,txtbuf,(ULONG)strlen(txtbuf));

    if(inputreconnect)
    {
        struct DelfModule *dm;
        inputconnected=0;
        ObtainSemaphore(mod_scope->semaphore);  /*Disable();*/
        dm=Delf_FindModule(inputmodule);
        if(Delf_SetPipe(dm,0,mod_scope,0) && dm) inputconnected=1;
        ReleaseSemaphore(mod_scope->semaphore); /*Enable();*/
        inputreconnect=0;
    }

    if(!inputmodule || strcmp("Connectors",inputmodule)==0)
    {
        sprintf(txtbuf,"gain=%d.%ddB ",inputgain/10,inputgain%10);
        Move(rp,8,104+4+8);
        Text(rp,"rate=48000Hz",12);
        Move(rp,8,104+4+8+8);
        Text(rp,txtbuf,(ULONG)strlen(txtbuf));
        Move(rp,8,104+4+8+8+8+8);
        if(inputclip) {SetDrMd(rp,JAM2|INVERSVID); Text(rp,"  CLIPPING  ",12);}
        else {SetAPen(rp,2); Text(rp,"no clipping ",12);}
    }
    else
    {
        Move(rp,8,104+4+8+8);
        Text(rp,inputmodule,(ULONG)strlen(inputmodule));
        Move(rp,8,104+4+8+8+8);
        if(!inputconnected) {Text(rp,"<not available>",15);}
        else {SetAPen(rp,2); Text(rp,"<connected>    ",15);}
    }

    newstatus=0;
    if(fake_pause) pause=0;
}

void printmain(void)
{
    UBYTE *p;
    WORD i;

    pause+=99;
    SetDrMd(rp,JAM2);
    SetAPen(rp,1);
    SetBPen(rp,0);

    Move(rp,0,0);
    ClearScreen(rp);

    Move(rp,8,16);
    Text(rp,"Delfina Scope",13);
    Move(rp,8,40);
    Text(rp,"display:",8);
    Move(rp,8,104);
    Text(rp,"audio in:",9);
    Move(rp,8,480-8);
    Text(rp,"An INFECT Millennium Production",31);
    Move(rp,640-20*8,480-8);
    Text(rp,"programmed by Smack",19);

    SetAPen(rp,2);
    Move(rp,8,40+4+8+8+8);
    Text(rp,"[1]...[9]",9);
    Move(rp,8,40+4+8+8+8+8+8);
    Text(rp,"[space]",7);
    Move(rp,8,104+4+8+8+8);
    Text(rp,"[+] [-]",7);

    p=chipmem+2+640/8*448+640/8*480;
    for(i=255; i>0; i-=2) { (*p)=0xff; p-=640/8*2; }
    p=chipmem+4+640/8*448+640/8*480;
    for(i=255; i>0; i-=2) { (*p)=0xff; p-=640/8*2; }
    Move(rp,44,448-256+4);
    Text(rp," dB",3);
    Move(rp,44,448-256+4+43);
    Text(rp,"-10",3);
    Move(rp,44,448-256+4+85);
    Text(rp,"-20",3);
    Move(rp,44,448-256+4+128);
    Text(rp,"-30",3);
    Move(rp,44,448-256+4+171);
    Text(rp,"-40",3);
    Move(rp,44,448-256+4+213);
    Text(rp,"-50",3);
    Move(rp,44,448-256+4+256);
    Text(rp,"-60",3);

    if(plot_type==1)    /* oscilloscope */
    {
        SetAPen(rp,1);
        Move(rp,128+(512-14*8)/2,16);
        Text(rp,"[o]scilloscope",14);
        SetAPen(rp,2);
        Move(rp,128+(512-19*8)/2,16+8);
        Text(rp,"[s]pectrum analyzer",19);
        Move(rp,128+(512-12*8)/2,48+96-3);
        Text(rp,"left channel",12);
        Move(rp,plot_leftbound,48);
        Draw(rp,plot_rightbound,48);
        Move(rp,plot_leftbound,48+96);
        Draw(rp,plot_rightbound,48+96);
        Move(rp,128+(512-13*8)/2,48+96-3+192);
        Text(rp,"right channel",13);
        Move(rp,plot_leftbound,48+192);
        Draw(rp,plot_rightbound,48+192);
        Move(rp,plot_leftbound,48+96+192);
        Draw(rp,plot_rightbound,48+96+192);
        Move(rp,plot_leftbound,48+96+192+96);
        Draw(rp,plot_rightbound,48+96+192+96);
    }
    if(plot_type==2)    /* spectrum analyzer */
    {
        LONG x;
        Move(rp,128+(512-14*8)/2,16);
        Text(rp,"[o]scilloscope",14);
        SetAPen(rp,1);
        Move(rp,128+(512-19*8)/2,16+8);
        Text(rp,"[s]pectrum analyzer",19);
        SetAPen(rp,2);
        Move(rp,192,112); Draw(rp,192+416,112);
        Move(rp,192-4-3*8,112+3); Text(rp,"-20",3);
        Move(rp,192-8-2*8,112+3+21); Text(rp,"dB",2);
        Move(rp,192,112+43); Draw(rp,192+416,112+43);
        Move(rp,192-4-3*8,112+43+3); Text(rp,"-30",3);
        Move(rp,192,112+85); Draw(rp,192+416,112+85);
        Move(rp,192-4-3*8,112+85+3); Text(rp,"-40",3);
        Move(rp,192,112+128); Draw(rp,192+416,112+128);
        Move(rp,192-4-3*8,112+128+3); Text(rp,"-50",3);
        Move(rp,192,112+171); Draw(rp,192+416,112+171);
        Move(rp,192-4-3*8,112+171+3); Text(rp,"-60",3);
        Move(rp,192,112+213); Draw(rp,192+416,112+213);
        Move(rp,192-4-3*8,112+213+3); Text(rp,"-70",3);
        Move(rp,192,112+256); Draw(rp,192+416,112+256);
        Move(rp,192-4-3*8,112+256+3); Text(rp,"-80",3);

        Move(rp,192-16,384); Text(rp,"kHz",3);
        for(i=500;i<19500;i+=500)
        {
            rp->LinePtrn=0x5555;
            x=192+(LONG)((double)i*1024.0/48000.0+0.5);
            Move(rp,x,372); Draw(rp,x,108); i+=500;
            rp->LinePtrn=0xffff;
            x=192+(LONG)((double)i*1024.0/48000.0+0.5);
            Move(rp,x,374); Draw(rp,x,106);
            Move(rp,x-(i>9999?8:4),384);
            sprintf(txtbuf,"%d",i/1000); Text(rp,txtbuf,(ULONG)strlen(txtbuf));
        }
    }

    if(pause>=99) pause-=99;
    printstatus();
    newmain=0;
}




int main(void)
{
    struct RDArgs *rdargs;
    LONG args[3]={0,0,0};
    struct rtScreenModeRequester *rtscmreq=NULL;
    struct NameInfo nameinfo;
    struct DelfMsg *delfmsg;
    int rc=0;

    GFXBASE=FindName(&SysBase->LibList,"graphics.library");
    rdargs=ReadArgs(template,args,NULL);
    printf("\33[1m%s by Smack/Infect!\33[0m\n",&version[6]);
    inputmodule=(UBYTE*)args[1];
    rtgmode=args[2];
    if(args[0])
    {
        int b=10;
        UBYTE *pt=(UBYTE*)args[0];
        if(pt[0]=='$') {pt++; b=16;}
        if((pt[0]=='0')&&((pt[1]=='x')||(pt[1]=='X'))) {pt+=2; b=16;}
        modeid=strtoul(pt,NULL,b);
    }
    else
    {
        if((ReqToolsBase=(struct ReqToolsBase*)OpenLibrary("reqtools.library",38)))
        {
            if((rtscmreq=rtAllocRequest(RT_SCREENMODEREQ,NULL)))
            {
                if(!rtScreenModeRequest(rtscmreq, "DelfScope: select (640x480) mode",
                                        RTSC_Flags, SCREQF_GUIMODES,
                                        RTSC_MinWidth, 640,
                                        RTSC_MinHeight, 480,
                                        RTSC_MinDepth, 2,
                                        RTSC_MaxDepth, 8,
                                        TAG_DONE ))
                goto exit_clean;
                modeid=rtscmreq->DisplayID;
            }
        }
    }
    if(!GetDisplayInfoData(NULL,(UBYTE*)&nameinfo,sizeof(struct NameInfo),DTAG_NAME,modeid)) nameinfo.Name[0]='\0';
    printf("  selected: modeid=0x%x (%s)\n",modeid,nameinfo.Name);
    if(inputmodule) printf("  input module: '%s'\n",inputmodule);
#ifdef RTGMODE
    printf("  RTG mode=o%s\n",(rtgmode)?"n":"ff");
#endif

    if(!(DelfinaBase=OpenLibrary("delfina.library",4)))
    {
        printf("**unable to open delfina.library V4\n");
        rc=20;
        goto exit_clean;
    }
    if(!(prg_scope=Delf_AddPrg(&DSP56K_SCOPE)))
    {
        printf("**not enough Delfina memory\n");
        rc=20;
        goto exit_clean;
    }
    if(!(mod_scope=Delf_AddModule(DM_Inputs,1, DM_Outputs,1,
                                  DM_Code,prg_scope->prog+PROG_SCOPE_MODULE,
                                  DM_Freq,48000,
                                  DM_Name,"DelfScope",
                                  0)))
    {
        printf("**couldn't create DelfModule\n");
        rc=20;
        goto exit_clean;
    }
    if(!(delfport=Delf_StartNotify(DA_InputGainL,DA_InputClip,DA_UpdateMod,0)))
    {
        printf("**couldn't create Delf_Notify port\n");
        rc=20;
        goto exit_clean;
    }
    inputgain=15*(Delf_GetAttr(DA_InputGainL,0)>>12);
    inputconnected=1;
    if(inputmodule)
    {
        struct DelfModule *dm;
        dm=Delf_FindModule(inputmodule);
        if(Delf_SetPipe(dm,0,mod_scope,0) && dm) printf("  input connect ok.\n");
        else {printf("**input connect failed.\n"); inputconnected=0;}
    }

    if(!(chipmem=AllocMem(640*480/4,MEMF_CHIP|MEMF_CLEAR|MEMF_PUBLIC)))
    {
        printf("**not enough graphics memory\n");
        rc=20;
        goto exit_clean;
    }
    bmap.Planes[0]=chipmem;
    bmap.Planes[1]=chipmem+640*480/8;
    if(!(myscreen=OpenScreenTags(&newscr,
                                 SA_DisplayID,modeid,
                                 SA_Colors32,palette,
                                 SA_Overscan,OSCAN_STANDARD,
                                 SA_BackFill,1, /*LAYERS_NOBACKFILL*/
                                 TAG_END,0)))
    {
        printf("**unable to open screen\n");
        rc=20;
        goto exit_clean;
    }
    rp= &myscreen->RastPort;
    {   /*** check the properties of the screen we just opened ***/
        ULONG modeid2;
        modeid2=GetVPModeID(&myscreen->ViewPort);
        if(!GetDisplayInfoData(NULL,(UBYTE*)&nameinfo,sizeof(struct NameInfo),DTAG_NAME,modeid2)) nameinfo.Name[0]='\0';
        if(modeid!=modeid2) printf("  actual:   modeid=0x%x (%s)  **promoted**\n",modeid2,nameinfo.Name);
        if(rp->BitMap->Planes[0]!=chipmem)
        {
            printf("**it seems that we've got a graphics board screenmode here!\n"
                   "**sorry, but this program works with Amiga native (ECS/AGA) modes only.\n");
            goto exit_clean;
        }
    }
    textfont=OpenFont(&textattr);
    SetFont(rp,textfont);
    newwin.Screen=myscreen;
    if(!(mywindow=OpenWindowTags(&newwin,
                                 WA_BackFill,1, /*LAYERS_NOBACKFILL*/
                                 TAG_END,0)))
    {
        printf("**unable to open window\n");
        rc=20;
        goto exit_clean;
    }
    Disable();
    mywindow->UserPort->mp_Flags=PA_SOFTINT;
    mywindow->UserPort->mp_SigTask=(void*)&window_int;
    Enable();

    printmain();
    SetAPen(rp,1);
    Move(rp,128+(512-17*8)/2,240);
    Text(rp,"< synchronizing >",17);
    {
        double d_fps, d_spf;
        vblank_int.is_Code=(void(*)(void))vblank_count;
        AddIntServer(INTB_VERTB,&vblank_int);
        while(countvblank<200);
        RemIntServer(INTB_VERTB,&vblank_int);
        d_fps=(double)countvblank/((double)countmod*128.0/48000.0);
        d_spf=(double)countmod*128.0/(double)countvblank;
        fps=(LONG)(d_fps+0.5);
        spf=(LONG)(d_spf+0.5);
        if(spf>1024) spf=1024;
        printf("  vbl=%d mod=%d   fps=%f (%d)  spf=%f (%d)\n",
                countvblank,countmod,d_fps,fps,d_spf,spf );
        fflush(stdout);
        plot_leftbound=128+((((1024/2-(spf>>1)-31)>>5))<<5);
        plot_rightbound=plot_leftbound+((spf>>4)<<3)-1;
    }
    plot_type=1;
    printmain();
    newstatus++;

    vblank_int.is_Code=(void(*)(void))vblank_plot;
    AddIntServer(INTB_VERTB,&vblank_int);
    addint++;

    while(!ende)
    {
        Delay(1);
        while((delfmsg=(struct DelfMsg*)GetMsg(delfport)))
        {
            struct TagItem *ti;
            if((ti=FindTagItem(DA_InputGainL,delfmsg->taglist)))
                inputgain=15*(ti->ti_Data>>12);
            if((ti=FindTagItem(DA_InputClip,delfmsg->taglist)))
                inputclip=ti->ti_Data;
            if((ti=FindTagItem(DA_UpdateMod,delfmsg->taglist)))
            {
                /*printf(":::updateMod::: '%s'\n",ti->ti_Data);*/
                if(strcmp("DelfScope",(UBYTE*)ti->ti_Data)==0) inputreconnect++;
                else if(inputmodule && strcmp(inputmodule,(UBYTE*)ti->ti_Data)==0) inputreconnect++;
            }
            delfmsg->result=0;
            ReplyMsg(&delfmsg->msg);
            newstatus++;
        }
        if(newmain) printmain();
        if(newstatus) printstatus();
    }

    if(too_slow>0) printf("  detected REDRAW_BUSY %d times\n",too_slow);

exit_clean:
    if(addint) RemIntServer(INTB_VERTB,&vblank_int);
    if(delfport) Delf_EndNotify(delfport);
    if(mod_scope) Delf_RemModule(mod_scope);
    if(prg_scope) Delf_RemPrg(prg_scope);
    if(DelfinaBase) CloseLibrary(DelfinaBase);
    if(mywindow) CloseWindow(mywindow);
    if(myscreen) CloseScreen(myscreen);
    if(chipmem) FreeMem(chipmem,640*480/4);
    if(textfont) CloseFont(textfont);
    if(rdargs) FreeArgs(rdargs);
    if(rtscmreq) rtFreeRequest(rtscmreq);
    if(ReqToolsBase) CloseLibrary((struct Library*)ReqToolsBase);
    return(rc);
}

