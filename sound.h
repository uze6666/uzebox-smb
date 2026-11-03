/* sound.h -- the sound, effects and music (sound.c, on apu.c's registers): see smb.h */
#pragma once
void sound_Init(void);
void SoundEngine(void);             /* once a frame, where the NMI called it: plays the queues */
