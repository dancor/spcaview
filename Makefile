##############################
# spcaview Makefile
##############################

INSTALLROOT=$(PWD)

CC=gcc
CPP=g++
INSTALL=install
APP_BINARY=spcaview
BIN=/usr/local/bin
#SDLLIBS= -lSDL -lpthread -lSDL_image
SDLLIBS = $(shell sdl-config --libs) 
SDLFLAGS = $(shell sdl-config --cflags)

SERVFLAGS= -O2 -DLINUX $(WARNINGS)
MATH_LIB=-lm 
SERVLIBS= $(MATH_LIB) -lpthread

#WARNINGS = -Wall \
#           -Wundef -Wpointer-arith -Wbad-function-cast \
#           -Wcast-align -Wwrite-strings -Wstrict-prototypes \
#           -Wmissing-prototypes -Wmissing-declarations \
#           -Wnested-externs -Winline -Wcast-qual -W \
#           -Wno-unused
#           -Wunused

CFLAGS = -DUSE_SDL -O2 -DLINUX $(SDLFLAGS) $(WARNINGS)
CPPFLAGS = $(CFLAGS)
SHCFLAGS=  -O2 -ffast-math -fforce-addr -fstrict-aliasing -fomit-frame-pointer
#CLIBFLAGS= -O9 -falign-functions=4 -march=athlon 
#LIB_ENCODE = libjpgenc.a
#LIB_ENCODE_OBJECTS = encoder.o huffman.o marker.o quant.o

OBJECTS= spcaview.o utils.o  tcputils.o picture.o encoder.o huffman.o marker.o quant.o avilib.o \
		dpsh.o shc.o shclib.o\
		audioin_devdsp.o SDL_audioin.o
		
OBJSERVER= server.o spcav4l.o utils.o tcputils.o pargpio.o encoder.o huffman.o marker.o quant.o

OBJCAT= spcacat.o spcav4l.o utils.o picture.o pargpio.o encoder.o huffman.o marker.o quant.o avilib.o		

# Makefile commands:
#libjpgenc:	$(LIB_ENCODE_OBJECTS)
#		ld -r $(LIB_ENCODE_OBJECTS) -o $(LIB_ENCODE)
		
all:	spcaview spcaserv spcacat

clean:
	@echo "Cleaning up directory."
	rm -f *.a *.o $(APP_BINARY) spcaserv  spcacat core *~ log errlog

# Applications:
spcaview:	$(OBJECTS)
	$(CC)	$(CFLAGS) $(OBJECTS) $(X11_LIB) $(XPM_LIB)\
		$(MATH_LIB) \
		$(SDLLIBS)\
		-o $(APP_BINARY)
	chmod 755 $(APP_BINARY)

spcaserv: $(OBJSERVER)
	gcc $(SERVFLAGS) -o spcaserv $(OBJSERVER) $(SERVLIBS)
	
spcacat: $(OBJCAT)
	gcc $(SERVFLAGS) -o spcacat $(OBJCAT) $(SERVLIBS)
	
spcaview.o: spcaview.c  jconfig.h dpsh.h utils.h SDL_audioin.h


shc.o : shc.c shc.h
	$(CC) $(SHCFLAGS) -c -o $@ $<
shclib.o: shclib.c shclib.h
	$(CC) $(SHCFLAGS) -c -o $@ $<
dpsh.o: dpsh.c  dpsh.h
	$(CC) $(SHCFLAGS) -c -o $@ $<

avilib.o: avilib.c avilib.h
		$(CC) $(SERVFLAGS) -c -o $@ $<
	
server.o:	server.c
		$(CC) $(SERVFLAGS) -c -o $@ $<
		
spcacat.o:	spcacat.c
		$(CC) $(SERVFLAGS) -c -o $@ $<
	
spcav4l.o:	spcav4l.c spcav4l.h
		$(CC) $(SERVFLAGS) -c -o $@ $<
		
utils.o:	utils.c utils.h
		$(CC) $(SERVFLAGS) -c -o $@ $<
		
picture.o:	picture.c picture.h
		$(CC) $(SERVFLAGS) -c -o $@ $<
		
pargpio.o:	pargpio.c pargpio.h
		$(CC) $(SERVFLAGS) -c -o $@ $<
		
tcputils.o:	tcputils.c tcputils.h
		$(CC) $(SERVFLAGS) -c -o $@ $<
		
encoder.o:	encoder.c encoder.h
		$(CC) $(SERVFLAGS) -c -o $@ $<
		
huffman.o:	huffman.c huffman.h
		$(CC) $(SERVFLAGS) -c -o $@ $<
		
marker.o:	marker.c marker.h
		$(CC) $(SERVFLAGS) -c -o $@ $<
		
quant.o:	quant.c quant.h
		$(CC) $(SERVFLAGS) -c -o $@ $<
		
install_spcaserv: spcaserv
	$(INSTALL) -s -m 755 -g root -o root spcaserv $(BIN) 
	
install_spcacat: spcacat	
	$(INSTALL) -s -m 755 -g root -o root spcacat $(BIN)

install_spcaview: spcaview
	 $(INSTALL) -s -m 755 -g root -o root spcaview $(BIN)
	 
install: spcaview spcaserv spcacat
	$(INSTALL) -s -m 755 -g root -o root spcaview $(BIN) 
	$(INSTALL) -s -m 755 -g root -o root spcaserv $(BIN) 
	$(INSTALL) -s -m 755 -g root -o root spcacat $(BIN) 
