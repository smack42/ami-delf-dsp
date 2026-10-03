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


/* proposed MHI extensions */



/* 1. Stream Info
 *
 * MHI driver generates text messages containing information about the stream
 * that is currently decoding. This can be static info about the audio
 * format and maybe dynamic info too, such as number of decoded frames etc.
 *
 * Applications can query this info and display it without knowing anything
 * about the structure of the streams and the audio formats.
 */

/* experimental implementation in mhidelfina.library and DelfMHIplay
 * uses AmigaDOS Local Variables of these names */
#define MHI_EXT_SETPARAM_STREAMINFO "MHI_EXT_SETPARAM_STREAMINFO"
#define MHI_EXT_QUERY_STREAMINFO    "MHI_EXT_QUERY_STREAMINFO"
