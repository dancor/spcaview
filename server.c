/****************************************************************************
#	 	spcaserv: TCP/IP mjpeg stream server .		            #
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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>
#include "pargpio.h"
#include "spcaframe.h"
#include "spcav4l.h"
#include "utils.h"
#include "tcputils.h"	
#include "version.h"


void grab (void);
void service (void *ir);
void sigchld_handler(int s);
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
  int i;
  int serv_sock,new_sock;
  pthread_t w1;
  pthread_t server_th;
  int sin_size;
  unsigned short serverport = 7070;
  struct sockaddr_in their_addr;
  struct sigaction sa;
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
	if (strcmp (argv[i], "-w") == 0) {
			if (i + 1 >= argc) {
				printf ("No parameter specified with -w, aborting.\n");
				exit (1);
			}
			serverport = (unsigned short) atoi (argv[i + 1]);
			if (serverport < 1024  ){
			printf ("Port should be between 1024 to 65536 set default 7070 !.\n");
			serverport = 7070;
			}
	}
	if (strcmp (argv[i], "-p") == 0)
	{
	  if (i + 1 >= argc)
	    {
	      printf ("No parameter specified with -p, aborting.\n");
	      exit (1);
	    }
	  partdevice = strdup (argv[i + 1]);
	  usepartport = 1;
	}
      if (strcmp (argv[i], "-h") == 0)
	{
	  printf ("usage: cdse [-h -d -g ] \n");
	  printf ("-h	print this message \n");
	  printf ("-d	/dev/videoX       use videoX device\n");
	  printf ("-g	use read method for grab instead mmap \n");
	  printf ("-f	video format  default yuv  others options are r16 r24 r32 yuv jpg \n");
	  printf ("-s	widthxheight      use specified input size \n");
	  printf ("-w	port      server port \n");
	  printf ("-p	/dev/partportX       use partportX device\n");
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
 // printf("depth %d",videoIn.bppIn);  
if(usepartport){ 
	fd = openclaimParaport(partdevice);
	if (fd > 0) {
	port_setdata1(fd,128);
	port_setdata2(fd,128);
	} else {
	usepartport = 0;
	fd = 0;
	}
 }

 if((err= pthread_create (&w1, NULL, (void *) grab, NULL)) != 0){
	printf("thread grabbing error %d \n",err);
	close_v4l (&videoIn);
	exit(1);
 }
 
 
 
 serv_sock = open_sock(serverport);
 signal(SIGPIPE, SIG_IGN);	/* Ignore sigpipe */

	sa.sa_handler = sigchld_handler;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART;
	syslog(LOG_ERR,"Spcaserv Listening on Port  %d\n",serverport);
	printf("Waiting .... for connection. CTrl_c to stop !!!! \n");	
  while (videoIn.signalquit)
    {
     sin_size = sizeof(struct sockaddr_in);
		if ((new_sock = accept(serv_sock, (struct sockaddr *)&their_addr, &sin_size)) == -1)
		{
			continue;
		}
	syslog(LOG_ERR,"Got connection from %s\n",inet_ntoa(their_addr.sin_addr));
	printf("Got connection from %s\n",inet_ntoa(their_addr.sin_addr));
	pthread_create(&server_th, NULL, (void *)service, &new_sock);
    } 
  pthread_join (w1, NULL);
 if(usepartport){
 	err= closereleaseParaport(fd);
 }
  close(serv_sock);
  close_v4l (&videoIn);

}


void
grab (void)
{
int err = 0;
  for (;;)
    {
      //printf("I am the GRABBER !!!!! \n");
      err = v4lGrab(&videoIn);
      if (!videoIn.signalquit || (err < 0)){
      printf("GRABBER going out !!!!! \n");
	break;
	}
    }
}

void
service (void *ir)
{
  int *id = (int*) ir;
  int frameout = 1;
  struct frame_t *headerframe;
  int ret;
  int sock;
  int framecount=0;
  int retsize=0;
  int ack = 0;
 // int i = 0;
 // int j;
 unsigned char x = 0;
 unsigned char y = 0;
 int err;
  unsigned char wakeup = 0;
  unsigned char cmd = 0;
 unsigned short bright;
 unsigned short contrast;
  struct client_t message;
  sock = *id;
 // printf (" \n I am the server %d \n", *id);
 /* initialize video setting */
 bright = upbright(&videoIn);
 contrast = upcontrast(&videoIn);
 bright = downbright(&videoIn);
 contrast = downcontrast(&videoIn);
  for (;;)
    {   
      memset(&message,0,sizeof(struct client_t));
	 ret = read(sock,(unsigned char*)&message,sizeof(struct client_t));
	 //printf("retour %s %d ret\n",message.message,ret);
	 if (ret < 0) {
	 	printf(" Client vaporished !! \n");
		break;
	 }
	 if (!ret) break;
	 if(ret && (message.message[0] != 'O')) break;
	 if(fd) {
	 if ( x != message.x ){
	   x = message.x; 
	   //printf("x %d \n",x);
	   err = port_setdata1(fd,x);
	 }
	 if ( y != message.y){
	   y = message.y;
	   //printf("y %d \n",y);
	   err = port_setdata2(fd,y);
	 }
	 }
	 if (message.updobright){
	 	switch (message.updobright){
		case 1: bright = upbright(&videoIn);
		break;
		case 2: bright = downbright(&videoIn);
		break;
		}
		ack = 1;
	 } else if (message.updocontrast){
	 	switch (message.updocontrast){
		case 1: contrast = upcontrast(&videoIn);
		break;
		case 2: contrast = downcontrast(&videoIn);
		break;
		}
		ack = 1;
	} else if (message.updoexposure){
	 	switch (message.updoexposure){
		case 1: spcaSetAutoExpo(&videoIn);
		break;
		case 2:;
		break;
		}
		ack = 1;
	} else if (message.updosize){ //compatibility FIX chg quality factor ATM
	 	switch (message.updosize){
		case 1: qualityUp(&videoIn);
		break;
		case 2: qualityDown(&videoIn);
		break;
		}
		ack = 1;
	} else if (message.fps){
	 	switch (message.fps){
		case 1: timeDown(&videoIn);
		break;
		case 2: timeUp(&videoIn);
		break;
		}
		ack = 1;
	} else if (message.sleepon){
		if(fd){
			switch (message.sleepon){
			case 1:
	 		wakeup = !wakeup; //1;
			break;
			case 2:
			cmd = 1;
			break;
			}
		}
		ack = 1;
	 } else ack =0;
redo:
       while ((frameout == videoIn.frame_cour) && videoIn.signalquit) usleep(1000);
       if (videoIn.signalquit){
	videoIn.framelock[frameout]++;
     	headerframe = (struct frame_t *) videoIn.ptframe[frameout];
	//headerframe->nbframe = framecount++;
	//printf ("reader %d key %s width %d height %d times %dms size %d \n", sock,headerframe->header,
	//headerframe->w,headerframe->h,headerframe->deltatimes,headerframe->size);
	  headerframe->acknowledge = ack;
	  headerframe->bright = bright;
	  headerframe->contrast = contrast;
	  headerframe->wakeup = wakeup;
	  if(headerframe->size == 0){
	  // frame corrupt get another one
	  	videoIn.framelock[frameout]--;
		frameout = (frameout+1)% OUTFRMNUMB;  
	  goto redo;
	  }
	 ret = write_sock(sock, (unsigned char *)headerframe, sizeof(struct frame_t)) ;
	 
	 if(!wakeup)	
	 ret = write_sock(sock,(unsigned char*)(videoIn.ptframe[frameout]+sizeof(struct frame_t)),headerframe->size);
	 else if (fd)
	 	if ( !read_portstatus15(fd)) {
	 	wakeup = 0;
	 }
	 
	 if(fd )
	 if(cmd ){ //relay command on bit autofeed
	 	port_toggleAutoFeedbit(fd);
	 	usleep(100000);
	 	port_toggleAutoFeedbit(fd);
	  	cmd = 0;
	 }
	videoIn.framelock[frameout]--;
	frameout = (frameout+1)% OUTFRMNUMB;     
      } else {
       printf("reader %d going out \n",*id);
	break;
      }
    }
  close_sock(sock);
  pthread_exit(NULL);
}
void sigchld_handler(int s)
{
	videoIn.signalquit = 0;
}
