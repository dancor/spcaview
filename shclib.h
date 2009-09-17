/*
 *  shcodec ;) version 1.0.1
 *  Copyright (C) 1998-2002 Alexander Simakov
 *  April 2002
 *
 *  This software may be used freely for any purpose. However, when
 *  distributed, the original source must be clearly stated, and,
 *  when the source code is distributed, the copyright notice must
 *  be retained and any alterations in the code must be clearly marked.
 *  No warranty is given regarding the quality of this software.
 *
 *  internet: http://www.webcenter.ru/~xander
 *  e-mail: xander@online.ru
 */

#ifndef SHCLIB_INCLUDED
#define SHCLIB_INCLUDED

typedef unsigned char uchar;
typedef unsigned int  uint;

/*
 *  Encode bSize bytes from iBlock to oBlock and
 *  return encoded stream size(if success) or
 *  0 (if fail). oBlock size must be at least
 *  iBlock size + 256 (for code tree stuff)
 */

int sh_EncodeBlock(uchar *iBlock,
                   uchar *oBlock,
                   int bSize);

/*
 *  Decode bSize bytes from iBlock to oBlock and return
 *  decoded stream size(if success) or 0(if fail).
 *  Note: decoded message must fits in oBlock. To ensure
 *  that oBlock has enought room for a data, check first
 *  DWORD in the iBlock. First DWORD in iBlock is decoded
 *  stream size(i.e. length of original message).
 */

int sh_DecodeBlock(uchar *iBlock,
                   uchar *oBlock,
                   int bSize);

#endif /* SHCLIB_INCLUDED */
