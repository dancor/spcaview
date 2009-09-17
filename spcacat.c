 /****************************************************************************
#	 	spcacat: grabpicture from the spca5xx module.		    #
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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>
#include <linux/parport.h>
#include <linux/ppdev.h>
#include <sys/time.h>
#include "spcaframe.h"
#include "spcav4l.h"
#include "utils.h"
#include "version.h"


volatile int pictFlag = 0;
static int Oneshoot = 1;
static int numshoot = 0;
static int modshoot = 0;
static int overwrite = 0;
void grab (void);
static int set_timer(int interval);

int fd = 0;
struct vdIn videoIn;

int
main (int argc, char *argv[])
{
  int videoon = 0;
  char *videodevice = NULL;
  char *partdevice = NULL;
  int usepartport = 0;
  int err;
  int grabmethod = 1;
  int format = VIDEO_PALETTE_YUV420P;
  int width = 352;
  int height = 288;
  char *separateur;
  char *sizestring = NULL;
  char *mode = NULL;
  int interval = 0;
  int i;
  pthread_t w1;
  
    for (i = 1; i < argc; i++)
    {
      /* skip bad arguments */
      if (argv[i] == NULL || *argv[i] == 0 || *argv[i] != '-')
	{
	  continue;
	}
      if (strcmp (argv[i], "-d") == 0)
	{
	  if (i + 1 >= argc)
	    {
	      printf ("No parameter specified with -d, aborting.\n");
	      exit (1);
	    }
	  videodevice = strdup (argv[i + 1]);
	}
      if (strcmp (argv[i], "-g") == 0)
	{
	  /* Ask for read instead default  mmap */
	  grabmethod = 0;
	}
	if (strcmp (argv[i], "-f") == 0) {
			if (i + 1 >= argc) {
				printf ("No parameter specified with -f, aborting.\n");
				exit (1);
			}
			mode = strdup (argv[i + 1]);

			if (strncmp (mode, "r32", 3) == 0) {
				format = VIDEO_PALETTE_RGB32;
				

			} else if (strncmp (mode, "r24", 3) == 0) {
				format = VIDEO_PALETTE_RGB24;
				
			} else if (strncmp (mode, "r16", 3) == 0) {
				format = VIDEO_PALETTE_RGB565;
				
			} else if (strncmp (mode, "yuv", 3) == 0) {
				format = VIDEO_PALETTE_YUV420P;
				
			} else if (strncmp (mode, "jpg", 3) == 0) {
				format = VIDEO_PALETTE_JPEG;
				
			}else {
				format = VIDEO_PALETTE_YUV420P;
				
			}
	}
	if (strcmp (argv[i], "-s") == 0) {
			if (i + 1 >= argc) {
				printf ("No parameter specified with -s, aborting.\n");
				exit (1);
			}

			sizestring = strdup (argv[i + 1]);

			width = strtoul (sizestring, &separateur, 10);
			if (*separateur != 'x') {
				printf ("Error in size use -s widthxheight \n");
				exit (1);
			} else {
				++separateur;
				height =
					strtoul (separateur, &separateur, 10);
				if (*separateur != 0)
					printf ("hmm.. dont like that!! trying this height \n");
				printf (" size width: %d height: %d \n",
					width, height);
			}
	}
	if (strcmp (argv[i], "-P") == 0)
	{
	  if (i + 1 >= argc)
	    {
	      printf ("No parameter specified with -P, aborting.\n");
	      exit (1);
	    }
	  partdevice = strdup (argv[i + 1]);
	  usepartport = 1;
	}
	if (strcmp (argv[i], "-o") == 0)
	{
	  overwrite = 1;
	}
	if (strcmp (argv[i], "-p") == 0) { 		  
			if (argv[i + 1]) {
			  interval = (atoi(argv[i + 1])); // timer works on ms
			  if ( interval < 50) interval = 50;
 		  	 } else {
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
 		   	 	printf ("No parameter specified with -N, aborting.\n");
 		    		exit (1);
 		  	}

 		}
      if (strcmp (argv[i], "-h") == 0)
	{
	  printf ("usage: cdse [-h -d -g -s -P -p -N -o] \n");
	  printf ("-h	print this message \n");
	  printf ("-d	/dev/videoX       use videoX device\n");
	  printf ("-g	use read method for grab instead mmap \n");
	  printf ("-f	video format  default yuv  others options are r16 r24 r32 yuv jpg \n");
	  printf ("-s	widthxheight      use specified input size \n");
	  printf ("-P	/dev/partportX       use partportX device\n");
	  printf ("-p	x ms       take a picture every x ms minimum is set to 50ms \n");
	  printf ("-N   take a N pictures every p ms and stop \n");
	  printf ("-o	overwrite picture, each picture come with the same name SpacPict.jpg \n");
	  exit (0);
	}
    }
   /* main code */  
  printf(" %s \n",version);
  if (videodevice == NULL || *videodevice == 0)
    {
      videodevice = "/dev/video0";
    }
  if(usepartport && partdevice == NULL)
  	partdevice = "/dev/parport0";
	
  memset (&videoIn, 0, sizeof (struct vdIn));
  if (init_videoIn
        (&videoIn, videodevice, width, height, format,grabmethod) != 0)     
    printf (" damned encore rate !!\n");
/*
if(usepartport){ 
	fd = openclaimParaport(partdevice);
	if(fd > 0){
	port_setdata1(fd,128);
	port_setdata2(fd,128);
	} else {
	usepartport =0;
	fd = 0;
	}
 }
 */
  pthread_create (&w1, NULL, (void *) grab, NULL);
  err = set_timer(interval);
	printf("Waiting .... for Incoming Events. CTrl_c to stop !!!! \n");
	/* main wait loop */
	while(videoIn.signalquit){
	sleep(1);
	}
pthread_join (w1, NULL);
/*
 if(usepartport){
 	err= closereleaseParaport(fd);
 }
*/
  close_v4l (&videoIn);
}

void
grab (void)
{
int err = 0;
int iframe =0;
unsigned char *pictureData =NULL;
struct frame_t *headerframe;
  for (;;)
    {
      //printf("I am the GRABBER !!!!! \n");
      err = v4lGrab(&videoIn);
      if (!videoIn.signalquit || (err < 0)){
	break;
	}
	if(pictFlag){
	/*verrouiller le buffer */
		/*verrouiller le buffer */
	if(modshoot){
	     numshoot--;
	     if(!numshoot)
	     	videoIn.signalquit = 0;
	     }
	iframe=(videoIn.frame_cour +(OUTFRMNUMB-1))% OUTFRMNUMB; //set the last frame available
	videoIn.framelock[iframe]++;
	headerframe=(struct frame_t*)videoIn.ptframe[iframe];
	pictureData = videoIn.ptframe[iframe]+sizeof(struct frame_t);
	if(overwrite) {
	getJpegPicture(pictureData,headerframe->w,headerframe->h,VIDEO_PALETTE_JPEG,headerframe->size,PICTWRD,NULL);
	} else {
	getJpegPicture(pictureData,headerframe->w,headerframe->h,VIDEO_PALETTE_JPEG,headerframe->size,PICTURE,NULL);
	}
	pictFlag = 0;
	videoIn.framelock[iframe]--;
	}
	
    }
 printf("GRABBER going out !!!!! \n");
}
typedef void (*sighandler_t)(int);
static sighandler_t original_sighandler;

static void take_snap(int x)
{
pictFlag=1;
}

static int set_timer(int interval)
{
  struct itimerval itimer;

  itimer.it_interval.tv_sec = interval/1000;
  itimer.it_interval.tv_usec = (interval%1000)*1000;
  itimer.it_value.tv_sec = interval/1000;
  itimer.it_value.tv_usec =(interval%1000)*1000 ;
  
   original_sighandler = signal(SIGALRM, take_snap);

   setitimer(ITIMER_REAL, &itimer, NULL);
}
