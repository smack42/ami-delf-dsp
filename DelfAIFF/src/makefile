# Makefile

.PHONY : all
.PHONY : clean

# System configuration
SHELL = sh

# vc is in vbcc:bin which is in the path.
CC = vc
#vb uses the vbrowse message browser from vbcc distribution.
#CC = vb

# The object files
OBJ = DelfAIFF.o \
	PCM.o \
	ADPCM4.o

# just turn off codegen to compile some ppc stuff...
CodeGen = -g

#Optimization
OPT = -O0


DelfAIFF: $(OBJ)
	$(CC) -+ -o DelfAIFF $(OBJ) -lmieee -lbgui -ldebug -ldelfina -lasyncio -lreqtools

all: clean DelfAIFF

clean:
	delete $(OBJ)
	
DelfAIFF.o: DelfAIFF.c 
	$(CC) -+ -o DelfAIFF.o $(CodeGen) $(OPT) -c DelfAIFF.c
	
PCM.o:  PCM.a56
	a56 PCM.a56 object _DSP56K_PCM nolist

ADPCM4.o:  ADPCM4.a56
	a56 ADPCM4.a56 object _DSP56K_ADPCM4 nolist
	