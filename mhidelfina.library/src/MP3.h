/*****************************************************************************

    mhidelfina.library - MHI driver for Delfina DSP
    Copyright (C) 1999-2026  Michael Henke

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


/** definitions for the Delfina-DSP56K program "MP3.o" **/
extern struct DelfObj DSP56K_MP3;


/** offsets in PROGRAM section **/
#define PROG_MP3_INIT       0
#define PROG_MP3_DECODE     2


/** offsets in DATA sections (X/Y/L memory) **/
#define DATX_MP3_INBUF      2048
#define DATY_MP3_BUSY       16
#define DATY_MP3_FORCEMONO  17
#define DATY_MP3_POW43TAB   18


/** additional DSP-memory allocations (internal/external, P/X/Y/L memory) **/
#define INTP_MP3_PROG       27
#define INTL_MP3_DATL       126
