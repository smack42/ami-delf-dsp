# about

These are some programs that I developed and released in the years 1999 to 2003. They are presented here in this Git repository just for easier browsing and searching of the source code. The content is identical to the packages released on Aminet back in the days.

The target platform of these programs is an Amiga computer expanded with a Delfina DSP sound board, of which there have been several models, all equipped with a Motorola 56002 DSP.

In the early and mid '90s I did some programming on my Amiga 1200, creating some demos and tool programs in Motorola 68K assembly and C languages. In 1999 I bought a Delfina 1200 sound board directly from the manufacturer in Finland. The package included some developer docs and tools, including an adapted version of the A56 assembler. I used it to learn the Motorola 56K DSP assembly language while writing some useful programs for my favourite computer.

### DelfAIFF - AIFF sound player for Delfina DSP
My first program to use the DSP 56002 of the Delfina board.  
A command line tool for Amiga Shell that plays AIFF files, running realtime ADPCM decoding on the DSP.

### DelfMPEG - MPEG audio player for Delfina DSP
My biggest, most complex program running on the DSP 56002: a realtime MPEG-1 audio (mp3) decoder. I spent many hours of coding and testing/debugging using very limited tools, but after several months of work I had the mp3 player running! I was sooo happy to have achieved this!  
A command line tool for Amiga Shell that plays MPEG audio files (mp2 and mp3), running realtime decoding on the DSP.

### delfinampeg.device - mpeg.device for Delfina DSP
Using the DSP code from my command line program DelfMPEG, this device driver can be used with several popular audio player GUI programs. Its such a huge difference, to be able to play mp3 files in full quality with little CPU load, while your classic Amiga's 68030 or 040 CPU on its own would be too slow for realtime mp3 decoding!

### DelfScope - oscilloscope/analyzer for Delfina DSP
A realtime audio visualizer that executes the heavy calculations (FFT, taken from Motorola example code) on the DSP. 

### DelfSF - sndfile player for Delfina DSP
A command line tool for Amiga Shell that plays many audio file formats, using sndfile library (an Amiga shared library based on Erik de Castro Lopo's libsndfile) for input processing. Not much DSP code necessary here.

# links

software: Aminet
- http://aminet.net/search?query=delfina
- http://aminet.net/package/mus/play/DelfAIFF
- http://aminet.net/package/mus/play/DelfMPEG
- http://aminet.net/package/mus/play/dmdev
- http://aminet.net/package/mus/misc/DelfScope
- http://aminet.net/package/mus/play/DelfSF

hardware: Amiga computers
- https://en.wikipedia.org/wiki/Amiga
- https://amiga.resource.cx/models.html
- https://www.bigbookofamigahardware.com/bboah/CategoryList.aspx?id=1

hardware: Delfina sound boards
- https://amiga.resource.cx/exp/delfina
- https://amiga.resource.cx/exp/delfinaplus
- https://amiga.resource.cx/exp/delfinalite  (software package)
- https://amiga.resource.cx/exp/delfina1200
- https://amiga.resource.cx/exp/delfinaflipper
- http://wiki.icomp.de/wiki/Delfina  (software package)
