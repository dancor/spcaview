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

/**
 *  This header file helps decide what audio subsystems should be used based
 *  on the system's apparent operating system, etc.
 */

#ifndef __AUDIO_DETECT_H__
#define __AUDIO_DETECT_H__


#include "SDL_audioin_internal.h"

/**
 *  Linux defaults to using /dev/dsp etc.
 */
#ifdef __linux__
# define AUDIOIN_DEVDSP
  extern Sound_InputFunctions driver_DEVDSP;
#endif/*__linux__*/

/**
 *  Windows defaults to using the windows DIB system
 */
#ifdef WIN32
# define AUDIOIN_WINDIB
  extern Sound_InputFunctions driver_WINDIB;
#endif/*WIN32*/

#endif/*__AUDIO_DETECT_H__*/
