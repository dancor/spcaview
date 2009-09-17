/***************************************************************************/
/*         Differential Pixels Static Huffmann Encoder Decoder             */
/***************************************************************************/
/*                                                                          #
# 		Copyright (C) 2004 Michel Xhaard                            #
# shclib   Copyright (C) 1998-2002 Alexander Simakov                        #
#***************************************************************************#
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

#include <string.h>
#include "shclib.h"
#include "dpsh.h"

void dpsh_yuv_encode(unsigned char *src, char *dst,int *encodesize)
{
	int i,sizein ;

	for (i=*encodesize; i > 0; i--)
	src[i]-=src[i-1];
	sizein=sh_EncodeBlock(src, dst,*encodesize );
	*encodesize=sizein;
	
}

void dpsh_yuv_decode(unsigned char *src, char *dst, int *decodesize)
{
	int i , dec_size;

	dec_size=sh_DecodeBlock(src, dst, *decodesize);
	for (i=1; i< dec_size; i++)
	dst[i] +=dst [i-1];
	*decodesize=dec_size;
	
}
