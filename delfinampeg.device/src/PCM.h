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


/** definitions for the Delfina-DSP56K program "PCM.o" **/


/** offsets in PROGRAM section **/
#define PROG_PCM_INIT       0
#define PROG_PCM_INTKEY     0
#define PROG_PCM_MUTE       2
#define PROG_PCM_MODULE     4


/** offsets in DATA sections (X/Y/L memory) **/
#define DATY_PCM_BUFPTR     0


/** additional DSP-memory allocations (internal/external, P/X/Y/L memory) **/
/* none */
