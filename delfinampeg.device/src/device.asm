;*****************************************************************************
;
;    delfinampeg.device - mpeg.device for Delfina DSP
;    Copyright (C) 2000-2003  Michael Henke
;
;    This program is free software; you can redistribute it and/or modify
;    it under the terms of the GNU General Public License as published by
;    the Free Software Foundation; either version 2 of the License, or
;    (at your option) any later version.
;
;    This program is distributed in the hope that it will be useful,
;    but WITHOUT ANY WARRANTY; without even the implied warranty of
;    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
;    GNU General Public License for more details.
;
;    You should have received a copy of the GNU General Public License
;    along with this program; if not, write to the Free Software
;    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
;
;*****************************************************************************


;** based on the framework of melodympeg.device
;** (C) Copyright 1998-2000 Kato Development (Thorsten Hansen)


VERSION		EQU	1
REVISION	EQU	6

; Hiermit wird das Device nach dem schliessen wieder automatisch
; aus dem System entfernt (EXPUNGE = 1). Sonst auf 0 setzen.
;;EXPUNGE		SET	1


	include "exec/exec.i"
	include "hardware/intbits.i"
	include "libraries/configvars.i"
	include "libraries/configregs.i"
	include "utility/utility.i"
	include "utility/tagitem.i"

	include	"melodympeg.i"

	XDEF	_DevName
	XDEF	_DelfinaBase

	XREF	_C_initunit	;unit init
	XREF	_C_expungeunit	;unit expunge
	XREF	_C_setpause	;MPEGCMD_PAUSE
	XREF	_C_setvolume	;MPEGCMD_SETAUDIOPARAMS
	XREF	_C_flush	;CMD_FLUSH
	XREF	_C_reset	;CMD_RESET
	XREF	_C_write	;CMD_WRITE

*
* Device Data Structure
*

MAX_UNITS	equ	1

; Hier Device globale Daten sichern (Library ptr, etc.)

 STRUCTURE MelodyDev,LIB_SIZE
	UBYTE	md_Flags
	UBYTE	md_Pad1
	APTR	md_SysLib
	APTR	md_SegList
	STRUCT	md_Units,MAX_UNITS*4	; Unit Ptr
	LABEL	MelodyDev_SIZE



****************************************************************************



	section	mycode,code

        moveq #-1,d0		;don't try do run this!
        rts                                

InitTable	dc.w	RTC_MATCHWORD
		dc.l	InitTable
		dc.l	EndCode
		dc.b	RTF_AUTOINIT
		dc.b	VERSION
		dc.b	NT_DEVICE
		dc.b	0		;DevPri
		dc.l	_DevName
		dc.l	IDString
		dc.l	Init__

		dc.b	"$VER: "
IDString	dc.b	"delfinampeg.device 1.6 (17.05.2003)",0

_DevName	dc.b	"delfinampeg.device",0

delflibname	dc.b	"delfina.library",0


*
* board description (256 byte)
*
* Dies ist eine kurze Beschreibung des MPEG Device. Diese kann von
* der Anwendung abgefragt werden.
*
	cnop	0,4
BoardDesc
	dc.b	"MPEG audio decoder for Delfina DSP."
	dc.b	" supports MPEG-1 audio Layer II/III."
	dc.b	" written by Michael Henke."
endtext
	dcb.b	256-(endtext-BoardDesc),0


_DelfinaBase	dc.l	0	;filled by device Init routine

Init__		dc.l	MelodyDev_SIZE
		dc.l	funcTable
		dc.l	dataTable
		dc.l	initRoutine

funcTable	dc.l	Open__
		dc.l	Close__
		dc.l	Expunge__
		dc.l	Null
		dc.l	BeginIO__
		dc.l	AbortIO__
		dc.l	SetSCR__
		dc.l	GetSCR__
		dc.l	Null
		dc.l	Null
		dc.l	-1

dataTable
	INITBYTE	LN_TYPE,NT_DEVICE
	INITLONG	LN_NAME,_DevName
	INITBYTE	LIB_FLAGS,LIBF_SUMUSED!LIBF_CHANGED
	INITWORD	LIB_VERSION,VERSION
	INITWORD	LIB_REVISION,REVISION
	INITLONG	LIB_IDSTRING,IDString
	dc.l	0




****************************************************************************
*
* This routine is called on the first device initialization
*
* -> d0 = device ptr, a0 = segment list, a6 = execbase
*
initRoutine
	movem.l	d1-d7/a0-a6,-(a7)
	move.l	d0,a5			;device ptr
	move.l	a6,md_SysLib(a5)	;save ptr to exec
	move.l	a0,md_SegList(a5)	;save ptr to our loaded code

	lea	delflibname(pc),a1
	moveq	#4,d0
	jsr	-552(a6)		;CALL OpenLibrary
	move.l	d0,_DelfinaBase		;global - for the 'C' routines
	beq.b	.init_end		;error: can't open delfina.library v4

	move.l	a5,d0			;no error: return device
.init_end
	movem.l	(a7)+,d1-d7/a0-a6
	rts




****************************************************************************
*
* Open wird beim öffnen des Device aufgerufen
*
* -> d0=unitnum, d1=flags, a1=ioreq, a6=device
*
Open__
	movem.l	d2-d7/a0-a6,-(a7)
	move.l	a6,a5			; device
	move.l	a1,a2
;----	 valid unit number?
	cmp.l	#MAX_UNITS,d0
	bge	.open_err
;----	 unit already initialized?
	move.l	d0,d3
	lea	md_Units(a5,d0.w*4),a4	; unit ptr
	move.l	(a4),d0
	bne.b	.open_err
	bsr	InitUnit		; unit:d3, devptr:a5
;----	 see if it initialized OK
	move.l	(a4),d0
	beq.b	.open_err
	move.l	d0,IO_UNIT(a2)
	addq	#1,LIB_OPENCNT(a5)
;----	 prevent delayed expunges
	bclr	#LIBB_DELEXP,md_Flags(a6)
	moveq	#0,d0
	move.b	d0,IO_ERROR(a2)
	move.b	#NT_REPLYMSG,LN_TYPE(a2) ; Mark IORequest as "complete" !
.open_end
	movem.l	(a7)+,d2-d7/a0-a6
	rts
.open_err
	moveq	#IOERR_OPENFAIL,d0
	move.b	d0,IO_ERROR(a2)
	move.l	d0,IO_DEVICE(a2)    ;IMPORTANT: trash IO_DEVICE on open failure
	bra.b	.open_end



****************************************************************************
*
* Close wird beim schliessen des Device aufgerufen
*
* -> a1=ioreq, a6=device
* <- d0=seglist oder 0
*
Close__
	movem.l	d1-d7/a1-a5,-(a7)
	move.l	a6,a5			; device ptr
	move.l	IO_UNIT(a1),a3
	moveq	#0,d0
	move.l	a3,d1
	beq.b	.close_end		; unit ist schon geschlossen
;----	 unit can be only open once one
	clr.l	IO_UNIT(a1)
	bsr	ExpungeUnit
	moveq	#0,d0
	subq	#1,LIB_OPENCNT(a6)
	bne.b	.close_end
;----	 see if we have a delayed expunge pending
	IFND	EXPUNGE
	btst	#LIBB_DELEXP,md_Flags(a6)
	beq.b	.close_end
	ENDC
	bsr.b	Expunge__
.close_end
	movem.l   (a7)+,d1-d7/a1-a5
	rts



****************************************************************************
*
* Device entfernen
*
* -> a6=device
*
Expunge__
	movem.l	d1-d2/a5-a6,-(a7)
	move.l	a6,a5			; device ptr
	move.l	md_SysLib(a5),a6	; ExecBase nach a6
	tst	LIB_OPENCNT(a5)
	beq.b	.exp_ok
;----	 it is still open.  set the delayed expunge flag
	bset	#LIBB_DELEXP,md_Flags(a5)
	moveq	#0,d0
	bra.b	.exp_end
;----	 device wird entfernt
.exp_ok	move.l	md_SegList(a5),d2	; Zeiger auf Segment Liste
	move.l	a5,a1			; device
	jsr	-252(a6)		;CALL Remove  Device aus Liste löschen

	move.l	_DelfinaBase(pc),d0
	beq.b	.no_delflib
	move.l	d0,a1
	jsr	-414(a6)		;CALL CloseLibrary
.no_delflib

	move.l	a5,a1			; device
	moveq	#0,d0
	move	LIB_NEGSIZE(a5),d0
	sub.l	d0,a1			; Anfang des Speichers für device
	add	LIB_POSSIZE(a5),d0	; Laenge vom Device bestimmen
	jsr	-210(a6)		;CALL FreeMem  Speicher freigeben
	move.l	d2,d0			; Segment Liste
.exp_end
	movem.l	(a7)+,d1-d2/a5-a6
	rts




****************************************************************************
*
* -> d3=unitnum, a5=device
*
InitUnit
	movem.l	d1-d7/a0-a6,-(a7)
	move.l	d3,-(a7)		;ULONG unitnum
	bsr	_C_initunit		;returns unit pointer
	addq.l	#4,a7
	movem.l	(a7)+,d1-d7/a0-a6
	move.l	d0,md_Units(a5,d3.w*4)	;store unit pointer
	rts


****************************************************************************
*
* -> a3=unit, a5=device
*
ExpungeUnit
	movem.l	d1-d7/a0-a6,-(a7)
	move.l	a3,-(a7)		;struct devunit*
	bsr	_C_expungeunit		;returns ULONG unitnum
	addq.l	#4,a7
	movem.l	(a7)+,d1-d7/a0-a6
	clr.l	md_Units(a5,d0.w*4)	;clear unit pointer
	rts


****************************************************************************

NSCMD_DEVICEQUERY	EQU	$4000

 STRUCTURE NSDeviceQueryResult,0
	ULONG	dqr_DevQueryFormat
	ULONG	dqr_SizeAvailable
	UWORD	dqr_DeviceType
	UWORD	dqr_DeviceSubType
	APTR	dqr_SupportedCommands
	LABEL	NSDeviceQueryResult_SIZEOF

NSDEVTYPE_UNKNOWN	EQU	0

cmdtable
	dc.w	Invalid-cmdtable	;0  CMD_INVALID
	dc.w	CMD_Reset__-cmdtable	;1  CMD_RESET
	dc.w	CMD_Read__-cmdtable	;2  CMD_READ			; no op
	dc.w	CMD_Write__-cmdtable	;3  CMD_WRITE
	dc.w	CMD_Update__-cmdtable	;4  CMD_UPDATE			; no op
	dc.w	CMD_Clear__-cmdtable	;5  CMD_CLEAR			; no op
	dc.w	CMD_Stop__-cmdtable	;6  CMD_STOP			; no op
	dc.w	CMD_Start__-cmdtable	;7  CMD_START			; no op
	dc.w	CMD_Flush__-cmdtable	;8  CMD_FLUSH
	dc.w	MPEG_Play-cmdtable	;9  MPEGCMD_PLAY
	dc.w	MPEG_Pause-cmdtable	;A  MPEGCMD_PAUSE
	dc.w	Invalid-cmdtable	;B  MPEGCMD_SLOWMOTION		; Not supported
	dc.w	Invalid-cmdtable	;C  MPEGCMD_SINGLESTEP		; Not supported
	dc.w	Invalid-cmdtable	;D  MPEGCMD_SEARCH		; Not supported
	dc.w	Invalid-cmdtable	;E  MPEGCMD_RECORD		; Not supported
	dc.w	MPEG_GetDevInfo-cmdtable ;F  MPEGCMD_GETDEVINFO
	dc.w	Invalid-cmdtable	;0  MPEGCMD_SETWINDOW		; Not supported
	dc.w	Invalid-cmdtable	;1  MPEGCMD_SETBORDER		; Not supported
	dc.w	Invalid-cmdtable	;2  MPEGCMD_GETVIDEOPARAMS	; Not supported
	dc.w	Invalid-cmdtable	;3  MPEGCMD_SETVIDEOPARAMS	; Not supported
	dc.w	MPEG_SetAudPara-cmdtable ;4  MPEGCMD_SETAUDIOPARAMS
	dc.w	Invalid-cmdtable	;5  MPEGCMD_PLAYLSN		; Not supported
	dc.w	Invalid-cmdtable	;6  MPEGCMD_SEEKLSN		; Not supported
	dc.w	Invalid-cmdtable	;7  MPEGCMD_READFRAMEYUV	; Not supported

cmdlist
	dc.w	CMD_RESET
	dc.w	CMD_WRITE
	dc.w	CMD_FLUSH
	dc.w	MPEGCMD_PLAY
	dc.w	MPEGCMD_PAUSE
	dc.w	MPEGCMD_GETDEVINFO
	dc.w	MPEGCMD_SETAUDIOPARAMS
;;;	dc.b	MPEGCMD_SETEQUALIZER	;what's this?? (from melodympeg.device)
	dc.w	0

	cnop	0,4



****************************************************************************
*
* BeginIO starts all incoming io. The IO is either queued up for the
* unit task or processed immediately.
*
* -> a1=ioreq, a6=device
*
BeginIO__
	movem.l	d1/a0/a3,-(a7)
	move.b	#NT_MESSAGE,LN_TYPE(a1)	; So WaitIO() is guaranteed to work
	move.l	IO_UNIT(a1),a3		; unit ptr
	move	IO_COMMAND(a1),d0
	; NS device query test
	cmp	#NSCMD_DEVICEQUERY,d0
	bne.b	.no_nsdquery
	bsr	NSDQuery
	bra.b	.beginio_end
.no_nsdquery
	; a valid command ?
	cmp	#MPEGCMD_END,d0
	bcc	.no_cmd
.immediate
	bsr.b   PerformIO__
.beginio_end
	movem.l	(a7)+,d1/a0/a3
	rts
.no_cmd	move.b	#IOERR_NOCMD,IO_ERROR(a1)
	bra.b	.beginio_end
*
* PerformIO actually dispatches an io request.	It might be called from
* the task, or directly from BeginIO (thus on the callers's schedule)
*
* -> a1=iorequest, a3=unitptr, a6=device
*
PerformIO__
	moveq	#0,d0
	move.b	d0,IO_ERROR(a1)		; kein Fehler
	move.b	IO_COMMAND+1(a1),d0	; low byte
	lea	cmdtable(pc),a0
	move.w	(a0,d0.w*2),d0
	jmp     (a0,d0.w)		; a1=ioreq, a6=devprt



****************************************************************************
*
* AbortIO() is a REQUEST to "hurry up" processing of an IORequest.
* If the IORequest was already complete, nothing happens (if an IORequest
* is quick or LN_TYPE=NT_REPLYMSG, the IORequest is complete).
* The message must be replied with ReplyMsg(), as normal.
*
* Note that AbortIO is called directly, not via BeginIO.
*
* If sucessful, AbortIO returns IOERR_ABORTED in IO_ERROR and zero in D0
*
* -> a1=iorequest, a6=device
*
AbortIO__
;;	moveq   #0,d0			; error code (always successful)
	moveq	#IOERR_NOCMD,d0		; return "AbortIO() request failed"
	rts


*
* -> a1=iorequest, a3=unitptr, a6=device
*
TermIO__
	btst	#IOB_QUICK,IO_FLAGS(a1)	; wenn quick, dann keine RyplyMsg
	bne.b   .termio_end
	move.l	a6,-(a7)
	move.l	md_SysLib(a6),a6
	jsr	-378(a6)		;CALL ReplyMsg (sets the LN_TYPE to NT_REPLYMSG)
	move.l	(a7)+,a6
.termio_end
	rts


NSDQuery
	move.l	IO_DATA(a1),a0		; NSDeviceQueryResult
	move.l	IO_LENGTH(a1),d0	; size
	move	#NSDEVTYPE_UNKNOWN,dqr_DeviceType(a0)
	move	#0,dqr_DeviceSubType(a0)
	move.l	#cmdlist,dqr_SupportedCommands(a0)
	move.l	#NSDeviceQueryResult_SIZEOF,dqr_SizeAvailable(a0)
	move.l	dqr_SizeAvailable(a0),IO_ACTUAL(a1)
	bra	TermIO__


;;Null	moveq	#0,d0
;;	rts

****************************************************************************
*
* Setzt die aktuelle System Clock Referenze
*
* -> a0=unit, d0=time
* <- d0=succes
*
Null
SetSCR__
GetSCR__
	moveq	#0,d0
	rts
*
* Gibt den aktuellen System Clock Reference wert
*
* -> a0=unit
* <- d0=clockValue (die unteren 32 bits vom SCR)
*
;;GetSCR__
;;	moveq	#0,d0
;;	rts



****************************************************************************
* hier beginnnen die Funktionen der Device Befehle
*
* Parameter:
* -> a1=iorequest, a3=unitptr, a6=device
*
Invalid
	move.b	#IOERR_NOCMD,IO_ERROR(a1)
	bra	TermIO__
*
* no operation
*
noOp
CMD_Read__
CMD_Update__
CMD_Clear__
CMD_Stop__
CMD_Start__
	clr.l	IO_ACTUAL(a1)
	bra	TermIO__


****************************************************************************
**
** MPEG device functions
**
* CMD_RESET - Decoder zurücksetzen
*
CMD_Reset__
	movem.l	d1-d7/a0-a6,-(a7)
	move.l	a3,-(a7)		;struct devunit*
	bsr	_C_reset		;returns nothing
	addq.l	#4,a7
	movem.l	(a7)+,d1-d7/a0-a6
	bra	TermIO__


****************************************************************************
*
* CMD_FLUSH - abort all CMD_WRITE requests and flush decoder buffer
*
* Abspieler stoppen und alle Requests abbrechen.
*
CMD_Flush__
	movem.l	d1-d7/a0-a6,-(a7)
	move.l	a3,-(a7)		;struct devunit*
	bsr	_C_flush		;returns nothing
	addq.l	#4,a7
	movem.l	(a7)+,d1-d7/a0-a6
	bra	TermIO__


****************************************************************************
*
* MPEGCMD_PLAY - start playback
*
* In diesem Device wird die Abspielhardware in der Write Funktion gestartet.
*
* INPUTS:
*   iomr_StreamType  - MPEGSTREAM_AUDIO, MPEGSTREAM_SYSTEM
*                      oder MPEGSTREAM_BYPASS abhängig vom stream typ.
*
MPEG_Play
;*** we don't need to do anything here...
;*** the first CMD_WRITE will automatically start playback
	bra	TermIO__


****************************************************************************
*
* MPEGCMD_Pause - set pause mode
*
* INPUTS:
*   iomr_StreamType  - MPEGSTREAM_AUDIO oder MPEGSTREAM_SYSTEM
*                      abhängig vom stream typ.
*   iomr_Arg1        - Pause-modus an: io_Arg ungleich 0
*                      Pause-modus aus: io_Arg gleich 0
*
MPEG_Pause
	movem.l	d1-d7/a0-a6,-(a7)
	move.l	a1,-(a7)		;struct IOMPEGReq*
	move.l	a3,-(a7)		;struct devunit*
	bsr	_C_setpause		;returns nothing
	addq.l	#8,a7
	movem.l	(a7)+,d1-d7/a0-a6
	bra	TermIO__


****************************************************************************
*
* MPEGCMD_GETDEVINFO - get device information
*
* INPUTS:
*   IO_DATA - Zeiger auf MPEGDevInfo struktur
*
MPEG_GetDevInfo
	movem.l	a0-a3,-(a7)
	move.l	IO_DATA(a1),a0		; MPEGDevInfo
	move	#1,mdi_Version(a0)
	move	#0,mdi_Flags(a0)
	move.l	#MPEGCF_PLAYRAWAUDIO|MPEGCF_PLAYAUDLAYER_II|MPEGCF_PLAYAUDLAYER_III,d0
	move.l	d0,mdi_BoardCapabilities(a0)
	lea	BoardDesc(pc),a2
	lea	mdi_BoardDesc(a0),a3
	move	#256-1,d0
.copylp
	move.b	(a2)+,(a3)+
	dbra	d0,.copylp
	movem.l	(a7)+,a0-a3
	bra	TermIO__


****************************************************************************
*
* MPEGCMD_SETAUDIOPARAMS - set playback parameter
*
* Hier wird die Lautstärkeregelung vorgenommen.
*
* INPUTS:
*   IO_DATA   - Zeiger auf MPEGAudioParams structure.
*   IO_LENGTH - Sizeof(struct MPEGAudioParams)
*
MPEG_SetAudPara
	movem.l	d1-d7/a0-a6,-(a7)
	move.l	a1,-(a7)		;struct IOMPEGReq*
	move.l	a3,-(a7)		;struct devunit*
	bsr	_C_setvolume		;returns nothing
	addq.l	#8,a7
	movem.l	(a7)+,d1-d7/a0-a6
	bra	TermIO__


****************************************************************************
*
* CMD_WRITE - write data to play
*
* INPUTS:
*   IO_DATA   - Zeiger auf Puffer mit Daten
*   IO_LENGTH - Anzahl der Bytes im Puffer
*
CMD_Write__
	movem.l	d1-d7/a0-a6,-(a7)
	clr.l	IO_ACTUAL(a1)		;actual number of bytes transferred
	move.l	a1,-(a7)		;struct IOMPEGReq*
	move.l	a3,-(a7)		;struct devunit*
	bsr	_C_write		;returns ULONG error
	addq.l	#8,a7
	movem.l	(a7)+,d1-d7/a0-a6
	tst.l	d0
	bne.b	.write_err
	rts
.write_err
	bra	TermIO__


EndCode
	END
