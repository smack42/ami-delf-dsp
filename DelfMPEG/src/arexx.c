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
#include <proto/rexxsyslib.h>
#include <proto/utility.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>


/**
*** in DelfMPEG.c
**/
extern ULONG verbose, ende, pause, havetag;
extern LONG volume;
extern UBYTE ID3v1_buffer[142];
void initPCM(void);
void setPosition(ULONG seconds);


/**
*** public variables
**/
struct Library *RexxSysBase;
char *rexxfilename[2]={NULL,NULL};
char *portname="DELFMPEG";
WORD rexxstatus, rexxerror;
char rexxfiletypebuf[512];
ULONG rexxduration, rexxposition;


static char rexxfilenamebuf[512];
static char rexxreturnbuf[512];
static struct MsgPort *rexxport=NULL;
static WORD myport=0;





char *initRexx(void)
{
    if(!(RexxSysBase=OpenLibrary("rexxsyslib.library",36)))
        return("unable to open rexxsyslib.library v36");

    if(FindPort(portname))
        return("found another DelfMPEG already running");

    if(!(rexxport=CreateMsgPort()))
        return("unable to create MsgPort");

    rexxport->mp_Node.ln_Pri=0;
    rexxport->mp_Node.ln_Name=portname;
    AddPort(rexxport);
    if(!FindPort(portname))
        return("AddPort() failed");
    myport=1;

    rexxfilenamebuf[0]=0;

    return(NULL);   /* ok */
}





void cleanupRexx(void)
{
    struct MsgPort *mp;
    struct Message *m;

    Forbid();
    mp=FindPort(portname);
    if(mp && myport)
    {
        while((m=GetMsg(mp))) ReplyMsg(m);
        RemPort(mp); myport=0;
    }
    Permit();
    if(rexxport) {DeleteMsgPort(rexxport); rexxport=NULL;}
    if(RexxSysBase) {CloseLibrary(RexxSysBase); RexxSysBase=NULL;}
}


static void print0(char *s)
{
    if(verbose) printf("  command '%s'\n",s);
}

static void retstr(struct RexxMsg *rm, char *comm, char *res)
{
    if(verbose) printf("  command '%s' returns '%s'\n",comm,res);
    if(rm->rm_Action&RXFF_RESULT)   /* result string expected? */
    {
        rm->rm_Result1=0;
        rm->rm_Result2=(LONG)CreateArgstring(res,(ULONG)strlen(res));
    }
}

void handleRexx(void)
{
    struct RexxMsg *rm;
    char *arg0, *substr;
    while((rm=(struct RexxMsg*)GetMsg(rexxport)))
    {
        if(IsRexxMsg(rm))
        {
            if((rm->rm_Action&RXCODEMASK)==RXCOMM)  /* it's a command */
            {
                arg0=ARG0(rm);
                if(Stricmp(arg0,"QUIT")==0)
                {
                    print0(arg0);
                    ende=2;
                    rexxstatus=0;   /* stop */
                } else
                if(Stricmp(arg0,"STOP")==0)
                {
                    print0(arg0);
                    ende=1;
                    rexxstatus=0;   /* stop */
                } else
                if(Stricmp(arg0,"PAUSE")==0)
                {
                    print0(arg0);
                    pause=2;
                    if(rexxstatus==1) rexxstatus=2; /* play -> pause */
                } else
                if(Stricmp(arg0,"CONTINUE")==0)
                {
                    print0(arg0);
                    pause=0;
                    if(rexxstatus==2) rexxstatus=1; /* pause -> play */
                } else
                if(Strnicmp(arg0,"PLAY",4)==0)
                {
                    substr=arg0+4;
                    while((*substr)==' ') substr++;
                    if((*substr)==0) printf("**command '%s' - missing parameter <file>\n",arg0);
                    else
                    {
                        print0(arg0);
                        strncpy(rexxfilenamebuf,substr,sizeof(rexxfilenamebuf));
                        rexxfilename[0]=rexxfilenamebuf;
                    }
                } else
                if(Strnicmp(arg0,"SETVOLUME",9)==0)
                {
                    substr=arg0+9;
                    while((*substr)==' ') substr++;
                    if((*substr)==0) printf("**command '%s' - missing parameter <volume>\n",arg0);
                    else
                    {
                        LONG v=atol(substr);
                        if((v<0) || (v>200))
                            printf("**command '%s' - invalid parameter value\n",arg0);
                        else
                        {
                            print0(arg0);
                            volume=v; initPCM();
                        }
                    }
                } else
                if(Strnicmp(arg0,"SETPOSITION",11)==0)
                {
                    substr=arg0+11;
                    while((*substr)==' ') substr++;
                    if((*substr)==0) printf("**command '%s' - missing parameter <seconds>\n",arg0);
                    else
                    {
                        LONG pos=(ULONG)atol(substr);
                        if(rexxstatus==0)
                            printf("**command '%s' - not allowed in STOP mode\n",arg0);
                        else
                        {
                            print0(arg0);
                            if(pos<0) pos=0;
                            setPosition((ULONG)pos);
                        }
                    }
                } else
                if(Stricmp(arg0,"GETSTATUS")==0)
                {
                    sprintf(rexxreturnbuf,"%d",rexxerror?rexxerror:rexxstatus);
                    retstr(rm,arg0,rexxreturnbuf);
                } else
                if(Stricmp(arg0,"GETFILENAME")==0)
                {
                    strcpy(rexxreturnbuf,rexxfilenamebuf);
                    retstr(rm,arg0,rexxreturnbuf);
                } else
                if(Stricmp(arg0,"GETFILETYPE")==0)
                {
                    strcpy(rexxreturnbuf,rexxfiletypebuf);
                    retstr(rm,arg0,rexxreturnbuf);
                } else
                if(Stricmp(arg0,"GETDURATION")==0)
                {
                    sprintf(rexxreturnbuf,"%ld",rexxduration);
                    retstr(rm,arg0,rexxreturnbuf);
                } else
                if(Stricmp(arg0,"GETPOSITION")==0)
                {
                    sprintf(rexxreturnbuf,"%ld",rexxposition);
                    retstr(rm,arg0,rexxreturnbuf);
                } else
                if(Stricmp(arg0,"GETTITLE")==0)
                {
                    if(!havetag) rexxreturnbuf[0]=0;
                    else strcpy(rexxreturnbuf,&ID3v1_buffer[00]);
                    retstr(rm,arg0,rexxreturnbuf);
                } else
                if(Stricmp(arg0,"GETARTIST")==0)
                {
                    if(!havetag) rexxreturnbuf[0]=0;
                    else strcpy(rexxreturnbuf,&ID3v1_buffer[31]);
                    retstr(rm,arg0,rexxreturnbuf);
                } else
                if(Stricmp(arg0,"GETALBUM")==0)
                {
                    if(!havetag) rexxreturnbuf[0]=0;
                    else strcpy(rexxreturnbuf,&ID3v1_buffer[62]);
                    retstr(rm,arg0,rexxreturnbuf);
                } else
                if(Stricmp(arg0,"GETCOMMENT")==0)
                {
                    if(!havetag) rexxreturnbuf[0]=0;
                    else strcpy(rexxreturnbuf,&ID3v1_buffer[98]);
                    retstr(rm,arg0,rexxreturnbuf);
                } else
                if(Stricmp(arg0,"GETYEAR")==0)
                {
                    if(!havetag) rexxreturnbuf[0]=0;
                    else strcpy(rexxreturnbuf,&ID3v1_buffer[93]);
                    retstr(rm,arg0,rexxreturnbuf);
                } else
                if(Stricmp(arg0,"GETGENRE")==0)
                {
                    if(!havetag) rexxreturnbuf[0]=0;
                    else sprintf(rexxreturnbuf,"%d",(int)ID3v1_buffer[141]);
                    retstr(rm,arg0,rexxreturnbuf);
                } else
                printf("**unknown command '%s'\n",arg0);
            }
        }
        ReplyMsg((struct Message*)rm);
    }
}

