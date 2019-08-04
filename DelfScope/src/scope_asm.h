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



extern void __asm ASM_prepare_plot(void);

extern void __asm ASM_pcm_plot( register __a0 UBYTE *pcmbuf,
                                register __a1 UBYTE *chipmem,
                                register __d0 LONG numsamp  );

extern void __asm ASM_fft_plot( register __a0 UWORD *fftbuf,
                                register __a1 UBYTE *chipmem,
                                register __a2 APTR rp );
