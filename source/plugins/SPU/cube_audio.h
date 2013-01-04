//cube_audio.h AUDIO output via libOGC

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/


#ifndef _CUBE_AUDIO_H
#define _CUBE_AUDIO_H

#ifdef __cplusplus
extern "C" {
#endif

void SetupSound(void);
void RemoveSound(void);
unsigned long SoundGetBytesBuffered(void);
void SoundFeedStreamData(unsigned char* pSound,long lBytes);
void PEOPS_SPUplayCDDAchannel(short* pcm, int nbytes);

#ifdef __cplusplus
} // extern "C" 
#endif

#endif // _CUBE_AUDIO_H
