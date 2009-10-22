/****************************************************************************
#	 	spcaview: Sdl video recorder and viewer with sound.         #
#This package work with the spca5xx based webcam with the raw jpeg feature. #
#All the decoding is in user space with the help of libjpeg.                #
#.                                                                          #
# 		Copyright (C) 2003 2004 2005 Michel Xhaard                  #
#                                                                           #
# This program is free software; you can redistribute it and/or modify      #
# it under the terms of the GNU General Public License as published by      #
# the Free Software Foundation; either version 2 of the License, or         #
# (at your option) any later version.                                       #
#                                                                           #
# This program is distributed in the hope that it will be useful,           #
# but WITHOUT ANY WARRANTY; without even the implied warranty of            #
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the             #
# GNU General Public License for more details.                              #
#                                                                           #
# You should have received a copy of the GNU General Public License         #
# along with this program; if not, write to the Free Software               #
# Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA #
#                                                                           #
****************************************************************************/


#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <string.h>
#include <pthread.h>
#include <SDL/SDL.h>
#include <SDL/SDL_thread.h>
#include <SDL/SDL_audio.h>
#include <SDL/SDL_timer.h>
#include <linux/videodev.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <sys/time.h>
#include <signal.h>
#include "SDL_audioin.h"

#include "jconfig.h"
#include "avilib.h"
#include "dpsh.h"
#include "utils.h"
#include "tcputils.h"
#include "spcaframe.h"
#include "version.h"

#define WAVE_AUDIO_PCM 1
#define CHANNELS 1
#define AUDIO_SAMPLERATE		22050 // 44100 // 11025 // 8000 // 22050
#define AUDIO_SAMPLES			512 // 1024
#define AUDIO_FORMAT			AUDIO_S16
#define AUDIOSIZE 2
#define NUM_CHUNCK 64 // 256 a large ring buffer
#define MAXBUFFERSIZE (AUDIO_SAMPLES*AUDIOSIZE*NUM_CHUNCK)
#define HELPBUFFERSIZE (AUDIO_SAMPLES*AUDIOSIZE)

// INIT_BRIGHT is the initial brightness level
// BRIGHTMEAN is the goal mean brightness for the
// auto adjustment.
// SPRING_CONSTANT determines the restoring force
// for the auto adjust
// BRIGHT_WINDOW is the window around the goal mean
// in which no adjustment is to occur
#define V4L_BRIGHT_MIN 0
#define V4L_BRIGHT_MAX 50000 //65000
#define BRIGHTMEAN 128 //145
#define SPRING_CONSTANT 25
#define BRIGHTWINDOW 10


/* pictFlag should be set by the hardware or the timer */
/* call_back function need to reset the flag */
static int totmean;
int interval = 0;
volatile int pictFlag = 0;
static int Oneshoot = 1;
static int numshoot = 0;
static int modshoot = 0;
static videoOk = 0;
/* picture structure */
typedef struct pictstruct{
	unsigned char *data;
	int sizeIn;
	int width;
	int height;
	int formatIn;
	int mode;
	avi_t *out_fd;
	pthread_mutex_t mutex;
	pthread_cond_t cond;
} pictstruct;

void *waitandshoot(void* ptr);

/* sound buffer */
struct Rbuffer {
	Uint8 buffer[MAXBUFFERSIZE];
	Uint8 helpbuffer[HELPBUFFERSIZE];
	int ptread ;
	int ptwrite;
	};

Uint32 GetVideoBpp (void);

static Uint32 callback_timer (Uint32 interval)
{
	pictFlag = 1;
	//printf("TIMER ON !!!!\n");
	return interval;
}
void callback_record(void *userdata, Uint8 *stream, int len)
{
	int part1 =0;
	int part2 =0;
 	struct Rbuffer *mybuf=(struct Rbuffer*) userdata;
 	if ((mybuf->ptwrite + len) > MAXBUFFERSIZE){
 		part1=MAXBUFFERSIZE-mybuf->ptwrite;
		part2 = len-part1;
		memcpy (mybuf->buffer+mybuf->ptwrite,stream,part1);
 		memcpy (mybuf->buffer,stream+part1,part2);
 	} else {
 		memcpy(mybuf->buffer+mybuf->ptwrite,stream,len);
 	}
 	mybuf->ptwrite = (mybuf->ptwrite +len) % MAXBUFFERSIZE;
 //fprintf(stderr,"callback_record(%p,%d,%d %d)\n",userdata,mybuf->ptwrite,part1,part2);

}
void callback_play(void *userdata, Uint8 *stream, int len)
{
	struct Rbuffer *mybuf=(struct Rbuffer*) userdata;

	int ptwrite=mybuf->ptwrite ;
	int ptread =mybuf->ptread;

	int part1 =0;
	int part2 =0;
	int delta =0;
	delta = ptwrite-ptread;

		if(delta < 0){
			if (((MAXBUFFERSIZE + delta) >= len)){

	 			part1=MAXBUFFERSIZE-ptread;
				if (part1 >=len){
				part1 = len;
				part2 = 0 ;
				} else {
	 			part2 = len-part1;
				}
				//fprintf(stderr,"callback_playT part1 %d part2 %d \n",part1,part2);
			 	 memcpy (mybuf->helpbuffer,mybuf->buffer+ptread,part1);
				if (part2)
			 	    memcpy (mybuf->helpbuffer+part1,mybuf->buffer,part2);
			 	SDL_MixAudio(stream,mybuf->helpbuffer, len, SDL_MIX_MAXVOLUME);
				mybuf->ptread = (mybuf->ptread +len) % MAXBUFFERSIZE;
 				//fprintf(stderr,"callback_playT ptread %d ptwrite %d \n",ptread,ptwrite);

			 } else {
			 	//fprintf(stderr,"Waiting for dataT to Play Ptread %dPtwrite %d !! \n",ptread,ptwrite);
			}
		} else {
			if (delta >= len){
				memcpy (mybuf->helpbuffer,mybuf->buffer+ptread,len);
				SDL_MixAudio(stream,mybuf->helpbuffer, len, SDL_MIX_MAXVOLUME);
				mybuf->ptread = (mybuf->ptread +len) % MAXBUFFERSIZE;
 				//fprintf(stderr,"callback_playF ptread %d ptwrite %d \n",ptread,ptwrite);
				} else {
	 			//fprintf(stderr,"Waiting for dataF to Play Ptread %d Ptwrite %d !! \n",ptread,ptwrite);
			}
		}




}

static int clip_to(int x, int low, int high);
//void equalize (unsigned char *src, int width, int height, int format);
static int adjust_bright( struct video_picture *videopict, int fd);

static int get_pic_mean ( int width, int height, const unsigned char *buffer,
			  int is_rgb,int startx, int starty, int endx,
			  int endy );

static int setVideoPict (struct video_picture *videopict, int fd);


static int isSpcaChip (const char *BridgeName);
/* return Bridge otherwhise -1 */
static int getStreamId (const char * BridgeName);
/* return Stream_id otherwhise -1 */
static int probeSize (const char *BridgeName, int *width, int *height);
/* return 1->ok otherwhise -1 */
void
resize (unsigned char *dst,unsigned char *src, int Wd,int Hd,int Ws,int Hs) ;

static int
readFrame (avi_t *out_fd,long i,unsigned char **jpegData, int * jpegSize, struct Rbuffer *RingBuffer, int maxsound, int *audiolen,
	   int *soundbytes, int isaudio, int updown, int compress);

static void
refresh_screen (unsigned char *src, unsigned char *pict, int format, int width,
		int height,int owidth ,int oheight, int size, int autobright);

int spcaClient (char *Ip, short port,int owidth, int oheight ,int statOn);
int spcaPlay (char* inputfile, int width, int height);
int spcaGrab (char *outputfile,char fourcc[4] , const char *videodevice, int image_width,int image_height, int format, int owidth, int oheight,
	int grabMethod,int videoOn,int audioout,int videocomp,int autobright,int statOn,int decodeOn);

void *waitandshoot(void* ptr)
{ pictstruct *mypict = (pictstruct *) ptr;
	int width = mypict->width;
	int height = mypict->height;
	int format = mypict->formatIn;
	int size = mypict->sizeIn;
	int mode = mypict->mode;
	avi_t *fd = mypict->out_fd;
	int status;
while(1){
    status = pthread_mutex_lock (&mypict->mutex);
    if (status != 0) {fprintf(stderr,"Lock error!\n"); exit(-1);}
	while (pictFlag == 0) {
              /* Set predicate */
 	status = pthread_cond_wait(&mypict->cond, &mypict->mutex);
    if (status != 0) {fprintf(stderr,"Wait error!\n"); exit(-1);}
 	}
	pictFlag = 0;
	getJpegPicture(mypict->data,width,height,format,size,mode,fd);
	if (modshoot)
	 if (!(--numshoot)) Oneshoot = 0;
    status = pthread_mutex_unlock (&mypict->mutex);
    if (status != 0) {fprintf(stderr,"Unlock error!\n"); exit(-1);}
    /******* EXIT the MONITOR ************/
//printf("Condition was accept, data.value=%d\n",pictFlag);

}
    return NULL;

}
void init_callbackmessage(struct client_t* callback)
{    char key[4] ={'O','K','\0','\0'} ;
	int x = 128;
	int y = 128;
	unsigned char sleepon=0;
	unsigned char bright=0;
	unsigned char contrast =0;
	unsigned char exposure = 0;
	unsigned char colors = 0;
	unsigned char size = 0;
	unsigned char fps = 0;
	memcpy(callback->message,key,4);
	callback->x = x;
	callback->y = y;
	callback->updobright=bright;
	callback->updocontrast=contrast;
	callback->updoexposure = exposure;
	callback->updocolors = colors;
	callback->sleepon=sleepon;
	callback->updosize = size;
	callback->fps = fps;
}
/* callback spec
	updobright = 1 increase bright
	updobright = 0 nothing todo
	updobright = 2 decrease bright
	same for all updocontrast updosize
	sleepon =1 ask the server for no frame
		on wakeup sleepon is cleared server will send frame
		and client should display
	sleepon =2 ask the server to send a 100ms pulse on pin 14
		pin 14 is inverted always 1 pulse 0
*/
void reset_callbackmessage(struct client_t* callback)
{
	callback->updobright= 0;
	callback->updocontrast= 0;
	callback->updoexposure = 0;
	callback->updocolors = 0;
	callback->sleepon= 0;
	callback->updosize = 0;
	callback->fps = 0;
}
int
main (int argc, char *argv[])
{
	/* Timing value used for statistic */

	const char *videodevice = NULL;
	/* default mmap */
	int grabMethod = 1;

	int format = VIDEO_PALETTE_YUV420P;
	/******** output screen pointer ***/

	int image_width = IMAGE_WIDTH;
	int image_height = IMAGE_HEIGHT;
	int owidth = 0;
	int oheight = 0;
	/**********************************/
	/*        avi parametres          */
	char *inputfile = NULL;
	char *outputfile = NULL;
	char fourcc[4] = "MJPG";

	char *sizestring = NULL;
	int use_libjpeg = 1;
	char *separateur;
	char *mode = NULL;
	/*********************************/
	/*          Starting Flags       */
	int i;
	int videoOn = 1;
	int decodeOn =1 ;
	int statOn = 0;
	int audioout = 0;
	int videocomp = 0;
	int channel = 0;
	int norme = 0;
	int autobright = 0;
	/*********************************/
	char *AdIpPort;
	char AdIp[]= "000.000.000.000";
	unsigned short ports = 0;

	/*********************************/
	SPCASTATE funct;


	/* init default bytes per pixels for VIDEO_PALETTE_RAW_JPEG 	*/
	/* FIXME bpp is used as byte per pixel for the ouput screen	*/
	/* we need also a bpp_in for the input stream as spcaview   	*/
	/* can convert stream That will be a good idea to have 2 struct */
	/* with all global data one for input the other for output      */
	bpp = 3;
	funct = GRABBER;
	printf(" %s \n",version);
	/* check arguments */
	for (i = 1; i < argc; i++) {
		/* skip bad arguments */
		if (argv[i] == NULL || *argv[i] == 0 || *argv[i] != '-') {
			continue;
		}
		if (strcmp (argv[i], "-d") == 0) {
			if (i + 1 >= argc) {
				printf ("No parameter specified with -d, aborting.\n");
				exit (1);
			}
			videodevice = strdup (argv[i + 1]);
		}
		if (strcmp (argv[i], "-n") == 0) {
			if (i + 1 >= argc) {
				printf ("No parameter specified with -n, aborting.\n");
				exit (1);
			}
			norme = atoi (argv[i + 1]);
			if (norme < 0 || norme > 4)
			printf ("Norme should be between 0..4 Read the readme !.\n");
		}
		if (strcmp (argv[i], "-c") == 0) {
			if (i + 1 >= argc) {
				printf ("No parameter specified with -c, aborting.\n");
				exit (1);
			}
			channel = atoi (argv[i + 1]);
			if (channel < 0 || channel > 9)
			printf ("Channel should be between [0..3] || [6..9] Read the readme !.\n");
		}
		if (strcmp (argv[i], "-v") == 0) {
			videoOn = 0;

		}
		if (strcmp (argv[i], "-j") == 0) {
			decodeOn = 0;

		}
		if (strcmp (argv[i], "-t") == 0) {
			statOn = 1;

		}
		if (strcmp (argv[i], "-z") == 0) {
			videocomp = 1;

		}
		if (strcmp (argv[i], "-b") == 0) {
		  autobright = 1;
		}
		if (strcmp (argv[i], "-f") == 0) {
			if (i + 1 >= argc) {
				printf ("No parameter specified with -f, aborting.\n");
				exit (1);
			}
			mode = strdup (argv[i + 1]);

			if (strncmp (mode, "r32", 3) == 0) {
				format = VIDEO_PALETTE_RGB32;
				bpp = 4;
				snprintf (fourcc, 5, "RGB4");

			} else if (strncmp (mode, "r24", 3) == 0) {
				format = VIDEO_PALETTE_RGB24;
				bpp = 3;
				snprintf (fourcc, 5, "RGB3");
			} else if (strncmp (mode, "r16", 3) == 0) {
				format = VIDEO_PALETTE_RGB565;
				bpp = 2;
				snprintf (fourcc, 5, "RGB2");
			} else if (strncmp (mode, "yuv", 3) == 0) {
				format = VIDEO_PALETTE_YUV420P;
				bpp = 3;
				snprintf (fourcc, 5, "I420");
			} else if (strncmp (mode, "jpg", 3) == 0) {
				format = VIDEO_PALETTE_JPEG;
				bpp = 3;
				snprintf (fourcc, 5, "MJPG");
			}else {
				format = VIDEO_PALETTE_YUV420P;
				bpp = 3;
				snprintf (fourcc, 5, "I420");
			}

		}

		if (strcmp (argv[i], "-i") == 0) {
			if (i + 1 >= argc) {
				printf ("No parameter specified with -i, aborting.\n");
				exit (1);
			}
			inputfile = strdup (argv[i + 1]);
			funct = PLAYER ;
		}

		if (strcmp (argv[i], "-g") == 0) {
			/* Ask for read instead default  mmap */
			grabMethod = 0;
		}

		if (strcmp (argv[i], "-a") == 0) {
			if (i + 1 >= argc) {
				printf ("No parameter specified with -a, aborting.\n");
				exit (1);
			}
			audioout = atoi (argv[i + 1]);
			if ((audioout < 0) || (audioout > 2)) {
				audioout = 0;
			}
			printf ("audio channel %d\n", audioout);
		}

		if (strcmp (argv[i], "-o") == 0) {
			if (i + 1 >= argc) {
				printf ("No parameter specified with -o, aborting.\n");
				exit (1);
			}
			outputfile = strdup (argv[i + 1]);
		}
		/* custom ? */
		if (strcmp (argv[i], "-s") == 0) {
			if (i + 1 >= argc) {
				printf ("No parameter specified with -s, aborting.\n");
				exit (1);
			}

			sizestring = strdup (argv[i + 1]);

			image_width = strtoul (sizestring, &separateur, 10);
			if (*separateur != 'x') {
				printf ("Error in size use -s widthxheight \n");
				exit (1);
			} else {
				++separateur;
				image_height =
					strtoul (separateur, &separateur, 10);
				if (*separateur != 0)
					printf ("hmm.. dont like that!! trying this height \n");
				printf (" size width: %d height: %d \n",
					image_width, image_height);
			}
		}
		if (strcmp (argv[i], "-m") == 0) {
			if (i + 1 >= argc) {
				printf ("No parameter specified with -m, aborting.\n");
				exit (1);
			}

			sizestring = strdup (argv[i + 1]);

			owidth = strtoul (sizestring, &separateur, 10);
			if (*separateur != 'x') {
				printf ("Error in size use -m widthxheight \n");
				exit (1);
			} else {
				++separateur;
				oheight =
					strtoul (separateur, &separateur, 10);
				if (*separateur != 0)
					printf ("hmm.. dont like that!! trying this height \n");
				printf (" output size width: %d height: %d \n",
					owidth, oheight);
			}
		}
		if (strcmp (argv[i], "-p") == 0) {
			if (argv[i + 1]) interval = atoi(argv[i + 1]);
 		  	else {
 		   	 	printf ("No parameter specified with -p, aborting.\n");
 		    		exit (1);
 		  	}

 		}
		if (strcmp (argv[i], "-N") == 0) {
			if (argv[i + 1]){
			 numshoot = (atoi(argv[i + 1])); // timer works on ms
			 if (numshoot <= 0) numshoot = 1; // in case :)
			 if(!interval) interval = 1000;
			 modshoot = 1;
			 }
 		  	else {
 		   	 	printf ("No parameter specified with -p, aborting.\n");
 		    		exit (1);
 		  	}

 		}
		if (strcmp (argv[i], "-w") == 0) {
			if (i + 1 >= argc) {
				printf ("No parameter specified with -w, aborting.\n");
				exit (1);
			}
			AdIpPort = strdup (argv[i + 1]);
			if(reportip(AdIpPort,AdIp,&ports) < 0)
			printf("error in port convertion \n");
			printf ("using Server %s Port %d \n",AdIp,ports);
			funct = CLIENT ;
		}
		/* large ? */
		if (strcmp (argv[i], "-l") == 0) {
			image_width = 640;
			image_height = 480;
		}



		if (strcmp (argv[i], "-h") == 0) {
			printf ("usage: spcaview [-h -d -n -c -v -j -z -g -l -s -m -a -f -b -t -p] [-i inputfile | -o outputfile]\n");
			printf ("-h	print this message \n");
			printf ("-d	/dev/videoX       use videoX device\n");
			printf ("-n	norme 0->pal 2->secam 1->ntsc \n");
			printf ("-c	input channels 0..9 exclude 4 and 5 \n");
			printf ("-v	disable video output for raw recording \n");
			printf ("-j	disable video output and userspace decoding \n");
			printf ("-z	compress yuv420p video output with DPSH encoder\n");
			printf ("-g	use read method for grab instead mmap \n");
			printf ("-l	default 320x480   use input size 640x480 \n");
			printf ("-s	widthxheight      use specified input size \n");
			printf ("-m	widthxheight      use specified ouput size \n");
			printf ("-a	default  0 -> nosound    1-> microphone 2-> mixer output\n");
			printf ("-f	video format  default yuv  others options are r16 r24 r32 yuv jpg \n");
			printf ("-b     enable automatic brightness adjustment \n");
			printf ("-t     print statistics \n");
			printf ("-p  x  getPicture every x msec \n");
			printf ("-p x && -o getPicture every x msec and record in outfile\n");
			printf ("-w 	Address:Port read from Address xxx.xxx.xxx.xxx:Port\n");
			printf ("-N x	take a x pictures and exit if p is not set p = 1 second \n");
			exit (0);
		}
	}






switch (funct) {
	case PLAYER:
	 {
		/* that is spcaview player */
		spcaPlay (inputfile, owidth, oheight);
	}
	break;
	case GRABBER:
	{
		/* spcaview grabber */
		spcaGrab (outputfile,fourcc , videodevice, image_width,image_height, format, owidth, oheight,
			grabMethod, videoOn, audioout, videocomp, autobright, statOn, decodeOn);
	}
	break;
	case CLIENT:
	{
		spcaClient(AdIp,ports,owidth,oheight, statOn);
	}



}

exit (0);
}
int readjpeg(int sock, unsigned char **buf,struct frame_t *headerframe,struct client_t *message,int statOn)
{

	int byteread,bytewrite;

	bytewrite = write_sock(sock,(unsigned char*)message,sizeof(struct client_t));
	// is sleeping ?
	if ((byteread= read_sock(sock,(unsigned char*)headerframe,sizeof(struct frame_t))) < 0){
	printf("Seem server is gone !! try later \n");
	goto error;
	}

	if(statOn)
		printf (" key %s nb %d width %d height %d times %dms size %d \n",headerframe->header,
		headerframe->nbframe,headerframe->w,headerframe->h,headerframe->deltatimes,headerframe->size);
	if(headerframe->size && !headerframe->wakeup){
	//if(headerframe->size){
			*buf=(unsigned char*) realloc(*buf,headerframe->size);
			if((byteread = read_sock(sock,*buf,headerframe->size)) < 0){
			printf("Seem server is gone !! try later \n");
			goto error;}
		}
		//printf("buf read %d \n",byteread);
	if(headerframe->acknowledge)
			reset_callbackmessage(message);
		usleep(5000);
	return ((headerframe->wakeup)?0:(headerframe->size));
	//return (headerframe->size);
error:
return -1;
}
void init_sdlall(void)
{	/* Initialize defaults, Video and Audio */
	if ((SDL_Init (SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_AUDIO | SDL_INIT_EVENTTHREAD) == -1)) {
		printf ("Could not initialize SDL: %s.\n", SDL_GetError ());
		exit (-1);
	}
	videoOk =1;
	atexit (SDL_Quit);
}
void init_sdlvideo(void)
{	/* Initialize defaults, Video and Audio */
	if ((SDL_InitSubSystem (SDL_INIT_VIDEO ) == -1)) {
		printf ("Could not initialize SDL: %s.\n", SDL_GetError ());
		exit (-1);
	}
	videoOk = 1;
}
void close_sdlvideo(void)
{     SDL_QuitSubSystem (SDL_INIT_VIDEO ) ;
	videoOk = 0;
}
int spcaClient (char *Ip, short port,int owidth, int oheight, int statOn)
{
	struct frame_t *headerframe;
	struct client_t *messcallback;
	unsigned char *buf = NULL;
	int width,height;
	int jpegsize;
	int sock_client;
	int run = 1;
	int quit =1;
	int keypressed  =0;
	int bpp = 3;
	SDL_Surface *pscreen;
	SDL_Event sdlevent;
	unsigned char* p= NULL;
	unsigned char *picture = NULL;
	struct tm *tdate;
	time_t curdate;
	char titre[21];
	init_sdlall();
	sock_client = open_clientsock(Ip,port);
	headerframe=(struct frame_t*)malloc(sizeof(struct frame_t));
	messcallback=(struct client_t*)malloc(sizeof(struct client_t));
	init_callbackmessage(messcallback);
	if ((jpegsize = readjpeg(sock_client,&buf,headerframe,messcallback,statOn)) < 0){
	printf("got size = 0 \n");
		goto error;
		}
	width = headerframe->w;
	height = headerframe->h;
	if(!owidth || !oheight){
		owidth	= width;
		oheight	= height;
		}
	if(videoOk) {
	pscreen = SDL_SetVideoMode (owidth, oheight, bpp * 8,
					  SDL_DOUBLEBUF | SDL_SWSURFACE);
	p=pscreen->pixels;
	}
	do{

	if((jpegsize = readjpeg(sock_client,&buf,headerframe,messcallback,statOn)) < 0){
	 // printf(" No size !!! exit fatal \n");
		goto error;
		}
/* mode sleep off */
	if(!jpegsize && videoOk)
		close_sdlvideo();
	if( !videoOk && jpegsize){
		init_sdlvideo();
		pscreen = SDL_SetVideoMode (owidth, oheight, bpp * 8,
					  SDL_DOUBLEBUF | SDL_SWSURFACE);
		p=pscreen->pixels;
	}

	curdate = (time_t) (headerframe->seqtimes / 1000);
	tdate = localtime(&curdate);
	snprintf (titre,21,"%02d/%02d/%04d-%02d:%02d:%02d\0",
	    tdate->tm_mday, tdate->tm_mon + 1, tdate->tm_year + 1900,
	    tdate->tm_hour, tdate->tm_min, tdate->tm_sec);
	if(jpegsize && videoOk){
		jpeg_decode(&picture,buf,&width,&height);
		resize (p,picture,owidth,oheight,width,height) ;
	SDL_WM_SetCaption (titre, NULL);
	SDL_Flip (pscreen);
	}
	printf("bright %05d contrast %05d \r",headerframe->bright,headerframe->contrast);
	fflush (stdout);
	switch (run){
	case 1:
	if (SDL_PollEvent (&sdlevent) == 1) {
				switch (sdlevent.type) {
					case SDL_KEYDOWN:
						switch (sdlevent.key.keysym.sym) {
							case SDLK_s:
								//getPicture(buf,jpegsize);
								getJpegPicture(buf,width,height,
									VIDEO_PALETTE_JPEG,jpegsize,PICTURE,NULL);
								break;
							case SDLK_w:
							messcallback->updocontrast =2;
							break;
							case SDLK_x:
							messcallback->updocontrast =1;
							break;
							case SDLK_b:
							messcallback->updobright=1;
							break;
							case SDLK_n:
							messcallback->updobright=2;
							break;
							case SDLK_l:
							messcallback->sleepon=1;
							break;
							case SDLK_c:
							messcallback->sleepon=2;
							break;
							case SDLK_j:
							messcallback->updoexposure=1;
							break;
							case SDLK_d:
							messcallback->updosize=1; //workaround change quality index
							break;
							case SDLK_f:
							messcallback->updosize=2; //workaround change quality index
							break;
							case SDLK_g:
							messcallback->fps=1; // decrease time_interval
							break;
							case SDLK_h:
							messcallback->fps=2; // increase time interval
							break;
							case SDLK_UP:
							if(messcallback->y -1 > 0)
								messcallback->y--;
								keypressed = SDLK_UP ; run = 2;
							break;
							case SDLK_DOWN:
							if(messcallback->y +1 < 256)
								messcallback->y++;
								keypressed = SDLK_DOWN; run = 2;
							break;
							case SDLK_RIGHT:
							if(messcallback->x -1 > 0)
								messcallback->x--;
								keypressed = SDLK_RIGHT; run = 2;
							break;
							case SDLK_LEFT:
							if(messcallback->x +1 < 256)
								messcallback->x++;
								keypressed = SDLK_LEFT; run = 2;
							break;
							case SDLK_SPACE:
								run = 0;
								break;
							case SDLK_q:
								quit =0;
								break;
						}
						break;
					case SDL_QUIT:
						quit = 0;
						break;
				}
	} //end Poll event
	break;
	case 0:
	if (SDL_WaitEvent (&sdlevent) == 1) {
				switch (sdlevent.type) {
					case SDL_KEYDOWN:
						switch (sdlevent.key.keysym.sym) {
							case SDLK_s:
								//getPicture(buf,jpegsize);
								break;
							case SDLK_SPACE:
								run = 1;
								break;
							case SDLK_q:
								quit =0;
								break;
						}
						break;
					case SDL_QUIT:
						quit = 0;
						break;
				}
	} //end wait event
	break;
	case 2:{
	if (SDL_PollEvent (&sdlevent) == 1) {
				switch (sdlevent.type) {
					case SDL_KEYDOWN:
					keypressed = 0;
					run = 1;
						break;
				} //end event type poll 2
	}
	switch (keypressed){
	case SDLK_UP:
		if(messcallback->y -1 > 0)
			messcallback->y--;
		break;
	case SDLK_DOWN:
		if(messcallback->y +1 < 256)
			messcallback->y++;
		break;
	case SDLK_RIGHT:
		if(messcallback->x -1 > 0)
		messcallback->x--;
		break;
	case SDLK_LEFT:
		if(messcallback->x +1 < 256)
		messcallback->x++;
		break;
	default:
	break;
	}
	} // end case 2
	break;
	}
        }while(quit);
error:
if(picture){
free(picture);
picture = NULL;
}
close_sock(sock_client);
free(buf);
free(messcallback);
free(headerframe);
SDL_Quit ();
return 0;
}

int spcaPlay (char* inputfile, int width, int height){

	/* Timing value used for statistic */
	Uint32 synctime = 0;
	Uint32 intime = 0;
	Uint32 pictime = 0;
	int format = VIDEO_PALETTE_YUV420P;
	/******** output screen pointer ***/
	SDL_Surface *pscreen;
	SDL_Event sdlevent;
	unsigned char *p = NULL;
	unsigned char *pp = NULL;
	int image_width = IMAGE_WIDTH;
	int image_height = IMAGE_HEIGHT;
	int owidth = width;
	int oheight = height;
	/**********************************/
	/*        avi parametres          */
	avi_t *out_fd;
	char *compressor;
	double framerate;
	long audiorate;
	int audiobits;
	int audioformat;
	unsigned char *tmp = NULL;	// don't forget the Till lesson
	unsigned char *dpshDest = NULL;
	u_int8_t *jpegData = NULL;	// Till lesson a good pointer is a pointer NULL
	int jpegSize;
	int framecount = 0;
	/*********************************/
	/*       file input output       */
	char *outputfile = NULL;
	static char Picture[80];
	char fourcc[4] = "MJPG";
	FILE *foutpict;
	/*********************************/
	/*          Starting Flags       */
	int run = 1;
	int quit = 1;
	int initAudio = 0; //flag start audio
	int videocomp = 0; //is compression dsph
	int frame_size = 0;
	int len = 0;
	int fullsize = 0;
	/*********************************/
	/* data for SDL_audioin && SDL_audio */
	SDL_AudioSpec spec, result;
  	SDL_AudioSpec expect;
  	struct Rbuffer RingBuffer;
  	int retry = 100;

  	int err = 0;
	int bytes_per_read =0;
	int audioout = 0;
	int audiolength = 0;
	int maxsound = 0;
	/*********************************/
	/* misc index */
	int i,j,k;
	int testbpp = 16;
	RingBuffer.ptread =0;
  	RingBuffer.ptwrite =0;
	/* that a is spcaview player */
	printf ("Initializing SDL.\n");
	/* Initialize defaults, Video and Audio */
	if ((SDL_Init (SDL_INIT_VIDEO | SDL_INIT_TIMER) == -1)) {
		printf ("Could not initialize SDL: %s.\n", SDL_GetError ());
		exit (-1);
	}
	//bitperpix = GetVideoBpp();

	/* Clean up on exit */
	atexit (SDL_Quit);

	printf ("SDL initialized.\n");
		printf ("Using %s avifile \n", inputfile);
		if ((out_fd = AVI_open_input_file (inputfile, 1)) == NULL) {
			printf ("cannot open input file ? \n");
			exit (1);
		}
		/* get the parameters from the file */

		framecount = (int) AVI_video_frames (out_fd);

		image_width = (int) AVI_video_width (out_fd);
		image_height = (int) AVI_video_height (out_fd);
		if(!owidth || !oheight){
		owidth	= image_width;
		oheight	= image_height;
		}
		printf ("frame: %d  width: %d height: %d \n",
			framecount, image_width, image_height);
		framerate = AVI_frame_rate (out_fd);
		printf (" framerate : %f \n", framerate);
		audiobits = AVI_audio_bits (out_fd);
		audiorate = AVI_audio_rate (out_fd);
		audioformat = AVI_audio_format (out_fd);
		printf ("audio rate: %d audio bits: %d audio format: %d\n",
			audiorate, audiobits, audioformat);

		compressor = AVI_video_compressor (out_fd);
		printf (" codec: %s \n", compressor);

		if (strncmp (compressor, "RGB4", 4) == 0) {
			format = VIDEO_PALETTE_RGB32;
			bpp = 4;
		} else if (strncmp (compressor, "RGB3", 4) == 0) {
			format = VIDEO_PALETTE_RGB24;
			bpp = 3;
		} else if (strncmp (compressor, "RGB2", 4) == 0) {
			format = VIDEO_PALETTE_RGB565;
			bpp = 2;
		} else if (strncmp (compressor, "I420", 4) == 0) {
			format = VIDEO_PALETTE_YUV420P;
			bpp = 3;
		} else if (strncmp (compressor, "MJPG", 4) == 0) {
			format = VIDEO_PALETTE_JPEG;
			bpp = 3;
		} else if (strncmp (compressor, "DPSH", 4) == 0) {
			format = VIDEO_PALETTE_YUV420P;
			videocomp = 1;
			frame_size = (image_width*image_height*3) >> 1;
			bpp = 3;
		} else {
			printf ("Cannot play this file try Mplayer or Xine \n");
			AVI_close (out_fd);
			exit (1);
		}
		/* Init the player */
		printf ("bpp %d format %d\n", bpp, format);
		//if (format == VIDEO_PALETTE_RAW_JPEG)
		//	init_libjpeg_decoder (image_width, image_height);
		//pscreen =
			//SDL_SetVideoMode (image_width, image_height, bpp * 8,
			//
			//		  SDL_DOUBLEBUF | SDL_SWSURFACE);
/* set the player screen always in 24 bits mode */
		pscreen =
			SDL_SetVideoMode (owidth, oheight, testbpp,
					  SDL_DOUBLEBUF | SDL_SWSURFACE);
		if (pscreen == NULL) {
			printf ("Couldn't set %d*%dx%d video mode: %s\n",
				image_width, image_height, bpp * 8,
				SDL_GetError ());
			exit (1);
		}

		/* No Cursor in Player */
		SDL_ShowCursor (0);
		SDL_WM_SetCaption ("Spcaview Player ", NULL);

		p = pscreen->pixels;
		synctime = 1000 / framerate;
		printf ("Frame time %d ms\n", synctime);
		if (AVI_audio_channels (out_fd) == 1) {

			expect.format=AUDIO_FORMAT;
  			expect.freq=audiorate;
  			expect.callback=callback_play;
  			expect.samples=AUDIO_SAMPLES;
  			expect.channels=CHANNELS;
  			expect.userdata=(void*)&RingBuffer;
			if(SDL_OpenAudio(&expect,NULL)<0)
  				{
    				fprintf(stderr,"Couldn't open audio output device: %s\n",SDL_GetError());
    				SDL_CloseAudio();
    				SDL_Quit();
  				}
			/* Init the sound player */
			bytes_per_read = 2 * audiorate / framerate;
			maxsound = audiolength = AVI_audio_bytes (out_fd);
			//printf ("bytes to read %d \n",bytes_per_read);
			initAudio =1;
			/* mute if not a raw PCM file */
			if (audioformat == 1)
				audioout = 1;
		}
		i = 0;

		do {
			if (run) {
				intime = SDL_GetTicks ();
				/* read frame read video and audio */
				err =readFrame (out_fd, i,&jpegData,&jpegSize,&RingBuffer, maxsound,
					   &audiolength, &bytes_per_read,
					   audioout, 1,videocomp);

				//printf("readframe %d audiolength %d position errors %d \n",i,audiolength,err);

				if (initAudio ){
					initAudio = 0;
					SDL_PauseAudio(0); // start play
				}

refresh_screen ( jpegData, p, format, image_width,image_height,owidth,oheight,jpegSize,0);
				SDL_Flip (pscreen);	//switch the buffer and update screen
				pictime = SDL_GetTicks () - intime;
				//printf(" get pictimes %d expected %d  \n",pictime,synctime);
				if (pictime < synctime)
					SDL_Delay (synctime - pictime);
				i++;
			}
			fflush (stdout);
			switch (run){
			case 1:
			if (SDL_PollEvent (&sdlevent) == 1) {
				switch (sdlevent.type) {
					case SDL_KEYDOWN:
						switch (sdlevent.key.keysym.sym) {

							case SDLK_DOWN:
								SDL_PauseAudio(1); // stop play
								run = 0;
								break;

							case SDLK_s:
								if (run == 0) {
									getJpegPicture(jpegData,image_width,image_height,
										format,jpegSize,PICTURE,NULL);
								}
								break;
							case SDLK_SPACE:
							if (fullsize ==
								    0) {
									pscreen = SDL_SetVideoMode (owidth, oheight, testbpp,
									SDL_DOUBLEBUF | SDL_FULLSCREEN | SDL_SWSURFACE);
									fullsize = 1;
								} else {
									pscreen = SDL_SetVideoMode (owidth, oheight, testbpp,
									SDL_DOUBLEBUF | SDL_SWSURFACE);
									fullsize = 0;
								}
								if (pscreen ==
								    NULL) {
									printf ("Couldn't set %d*%dx%d video mode: %s\n", owidth, oheight, testbpp, SDL_GetError ());
									exit (1);
								}

								SDL_ShowCursor
									(0);


								break;
							default:
								printf ("\nStop asked\n");
								quit = 0;
								break;
						}
						break;
					case SDL_QUIT:
						quit = 0;
						break;
				}
			} //end if poll
			break;
			case 0:
			if (SDL_WaitEvent (&sdlevent) == 1) {
				switch (sdlevent.type) {
					case SDL_KEYDOWN:
						switch (sdlevent.key.keysym.sym) {
							case SDLK_UP:
								initAudio = 1;
								run = 1;
								break;
							case SDLK_RIGHT:
								if (i <
								    (framecount
								     - 1)) {
									i++;

									err = readFrame
										(out_fd, i,
										&jpegData,&jpegSize,
										 &RingBuffer,
										 maxsound,
										 &audiolength,
										 &bytes_per_read,
										 audioout,
										 1,videocomp);
									printf ("scanning up frame %d\n", i);
									refresh_screen
										(jpegData,
										 p,
										 format,
										 image_width,
										 image_height,
										 owidth,
										 oheight,
										 jpegSize,0);

									SDL_Flip (pscreen);	//switch the buffer and update screen
								}
								break;
							case SDLK_LEFT:
								if (i > 1) {
									i--;

									err = readFrame
										(out_fd, i,
										&jpegData,&jpegSize,
										 &RingBuffer,
										 maxsound,
										 &audiolength,
										 &bytes_per_read,
										 audioout,
										 0,videocomp);
									printf ("scanning down frame %d\n", i);
									refresh_screen
										(jpegData,
										 p,
										 format,
										 image_width,
										 image_height,
										 owidth,
										 oheight,
										 jpegSize,0);

									SDL_Flip (pscreen);	//switch the buffer and update screen
								}
								break;
							case SDLK_s:
								if (run == 0) {
									getJpegPicture(jpegData,image_width,image_height,
										format,jpegSize,PICTURE,NULL);
								}
								break;
							case SDLK_SPACE:

								if (fullsize ==
								    0) {
									pscreen = SDL_SetVideoMode (owidth, oheight, testbpp, SDL_DOUBLEBUF | SDL_FULLSCREEN | SDL_SWSURFACE);
									fullsize = 1;
								} else {
									pscreen = SDL_SetVideoMode (owidth, oheight, testbpp, SDL_DOUBLEBUF | SDL_SWSURFACE);
									fullsize = 0;
								}
								if (pscreen ==
								    NULL) {
									printf ("Couldn't set %d*%dx%d video mode: %s\n", owidth, oheight, testbpp, SDL_GetError ());
									exit (1);
								}

								SDL_ShowCursor(0);
								refresh_screen(jpegData,
										 p,
										 format,
										 image_width,
										 image_height,
										 owidth,
										 oheight,
										 jpegSize,0);

									SDL_Flip (pscreen);	//switch the buffer and update screen
								break;
							default:
								printf ("\nStop asked\n");
								quit = 0;
								break;
						}
						break;
					case SDL_QUIT:
						quit = 0;
						break;
				}
			} //end if wait
			break;
			} //end switch run
		}
		while (i < framecount && quit == 1);
		free (jpegData);
		//close_libjpeg_decoder ();
		if (audioout) {

		while ((RingBuffer.ptread != RingBuffer.ptwrite) && retry--){
 			SDL_Delay(10);
  			//fprintf(stderr,"Waiting .. stop the player %d\n",retry);
 			}
			 SDL_CloseAudio(); // stop play

			audioout = 0;

		}
		AVI_close (out_fd);
		SDL_Quit ();
return 0;
}
static void spcaPrintParam (int fd,struct video_param *videoparam);

static void spcaSetAutoExpo(int fd, struct video_param * videoparam)
{
	videoparam->chg_para = CHGABRIGHT;
	videoparam->autobright = !videoparam->autobright;
	if(ioctl(fd,SPCASVIDIOPARAM, videoparam) == -1){
		printf ("autobright error !!\n");
	} else
		spcaPrintParam (fd,videoparam);

}

static void spcaSetLightFrequency(int fd, struct video_param * videoparam, int light_freq)
{
	videoparam->chg_para = CHGLIGHTFREQ;
	videoparam->light_freq = light_freq;
	if(ioctl(fd,SPCASVIDIOPARAM, videoparam) == -1){
		printf ("light freqency error !!\n");
	} else
		spcaPrintParam (fd,videoparam);
}

static int spcaSwitchLightFrequency(int fd, struct video_param * videoparam)
{
	int light_freq;
        if(ioctl(fd,SPCAGVIDIOPARAM, videoparam) == -1){
		printf ("wrong spca5xx device\n");
	} else {
	   light_freq = videoparam->light_freq;
	   if(light_freq == 50)
	      light_freq +=10;
	   else if(light_freq == 60)
              light_freq = 0;
	   else if(light_freq == 0)
	      light_freq = 50;
	   if(light_freq)
	    printf ("Current light frequency filter: %d Hz\n", light_freq);
	   else
	    printf ("Current light frequency filter: NoFliker\n");
	   spcaSetLightFrequency(fd,videoparam,light_freq);
	}
	return light_freq;
}


static void spcaSetTimeInterval(int fd, struct video_param *videoparam, unsigned short time)
{
	if (time < 1000) {
	videoparam->chg_para = CHGTINTER;
	videoparam->time_interval = time;
	if(ioctl(fd,SPCASVIDIOPARAM, videoparam) == -1){
		printf ("frame_times error !!\n");
	} else
		spcaPrintParam (fd,videoparam);
	}

}
static void spcaSetQuality(int fd, struct video_param *videoparam, unsigned char index)
{
	if (index < 6) {
	videoparam->chg_para = CHGQUALITY;
	videoparam->quality = index;
	if(ioctl(fd,SPCASVIDIOPARAM, videoparam) == -1){
		printf ("quality error !!\n");
	} else
		spcaPrintParam (fd,videoparam);
	}
}
static void spcaPrintParam (int fd, struct video_param *videoparam)
{
	if(ioctl(fd,SPCAGVIDIOPARAM, videoparam) == -1){
		printf ("wrong spca5xx device\n");
	} else
		printf("quality %d autoexpo %d Timeframe %d lightfreq %d\n",
			 videoparam->quality,videoparam->autobright,videoparam->time_interval, videoparam->light_freq);
}
static void qualityUp(int fd,struct video_param *videoparam)
{
	unsigned char index = videoparam->quality;
	index+=1;
	spcaSetQuality(fd,videoparam,index);
}
static void qualityDown(int fd,struct video_param *videoparam)
{
	unsigned char index = videoparam->quality;
	if(index > 0) index--;
	spcaSetQuality(fd,videoparam,index);
}
static void timeUp(int fd, struct video_param *videoparam)
{
	unsigned short index = videoparam->time_interval;
	index+=10;
	spcaSetTimeInterval(fd,videoparam,index);
}
static void timeDown(int fd, struct video_param *videoparam)
{
	unsigned short index = videoparam->time_interval;
	if(index > 0) index -=10;
	spcaSetTimeInterval(fd,videoparam,index);
}
int spcaGrab (char *outputfile,char fourcc[4] , const char *videodevice, int image_width,int image_height, int format, int owidth, int oheight,
	int grabMethod,int videoOn,int audioout,int videocomp,int autobright,int statOn,int decodeOn) {


	/* Timing value used for statistic */
	Uint32 total_decode_time = 0;
	Uint32 time = 0;
	Uint32 prevtime = 0;
	Uint32 synctime = 0;
	Uint32 decodetime = 0;
	double average_decode_time = 0;
	Uint32 intime = 0;
	Uint32 pictime = 0;
	Uint32 delaytime = 0;
	Uint32 compresstime = 0;
	/**********************************/
	/*        video structures       */
	int fd;
	/* default mmap */
	int i, j, k, nframes = 2000, f, status;
	struct video_mmap vmmap;
	struct video_capability videocap;
	int mmapsize;
	struct video_mbuf videombuf;
	struct video_picture videopict;
	struct video_window videowin;
	struct video_channel videochan;
	struct video_param videoparam;
	int norme = 0;
	int channel = 0;
	/******** output screen pointer ***/
	SDL_Surface *pscreen;
	SDL_Event sdlevent;
	unsigned char *p = NULL;
	unsigned char *pp = NULL;
	/**********************************/
	/*        avi parametres          */
	avi_t *out_fd;
	unsigned char *pFramebuffer;
	unsigned char *tmp = NULL;	// don't forget the Till lesson
	unsigned char *dpshDest = NULL;
	u_int8_t *jpegData = NULL;	// Till lesson a good pointer is a pointer NULL
	int jpegSize;
	int ff;
	int framecount = 0;
	/*********************************/
	/*          Starting Flags       */
	int run = 1;
	int quit = 1;


	int initAudio = 0; //flag start audio
	int method = 1;
	int streamid ;
	int isVideoChannel = 1;
	int frame_size = 0;
	int len = 0;


	/*********************************/
	/* data for SDL_audioin && SDL_audio */
	SDL_AudioSpec spec, result;
  	SDL_AudioSpec expect;
  	struct Rbuffer RingBuffer;
  	int retry = 100;
  	int ptread ;
	int ptwrite ;
  	int err = 0;
	int bytes_per_read =0;
	int testbpp=16;
	/*********************************/
	pictstruct mypict ;
	pthread_t waitandshoot_id;
	int wstatus ;
	/*********************************/
	RingBuffer.ptread =0;
  	RingBuffer.ptwrite =0;
		/* spcaview grabber */
		printf ("Initializing SDL.\n");

	/* Initialize defaults, Video and Audio */
	if ((SDL_Init (SDL_INIT_VIDEO | SDL_INIT_TIMER) == -1)) {
		printf ("Could not initialize SDL: %s.\n", SDL_GetError ());
		exit (-1);
	}


	/* Clean up on exit */
	atexit (SDL_Quit);
	if(!owidth || !oheight){
		owidth	= image_width;
		oheight	= image_height;
		}
	printf ("SDL initialized.\n");
		/* validate parameters */
		printf ("bpp %d format %d\n", bpp, format);
		if(!videoOn) {
		/* acquire raw data force palette to raw */
		printf ("VideoOn = 0\n");
			snprintf (fourcc, 5, "RAWD");
			format = VIDEO_PALETTE_RAW_JPEG;
			}
		if(videocomp) {
		/* acquire yuv420p and compress diff pixels static huffman */
			snprintf (fourcc, 5, "DPSH");
			format = VIDEO_PALETTE_YUV420P;
			}
		if (videodevice == NULL || *videodevice == 0) {
			videodevice = "/dev/video0";
		}
		printf ("Using video device %s.\n", videodevice);
		printf ("Initializing v4l.\n");

		//v4l init
		if ((fd = open (videodevice, O_RDWR)) == -1) {
			perror ("ERROR opening V4L interface \n");
			exit (1);
		}
		printf("**************** PROBING CAMERA *********************\n");
		if (ioctl (fd, VIDIOCGCAP, &videocap) == -1) {
			printf ("wrong device\n");
			exit (1);
		}

		printf("Camera found: %s \n",videocap.name);

		if (ioctl (fd, VIDIOCGCHAN, &videochan) == -1) {
			printf ("Hmm did not support Video_channel\n");
			isVideoChannel = 0;
		}
		if (isVideoChannel){
			videochan.norm = norme;
			videochan.channel = channel;
			if (ioctl (fd, VIDIOCSCHAN, &videochan) == -1) {
				printf ("ERROR setting channel and norme \n");
				exit (1);
			}
			/************ just to be sure *************/
			if (ioctl (fd, VIDIOCGCHAN, &videochan) == -1) {
				printf ("wrong device\n");
				exit (1);
			}
			printf("Bridge found: %s \n",videochan.name);
			streamid = getStreamId (videochan.name);

			if (streamid >= 0){
				printf("StreamId: %s Camera\n",Plist[streamid].name);
				/* look a spca5xx webcam try to set the video param struct */
				spcaPrintParam (fd,&videoparam);
			} else {
				printf("StreamId: %d Unknow Camera\n",streamid);
			}
			/* test jpeg capability if not and jpeg ask without raw data
			set default format to YUVP */
			if ((format == VIDEO_PALETTE_RAW_JPEG || format == VIDEO_PALETTE_JPEG )&& streamid != JPEG && videoOn) {
				printf ("Camera unable to stream in JPEG mode switch to YUV420P\n");
				format = VIDEO_PALETTE_YUV420P;
			}
			if(probeSize(videochan.name,&image_width,&image_height) < 0)
				printf("unable to probe size !!\n");
			}
		printf("*****************************************************\n");
		/* Init grab method mmap */
		if (grabMethod) {
			printf(" grabbing method default MMAP asked \n");
			// MMAP VIDEO acquisition
			memset (&videombuf, 0, sizeof (videombuf));
			if (ioctl (fd, VIDIOCGMBUF, &videombuf) < 0) {
				perror (" init VIDIOCGMBUF FAILED\n");
			}
			printf ("VIDIOCGMBUF size %d  frames %d  offets[0]=%d offsets[1]=%d\n", videombuf.size, videombuf.frames, videombuf.offsets[0], videombuf.offsets[1]);

			pFramebuffer =
				(unsigned char *) mmap (0, videombuf.size,
							PROT_READ | PROT_WRITE,
							MAP_SHARED, fd, 0);
			mmapsize = videombuf.size;
			vmmap.height = image_height;
			vmmap.width = image_width;
			vmmap.format = format;
			for (f = 0; f < videombuf.frames; f++) {
				vmmap.frame = f;
				if (ioctl (fd, VIDIOCMCAPTURE, &vmmap)) {
					perror ("cmcapture");
				}
			}
			vmmap.frame = 0;
			/* Compute the estimate frame size we expect that jpeg compress factor 10
			if ((format == VIDEO_PALETTE_RAW_JPEG) && videoOn)
				frame_size =
					videombuf.size / (10 *
							  videombuf.frames);
			else
				frame_size = videombuf.size / videombuf.frames;
			*/
		} else {
			/* read method */
			printf(" grabbing method READ asked \n");
			if (ioctl (fd, VIDIOCGWIN, &videowin) < 0)
				perror ("VIDIOCGWIN failed \n");
			videowin.height = image_height;
			videowin.width = image_width;
			if (ioctl (fd, VIDIOCSWIN, &videowin) < 0)
				perror ("VIDIOCSWIN failed \n");
			printf ("VIDIOCGWIN height %d  width %d \n",
				videowin.height, videowin.width);

		}
		switch (format) {
				case VIDEO_PALETTE_JPEG:{
					frame_size = image_width * image_height;
				}
				break;
				case VIDEO_PALETTE_RAW_JPEG:{
					frame_size = image_width *
							image_height * 3;
					}
					break;
				case VIDEO_PALETTE_YUV420P:{
						frame_size =
							(image_width *
							 image_height * 3) >> 1;
					}
					break;
				case VIDEO_PALETTE_RGB565:
				case VIDEO_PALETTE_RGB24:
				case VIDEO_PALETTE_RGB32:{
						frame_size =
							image_width *
							image_height * bpp;
					}
					break;
				default:
					break;

			}
		/* struct video_picture VIDIOCGPICT VIDIOCSPICT */
		if (ioctl (fd, VIDIOCGPICT, &videopict) < 0) {
			perror ("Couldnt get videopict params with VIDIOCGPICT\n");
		}
		printf ("VIDIOCGPICT\n");
		printf("brightnes=%d hue=%d color=%d contrast=%d whiteness=%d \n" ,
		 videopict.brightness, videopict.hue, videopict.colour, videopict.contrast, videopict.whiteness);
		printf("depth=%d palette=%d\n", videopict.depth, videopict.palette);

		videopict.palette = format;
		videopict.depth = bpp * 8;
		//videopict.brightness = INIT_BRIGHT;
		sleep (1);
		setVideoPict (&videopict, fd);


		/*
		 * Initialize the display
		 */
if ( decodeOn && videoOn )
			{	/* Display data */
		pscreen =
			SDL_SetVideoMode (owidth, oheight, testbpp,
					  SDL_SWSURFACE);
		if (pscreen == NULL) {
			printf ("Couldn't set %d*%dx%d video mode: %s\n",
				owidth, oheight,3 * 8,
				SDL_GetError ());
			exit (1);
		}
//		SDL_WM_SetCaption ("Spcaview Grabber ", NULL);
SDL_WM_SetCaption (videocap.name, NULL);
}
		printf ("\n");
		time = SDL_GetTicks ();
		prevtime = time;

		/* If need open the avi file and alloc jpegbuffer */
		if (outputfile) {
			jpegData = malloc (frame_size);
			dpshDest = malloc(frame_size);
			if ((out_fd =
			     AVI_open_output_file (outputfile)) == NULL) {
				printf ("cannot open write file ? \n");
				exit (1);
			}
			if(interval){
			/* picture goes in avi */
			AVI_set_video (out_fd, image_width, image_height, 1,
				       "MJPG");
			mypict.mode = AVIPICT;
			mypict.out_fd = out_fd;
			} else {
			AVI_set_video (out_fd, image_width, image_height, 20,
				       fourcc);
				       }
			if (audioout && !interval) {
			/* init the sound recorder if no picture*/
			spec.format=AUDIO_FORMAT;
  			spec.freq=AUDIO_SAMPLERATE;
  			spec.callback=callback_record;
  			spec.samples=AUDIO_SAMPLES;
  			spec.channels=1;
  			spec.userdata=(void*)&RingBuffer;
			if(SDL_InitAudioIn()<0)
  				{
   				 fprintf(stderr,"Couldn't initialize SDL_AudioIn: %s\n",SDL_GetError());
    				 SDL_Quit();
   				 return(2);
  				}
			 if(SDL_OpenAudioIn(&spec,&result)<0)
  				{
   				 fprintf(stderr,"Couldn't open audio input device: %s\n",SDL_GetError());
   				 SDL_CloseAudioIn();
    				 SDL_Quit();
  				}
			AVI_set_audio (out_fd, spec.channels, spec.freq,
					       16, WAVE_AUDIO_PCM);
			//printf ("audio record setting channel %d frequency %d format %d",spec.channels, spec.freq, WAVE_AUDIO_PCM);

			// SDL_PauseAudioIn(0); // start record
			initAudio = 1;
			}
		} else
		mypict.mode = PICTURE;


		/* Allocate tmp buffer for one frame. */

		tmp = (unsigned char*)malloc (frame_size);
		/* laugth the picture thread */
		mypict.data = malloc(frame_size);
		mypict.sizeIn = frame_size;
		mypict.width = image_width;
		mypict.height = image_height;
		mypict.formatIn = format;
		pthread_mutex_init(&mypict.mutex, NULL);
		pthread_cond_init(&mypict.cond, NULL);
		wstatus = pthread_create (&waitandshoot_id, NULL, (void *) waitandshoot, &mypict);
    		if (wstatus != 0) {
			fprintf(stderr,"thread shoot Create error!\n");
			exit(-1);
		}

	 	if (interval && videoOn){
			// set_timer(interval);
			SDL_SetTimer((Uint32) interval,callback_timer);
		 }
		i = 0;

		while (run && Oneshoot) {
			memset(tmp,0x00,frame_size);
			intime = SDL_GetTicks ();
			pictime = intime - delaytime;
			delaytime = intime;
			/* Try to synchronize sound with frame rate */
			if (initAudio && i > 9){
					initAudio = 0;
					SDL_PauseAudioIn(0); // start record
				}
			/* compute bytes sound */
			if (pictime < 100) {

				bytes_per_read =
					((AUDIO_SAMPLERATE / 1000) * 2 * pictime);
			}
			i++;
			if (grabMethod) {
				ff = vmmap.frame;
				if (ioctl (fd, VIDIOCSYNC, &ff) < 0) {
					perror ("cvsync err\n");
				}
				vmmap.frame = ff;
				memcpy (tmp,
					pFramebuffer +
					videombuf.offsets[vmmap.frame],
					frame_size);
				if ((status =
				     ioctl (fd, VIDIOCMCAPTURE, &vmmap)) < 0) {
					perror ("cmcapture");
					printf (">>cmcapture err %d\n", status);
				}
				vmmap.frame =
					(vmmap.frame + 1) % videombuf.frames;

			} else {
				/* read method */
				len = read (fd, tmp, frame_size);
				// printf ("len %d asked %d \n",len,frame_size);
			}
			synctime = SDL_GetTicks ();
			/*here the frame is in tmp ready to used */
			if (pictFlag){
			// printf("get Picture condition \n");
				wstatus = pthread_mutex_lock (&mypict.mutex);
    				if (wstatus != 0) {fprintf(stderr,"Lock error!\n"); exit(-1);}
     				memcpy (mypict.data,tmp,frame_size);
				//printf("COND OK !!\n");
       				wstatus = pthread_cond_signal (&mypict.cond);
    				if (wstatus != 0) {fprintf(stderr,"Signal error!\n"); exit(-1);}
    				wstatus = pthread_mutex_unlock (&mypict.mutex);
    				if (wstatus != 0) {fprintf(stderr,"Unlock error!\n"); exit(-1);}
			}
			if ((outputfile) && (i > 10) && !interval) {
				/* Output video frame with the good format */
				switch (format) {
					case VIDEO_PALETTE_JPEG:{
						int fs = get_jpegsize (tmp , frame_size);
							if (AVI_write_frame (out_fd,
							     		    (unsigned char *)
							     		     tmp,
							    		     fs) < 0)
									printf ("write error on avi out \n");
									method = 0;
						}
						break;
					case VIDEO_PALETTE_RAW_JPEG:{

								if (AVI_write_frame
							    			(out_fd,
							     			(unsigned char *)
							     			tmp,
							    			 frame_size) < 0)
									printf ("write error on avi out \n");

						}
						break;
					case VIDEO_PALETTE_YUV420P:{
							if ( videocomp ) {

								memcpy( jpegData,tmp,frame_size);
								jpegSize=frame_size;
								/*jpegData is destroy here */
								dpsh_yuv_encode(jpegData, dpshDest, &jpegSize);

							// printf ("write DPSH Size %d \n", jpegSize);
							if (AVI_write_frame
									 (out_fd,
									(unsigned char *)
									dpshDest, jpegSize) < 0)
									printf ("write error on avi out \n");
							}else{
								if (AVI_write_frame
									 (out_fd,
									(unsigned char *)
									tmp,
									image_width *
									image_height *
									1.5) < 0)
									printf ("write error on avi out \n");
							}
						}
						break;
					case VIDEO_PALETTE_RGB565:
					case VIDEO_PALETTE_RGB24:
					case VIDEO_PALETTE_RGB32:{
							if (AVI_write_frame
							    (out_fd,
							     (unsigned char *)
							     tmp,
							     image_width *
							     image_height *
							     bpp) < 0)
								printf ("write error on avi out \n");
						}
						break;
					default:
						break;
				}
				/* write sound in avi */
				if (audioout) {
					//printf("bytes per read sound %d \n",bytes_per_read);
					SDL_LockAudio();
					ptread = RingBuffer.ptread;
					ptwrite = RingBuffer.ptwrite;
					SDL_UnlockAudio();
					if (ptwrite > ptread) {
					if (AVI_write_audio
						    (out_fd, (char *) RingBuffer.buffer+ptread,
						     ptwrite-ptread) < 0)
							printf (" write AVI error \n");
						 RingBuffer.ptread =  (RingBuffer.ptread + (ptwrite-ptread))%MAXBUFFERSIZE;
					} else if (ptwrite < ptread ){
					if (AVI_write_audio
						    (out_fd, (char *) RingBuffer.buffer+ptread,
						     MAXBUFFERSIZE-ptread) < 0)
							printf (" write AVI error \n");
					if (AVI_write_audio
						    (out_fd, (char *) RingBuffer.buffer,
						     ptwrite) < 0)
							printf (" write AVI error \n");
					 RingBuffer.ptread =  (RingBuffer.ptread + ptwrite+(MAXBUFFERSIZE-ptread))%MAXBUFFERSIZE;
					}

				}

			}

			compresstime =SDL_GetTicks ();



			if ( decodeOn && videoOn && (i > 10))
			{	/* Display data */
				p = pp = pscreen->pixels;

			  refresh_screen ( tmp, p, format,
			  image_width,image_height,owidth,oheight,image_width*image_height*bpp,autobright);
			  if (autobright)
			  	adjust_bright(&videopict, fd);
			  decodetime = SDL_GetTicks ();

			SDL_UpdateRect (pscreen, 0, 0, 0, 0);	//update the entire screen

			} else {
			decodetime = SDL_GetTicks ();
			}

			/* increment the real frame count and update statistics */
			framecount++;

			total_decode_time += decodetime - synctime;
			average_decode_time = total_decode_time / framecount;

			if (!autobright && statOn){
// \r
				printf ("frames:%04d pict:%03dms synch:%03dms write/comp:%03dms decode:%03dms display:%03dms \n",
				 framecount, pictime, synctime-intime, compresstime-synctime, decodetime-compresstime,SDL_GetTicks ()-decodetime);
			fflush (stdout);
			}

			if (SDL_PollEvent (&sdlevent) == 1) {
				switch (sdlevent.type) {
					case SDL_KEYDOWN:
						switch (sdlevent.key.keysym.sym) {
							case SDLK_b:
								videopict.
									brightness
									-=
									0x100;
								setVideoPict
									(&videopict,
									 fd);
								break;
							case SDLK_n:
								videopict.
									brightness
									+=
									0x100;
								setVideoPict
									(&videopict,
									 fd);
								break;
							case SDLK_c:
								videopict.colour -= 0x200;	// = saturation ?
								setVideoPict
									(&videopict,
									 fd);
								break;
							case SDLK_s:
							/* should get the next frame available */
								pictFlag = 1;
								printf ("\nPicture asked\n");
								break;
							case SDLK_v:
								videopict.colour += 0x200;	// = saturation ?
								setVideoPict
									(&videopict,
									 fd);
								break;
							case SDLK_w:
								videopict.
									contrast -= 0x200;	// -contrast
								setVideoPict
									(&videopict,
									 fd);
								break;
							case SDLK_x:
								videopict.
									contrast += 0x200;	// +contrast
								setVideoPict
									(&videopict,
									 fd);
								break;
							case SDLK_g:
							timeDown(fd, &videoparam); //up the frame rate by 10ms
							break;
							case SDLK_h:
							timeUp(fd, &videoparam); //slow the frame rate
							break;
							case SDLK_j:
							spcaSetAutoExpo(fd, &videoparam); //toggle autoexpo
							break;
							case SDLK_d:
							qualityUp(fd, &videoparam); // increase quality
							break;
							case SDLK_f:
							qualityDown(fd, &videoparam); //decrease quality
							break;
							case SDLK_l:
							spcaSwitchLightFrequency(fd, &videoparam); //switch light frequency filter
							break;
							case SDLK_q:
								printf ("\nStop asked\n");
								run = 0;
								break;
							default:

								break;
						}
						break;
					case SDL_QUIT:
						run = 0;
						break;
				}
			}

		}

			if (audioout) {

				SDL_CloseAudioIn(); //stop record
				printf ("free sound buffer\n");

			}
			if (interval && decodeOn && videoOn){
			// set_timer(interval);
			SDL_SetTimer((Uint32) 0, NULL);
			 }
		time = SDL_GetTicks () - time;
		printf ("\nUsed %dms for %d images => %dms/image %dfps.\n",
			time, framecount, time / framecount,
			framecount * 1000 / time);


		printf ("Quiting SDL.\n");

		printf ("Decoded frames:%d Average decode time: %f\n",
			framecount, average_decode_time);
		if (grabMethod) {
			printf ("unmapping\n");
			munmap (pFramebuffer, mmapsize);
		}
		printf ("closing\n");
		close (fd);
		printf ("closed\n");
		if (outputfile) {
			if (!interval)
			out_fd->fps = (double) framecount *1000 / time;

			AVI_close (out_fd);
			printf ("close avi\n");
			if (audioout) {

				 SDL_QuitAudioIn();
				printf ("free sound buffer\n");

			}
			free (jpegData);
			free(dpshDest);
		}

		pthread_cancel(waitandshoot_id);
		pthread_join (waitandshoot_id,NULL);
		pthread_cond_destroy(&mypict.cond);
		pthread_mutex_destroy(&mypict.mutex);
		if (mypict.data)
			free(mypict.data);
		printf ("Destroy Picture thread ...\n");

		/* Shutdown all subsystems */
		free (tmp);
		//close_libjpeg_decoder ();
		printf ("Quiting....\n");
		SDL_Quit ();
return 0;
}

Uint32 GetVideoBpp (void)
{
	const SDL_VideoInfo *info;
	info = SDL_GetVideoInfo();
	printf(
	"Current display: %d bits-per-pixel\n",info->vfmt->BitsPerPixel);
return info->vfmt->BitsPerPixel;
}

static int
 setVideoPict (struct video_picture *videopict, int fd)
{
	if (ioctl (fd, VIDIOCSPICT, videopict) < 0) {
		perror ("Couldnt get videopict params with VIDIOCSPICT\n");
		return -1;
	}
	printf ("VIDIOCSPICT\n");
	printf ("brightness=%d hue=%d color=%d contrast=%d whiteness=%d \n",
		videopict->brightness, videopict->hue,
		videopict->colour, videopict->contrast, videopict->whiteness);
		printf("depth=%d palette=%d \n",videopict->depth, videopict->palette);

	return 0;
}


static int
 readFrame (avi_t *out_fd, long i,unsigned char **jpegData, int *jpegSize, struct Rbuffer *RingBuffer, int maxsound, int *audiolen,
	   int *soundbytes, int isaudio, int updown, int compress )
{
	static int avipos = 0;
	unsigned char *dpshDest = NULL;
	int ptwrite;
	int ptread;
	int audiosize=0;
	int delta =0 ;
	/* got frame i and read the data */

	if (AVI_set_video_position (out_fd, i) < 0)
		printf ("error get video position\n");
		*jpegSize = (int) AVI_frame_size (out_fd, i);
		//audiosize = (int)AVI_chunk_size (out_fd, i);
		audiosize = *soundbytes;
		avipos += audiosize;
		delta = ((i+1) * (*soundbytes))-avipos;
		//printf("audiosize delta %d \n",delta);
		*jpegData = (unsigned char *) realloc (*jpegData, (size_t) *jpegSize);
	if (AVI_read_frame (out_fd, *jpegData) < 0)
		printf ("error read frame\n");
	if (compress){
		/* decompress the video stream */
		dpshDest = malloc(640*480*3);
		dpsh_yuv_decode(*jpegData , dpshDest, jpegSize);
		/* restore jpegData size to the uncompressed mode */
		*jpegData = (unsigned char *) realloc (*jpegData, (size_t) *jpegSize);
		memcpy(*jpegData, dpshDest, *jpegSize);
		free(dpshDest);
	}

	if (isaudio && (*audiolen > 0)) {
			SDL_LockAudio();
				ptread = RingBuffer->ptread;
				ptwrite = RingBuffer->ptwrite;
			SDL_UnlockAudio();
		if (updown) {
			/* upstream */
			/* read the audio Byte until no Byte */

					if (ptwrite + audiosize < MAXBUFFERSIZE) {

					if (AVI_read_audio
						    (out_fd, (char *) RingBuffer->buffer+ptwrite,
						     audiosize) < 0)
							 printf (" Read AVIchunck error \n");
					} else {
					//printf ("part1 %d, part2 %d \n",MAXBUFFERSIZE-ptwrite,audiosize -( MAXBUFFERSIZE-ptwrite));

					if (AVI_read_audio
						    (out_fd, (char *) RingBuffer->buffer+ptwrite,
						     MAXBUFFERSIZE-ptwrite) < 0)
							 printf (" Read AVIpart1 error \n");
					if (AVI_read_audio
						    (out_fd, (char *) RingBuffer->buffer,
						     audiosize -( MAXBUFFERSIZE-ptwrite)) < 0)
							 printf (" Read AVIpart2 error \n");

					}
			SDL_LockAudio();
				RingBuffer->ptwrite =  (RingBuffer->ptwrite + audiosize)%MAXBUFFERSIZE;
			SDL_UnlockAudio();

			*audiolen -= audiosize;



		} else {
			/* down stream */
			/* rewind one sound fragment */
			*audiolen += *soundbytes ;
			avipos = (maxsound - *audiolen);
			if (avipos >= 0)
				if (AVI_set_audio_position (out_fd, (long)avipos) < 0)
					printf ("error read soundframe down \n");
			//printf("audiolenth : %d soundbytes %d aviposition %d\n",*audiolen,audiosize,avipos);


		}

	}
return delta;
}

#define ADDRESSE(x,y,w) (((y)*(w))+(x))
void resize16 (unsigned char *dst,unsigned char *src, int Wd,int Hd,int Ws,int Hs)
{
	int rx,ry;
	int xscale,yscale;
	int x,y;
	Myrgb24 pixel;
	Myrgb16 *output =(Myrgb16*) dst ;
	Myrgb24 *input = (Myrgb24*) src ;

	xscale =  (Ws << 16)/Wd;
	yscale = (Hs << 16)/ Hd;
	for (y = 0; y < Hd; y++){
		for (x = 0; x < Wd; x++){
		 rx = x*xscale >> 16;
		 ry = y*yscale >> 16;
		 output->blue = input[ADDRESSE((int)rx,(int)ry,Ws)].blue >> 3;
		 output->green = input[ADDRESSE((int)rx,(int)ry,Ws)].green >> 2;
		 output->red = input[ADDRESSE((int)rx,(int)ry,Ws)].red >> 3;
		 output++ ;
		}
	}



}
void resize (unsigned char *dst,unsigned char *src, int Wd,int Hd,int Ws,int Hs)
{
	int rx,ry;
	int xscale,yscale;
	int x,y;
	Myrgb24 pixel;
	Myrgb24 *output =(Myrgb24*) dst ;
	Myrgb24 *input = (Myrgb24*) src ;

	xscale =  (Ws << 16)/Wd;
	yscale = (Hs << 16)/ Hd;
	for (y = 0; y < Hd; y++){
		for (x = 0; x < Wd; x++){
		 rx = x*xscale >> 16;
		 ry = y*yscale >> 16;
		 memcpy(output++,&input[ADDRESSE((int)rx,(int)ry,Ws)],sizeof(Myrgb24));
		}
	}



}
/* refresh_screen input all palette to RGB24 -> RGB565 */
static void
refresh_screen (unsigned char *src, unsigned char *pict, int format, int width,
		int height,int owidth ,int oheight, int size, int autobright)
{
  unsigned int *lpix;
  unsigned short  *pix;
  int i;
  int intwidth = width;
  int intheight = height;
unsigned char *dst = NULL;
/* in case VIDEO_PALETTE_RAW_JPEG nothing todo */
	if( format == VIDEO_PALETTE_RAW_JPEG)
		return ;
	/* for some strange reason tiny_jpegdecode need 1 macroblok line more ?? */
	dst = malloc (width*(height+8)*3);
	switch (format) {
		case VIDEO_PALETTE_JPEG:{
			//libjpeg_decode (dst, src, size, width, height);
			jpeg_decode (&dst, src, &intwidth, &intheight);
			}
			break;
		case VIDEO_PALETTE_YUV420P:{
				// uncompressed data yuv420P decoder in module
				// equalize (src, width, height, 0);
				YUV420toRGB ((unsigned char *) src,
					     (unsigned char *) dst, width,
					     height, 0, 0);

			}
			break;
		case VIDEO_PALETTE_RGB565:
		pix = (unsigned short *) src;
	  	for (i = 0; i < ((width*height*3) - 3); i += 3)
	    		{
	      		dst[i] = (*pix & 0x001F) << 3;
	      		dst[i + 1] = (*pix & 0x07E0) >> 3;
	      		dst[i + 2] = (*pix & 0xF800) >> 8;
	      		pix++;
	    		}
		break;
		case VIDEO_PALETTE_RGB32:
			lpix = (unsigned int  *) src;
	 	 for (i = 0; i < ((width*height*3) - 3); i += 3)
	    		{
	      		dst[i] = (*lpix & 0x000000FF);
	    	  	dst[i + 1] = (*lpix & 0x0000FF00) >> 8;
	      		dst[i + 2] = (*lpix & 0x00FF0000) >> 16;
	      		lpix++;
	    		}
		break;
		case VIDEO_PALETTE_RGB24:{
				// uncompressed data. rgb decoder in module simple copy for display.
				memcpy (dst, src, size);
			}
			break;
		default:
			break;
	}
if (autobright)
	totmean = get_pic_mean( width, height, dst, 0, 0, 0,
				  width, height );
/* rezize16 input rgb24 output rgb565 */
resize16 (pict,dst,owidth,oheight,width,height) ;
	free(dst);
}
#if 0
static int
 isSpcaChip (const char *BridgeName)
{
	int i = -1;
	int size =0;
	//size = strlen(BridgeName)-1;
	/* Spca506 return more with channel video, cut it */
	//if (size > 10) size = 8;
	/* return Bridge otherwhise -1 */
	for (i=0; i < MAX_BRIDGE ;i++){
		size=strlen(Blist[i].name);
		if(strncmp(BridgeName,Blist[i].name,size) == 0) {
		printf("Bridge find %s number %d\n",Blist[i].name,i);
		 break;
		}
	}

return i;
}
static int
 getStreamId (const char * BridgeName)
{
	int i = -1;
	int match = -1;
/* return Stream_id otherwhise -1 */
	if((match=isSpcaChip(BridgeName)) < 0){
	 printf("Not an Spca5xx Camera !!\n");
	 return match;
	 }
	switch (match) {
		case BRIDGE_SPCA505:
		case BRIDGE_SPCA506:
			i= YYUV;
			break;
		case BRIDGE_SPCA501:
			i = YUYV;
			break;
		case BRIDGE_SPCA508:
		 	i = YUVY;
			break;
		case BRIDGE_SPCA504:
		case BRIDGE_SPCA500:
		case BRIDGE_SPCA504B:
		case BRIDGE_SPCA533:
		case BRIDGE_SPCA504C:
		case BRIDGE_SPCA536:
//		case BRIDGE_ZR364XX:
		case BRIDGE_ZC3XX:
		case BRIDGE_CX11646:
		case BRIDGE_SN9CXXX:
		case BRIDGE_MR97311:
			i = JPEG;
			break;
		case BRIDGE_ETOMS:
		case BRIDGE_SONIX:
		case BRIDGE_SPCA561:
		case BRIDGE_TV8532:
			i = GBRG;
			break;
		default:
			i = -1;
			 printf("Unable to find a StreamId !!\n");
			break;

	}
return i;
}
 #endif
 static int
isSpcaChip (const char *BridgeName)
{
  int i = -1;
  int find = -1;
  int size = 0;

  /* Spca506 return more with channel video, cut it */

  /* return Bridge otherwhise -1 */
  for (i = 0; i < MAX_BRIDGE -1; i++)
    {
    size = strlen (Blist[i].name) ;
   // printf ("is_spca %s \n",Blist[i].name);
      if (strncmp (BridgeName, Blist[i].name, size) == 0)
	{
	find = i;
	  break;
	}
    }

  return find;
}

static int
getStreamId (const char *BridgeName)
{
  int i = -1;
  int match = -1;
/* return Stream_id otherwhise -1 */
  if ((match = isSpcaChip (BridgeName)) < 0)
    {
      printf ("Not an Spca5xx Camera !!\n");
      return match;
    }
  switch (match)
    {
    case BRIDGE_SPCA505:
    case BRIDGE_SPCA506:
      i = YYUV;
      break;
    case BRIDGE_SPCA501:
    case BRIDGE_VC0321:
      i = YUYV;
      break;
    case BRIDGE_SPCA508:
      i = YUVY;
      break;
    case BRIDGE_SPCA536:
    case BRIDGE_SPCA504:
    case BRIDGE_SPCA500:
    case BRIDGE_SPCA504B:
    case BRIDGE_SPCA533:
    case BRIDGE_SPCA504C:
    case BRIDGE_ZC3XX:
    case BRIDGE_CX11646:
    case BRIDGE_SN9CXXX:
    case BRIDGE_MR97311:
    case BRIDGE_VC0323:
    case BRIDGE_PAC7311:
      i = JPEG;
      break;
    case BRIDGE_ETOMS:
    case BRIDGE_SONIX:
    case BRIDGE_SPCA561:
    case BRIDGE_TV8532:
    case BRIDGE_PAC207:
      i = GBRG;
      break;
    default:
      i = UNOW; // -1;
      printf ("Unable to find a StreamId !!\n");
      break;

    }
  return i;
}

static int
 probeSize (const char *BridgeName, int *width, int *height)
{ 	int bridge = -1;
	int i;
	unsigned int intwidth;
	unsigned int intheight;
	unsigned int intformat;
	int match =0;
/* return 1->ok otherwhise -1 */
	if ((bridge= isSpcaChip(BridgeName)) < 0) {
	printf ("Cannot Probe Size !! maybe not an Spca5xx Camera\n");
	return -1;
	}

	for (i=0; (unsigned int)(GET_EXT_MODES(bridge)[i][0]);i++){
		intwidth = GET_EXT_MODES(bridge)[i][0];
		intheight = GET_EXT_MODES(bridge)[i][1];
		intformat = (GET_EXT_MODES(bridge)[i][2] & 0xF0) >> 4;
		if ((intwidth== *width) && (intheight == *height)){
			match = 1;
		} else {
			match = 0;
		}

		printf("Available Resolutions width %d  heigth %d %s %c\n",
			intwidth,intheight,(intformat)?"decoded":"native",
			(match)?'*':' ');
	}

}

/*
 * get_pic_mean:  Calculate the mean value of the pixels in an image.
 *                      This routine is used for adjusting the values of
 *                      the pixels to reasonable brightness.
 *
 * Arguments:           width, height = Dimensions of the picture
 *                      buffer    = Buffer to picture.
 *                      is_rgb    = 1 if the picture is rgb, else 0
 *                      start{x,y}, end{x,y} = Region to calculate the
 *                                  mean of.  This MUST be valid before
 *                                  being passed in!
 *
 * Return values:       Returns the average of all the components if rgb, else
 *                      the average whiteness of each pixel if B&W
 */
static int
 get_pic_mean( int width, int height, const unsigned char *buffer,
			  int is_rgb,int startx, int starty, int endx,
			  int endy )
{
  double rtotal = 0, gtotal = 0, btotal = 0;
  int minrow, mincol, maxrow, maxcol, r, c;
  double bwtotal = 0, area;
  int rmean, gmean, bmean;
  const unsigned char *cp;

  minrow = starty;
  mincol = startx;
  maxrow = endy;
  maxcol = endx;

  area = (maxcol-mincol) * (maxrow-minrow);

  c = mincol;
  if( is_rgb ){
    for( r=minrow; r < maxrow; r++ ){
      cp = buffer + (r*width+c)*3;
      for( c=mincol; c < maxcol; c++ ) {
	rtotal += *cp++;
	gtotal += *cp++;
	btotal += *cp++;
      }
    }
    rmean = rtotal / area;
    gmean = gtotal / area;
    bmean = btotal / area;
    return (double)rmean * .299 +
	   (double)gmean * .587 +
           (double)bmean * .114;
  } else {
    for( r=minrow; r < maxrow; r++ ){
      cp = buffer + (r*width+c)*1;
      for( c=mincol; c < maxcol; c++ ) {
	bwtotal += *cp++;
      }
    }
    return (int)(bwtotal / area);
  }
}

static int
 adjust_bright( struct video_picture *videopict, int fd)
{
  int newbright;

  newbright=videopict->brightness;
  if( totmean < BRIGHTMEAN - BRIGHTWINDOW||
      totmean > BRIGHTMEAN + BRIGHTWINDOW ){
    newbright += (BRIGHTMEAN-totmean)*SPRING_CONSTANT;
    newbright = clip_to(newbright, V4L_BRIGHT_MIN, V4L_BRIGHT_MAX);
  }

  printf("totmean:%03d newbright:%05d\r",totmean,newbright);
  /* Slight adjustment */
  videopict->brightness=newbright;
  setVideoPict(videopict,fd);
}

static int clip_to(int x, int low, int high){
  if(x<low)
      return low;
  else if (x>high)
      return high;
  else
      return x;
}
