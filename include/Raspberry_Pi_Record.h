#ifndef Raspberry_Pi_Record_H
#define Raspberry_Pi_Record_H
#include <alsa/asoundlib.h>

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>

//pulseaudio play
#include <pulse/simple.h>
#include <pulse/error.h>

enum PcmType
{
	PCM_TYPE_ALSA=0,
	PCM_TYPE_PULSEaUDIO,
};

struct Stream_Record_Info
{
	unsigned int Channel;
	snd_pcm_uframes_t Frames;
	unsigned int Rate;
	snd_pcm_format_t Format;
	enum PcmType pcm_type;
};
snd_pcm_t * Raspberry_Pi_Record_Init(char *dev,struct Stream_Record_Info* Stream);

snd_pcm_t * Raspberry_Pi_Playback_Init(char *dev,struct Stream_Record_Info* Stream);

bool Raspberry_Pi_Playback_Send(snd_pcm_t * handle,unsigned char* buff,int len);

void Raspberry_Pi_Playback_Close(snd_pcm_t * handle);


int PulseAudioInit(pa_simple** handle,pa_sample_spec ss);

int PulseAudioPlay(pa_simple* handle, unsigned char* buff, int len);

void PulseAudioClose(pa_simple* handle);


#endif 
