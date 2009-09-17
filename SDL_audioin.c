/***************************************************************************
 *   Copyright (C) 2004 by Tyler Montbriand                                *
 *   tsm@accesscomm.ca                                                     *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/

#include <stdio.h>
#include "SDL_audioin.h"
#include "SDL_audioin_internal.h"

#include "audio_detect.h"

/* Used privately;  index of initialized driver, or -1. */
static int      audioin_init=-1;
/* Used by other drivers - status of audio input */
volatile SDL_audiostatus audioin_status=SDL_AUDIO_STOPPED;
/* Array of input drivers */
extern const Sound_InputFunctions *drivers[];

int SDL_InitAudioIn()
{
  int n;
//fprintf(stderr,"INIT_AUDIO \n");
  if(audioin_init>=0)
  {
    SDL_SetError("Audio Input already initialized!\n");
    return(-1);
  }

  for(n=0; drivers[n]!=NULL; n++)
    if(drivers[n]->init()>=0)
    {
      audioin_init=n;
      fprintf(stderr,"INIT_AUDIO %d\n",n);
      return(0);
    }

  SDL_SetError("No usable input drivers\n");
  return(-1);
}

int SDL_OpenAudioIn(SDL_AudioSpec *desired, SDL_AudioSpec *result)
{
//fprintf(stderr,"OPEN_AUDIO \n");
SDL_Delay(1);
  if(audioin_init<0)
  {
//  fprintf(stderr,"Not initialized, call SDL_InitAudioIn()\n");
    SDL_SetError("Not initialized, call SDL_InitAudioIn() first\n");
    return(-1);
  }
  else if(!AudioSpecValid(desired))
    return(-1);

  return(drivers[audioin_init]->open(desired,result));
}

int SDL_PauseAudioIn(int pause_on)
{
  if(audioin_init<0)
  {
    SDL_SetError("Not initialized, call SDL_InitAudioIn() first\n");
    return(-1);
  }
  else if(audioin_status==SDL_AUDIO_STOPPED)
  {
    SDL_SetError("Audio not open, call SDL_OpenAudioIn() first\n");
    return(-1);
  }
  else
    return(drivers[audioin_init]->pause(pause_on));
}

void SDL_CloseAudioIn(void)
{
//fprintf(stderr,"CLOSE_AUDIO \n");
  if(audioin_init>=0)
  {
    if(audioin_status!=SDL_AUDIO_STOPPED)
      drivers[audioin_init]->close();
  }
  return;
}

void SDL_QuitAudioIn(void)
{
//fprintf(stderr,"QUIT_AUDIO \n");
  if(audioin_init>=0)
  {
    SDL_CloseAudioIn();

    drivers[audioin_init]->quit();
    audioin_init=-1;
  }
  return;
}

SDL_audiostatus SDL_GetAudioInStatus(void)
{
  return(audioin_status);
}

const Sound_InputFunctions *drivers[]=
{
#ifdef AUDIOIN_DEVDSP
  &driver_DEVDSP,
#endif/*AUDIOIN_DEVDSP*/
#ifdef AUDIOIN_RAND
  &driver_RAND,
#endif/*AUDIOIN_RAND*/
#ifdef AUDIOIN_WINDIB
  &driver_WINDIB,
#endif/*AUDIOIN_WINDIB*/
NULL
};

int null_init(void)
{
  return(0);
}

void null_quit(void)
{
  SDL_CloseAudioIn();
  return;
}

int AudioSpecSize(const SDL_AudioSpec *spec)
{
  if(spec==NULL)
    return(0);

  switch(spec->format)
  {
  case AUDIO_U8:
  case AUDIO_S8:
    return(spec->channels*spec->samples);
    break;
  case AUDIO_S16LSB:
  case AUDIO_S16MSB:
  case AUDIO_U16LSB:
  case AUDIO_U16MSB:
    return(spec->channels*spec->samples*2);
    break;
  default:
    return(0);
  }
}

int AudioSpecTicks(const SDL_AudioSpec *spec)
{
  if(spec==NULL)
    return(0);

  return((spec->samples*1000)/spec->freq);
}

int AudioSpecValid(const SDL_AudioSpec *spec)
{
  int n;
  static const int validfreqs[]={
      6000, 8000, 11025, 22050, 32000,
     44100, 48000,64000, 88200, 96000,
    192000,-1};  

  if(spec==NULL)
    return(0);

  n=0;
  while(validfreqs[n]!=spec->freq)
  {
    if(validfreqs[n]==-1)
    {
      SDL_SetError("Invalid frequency: %d\n",spec->freq);
      return(0);
    }
    n++;
  }

  if((spec->channels<1)||(spec->channels>2))
  {
    SDL_SetError("Invalid # of channels: %d\n",spec->channels);
    return(0);
  }

  switch(spec->format)
  {
  case AUDIO_U8:
  case AUDIO_S8:
  case AUDIO_U16LSB:
  case AUDIO_U16MSB:
  case AUDIO_S16LSB:
  case AUDIO_S16MSB:
    break;
  default:
    SDL_SetError("Unknown audio format 0x%08x\n",spec->format);
    return(0);
  }

  if(spec->samples<=0)
  {
    SDL_SetError("Invalid # of samples: %d\n",spec->samples);
    return(0);
  }  
}

