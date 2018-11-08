#ifndef __LIBAAC_RTMP__
#define __LIBAAC_RTMP__
#include<alsa/asoundlib.h>
#include<stdio.h>
#include<pthread.h>
#include<unistd.h>
#include <semaphore.h>
#include <string.h>
#include <unistd.h>			//Used for UART
#include <fcntl.h>			//Used for UART
#include <termios.h>		//Used for UART
#include "Raspberry_Pi_Record.h"
//faac
#include <time.h>
#include "faac.h"
#if 0
#ifndef EASY_AAC_ENCODER_
#define EASY_AAC_ENCODER_ 1
#endif
#elif 1
#ifndef FAAC_ENCODER_ 
#define FAAC_ENCODER_ 1
#endif
#endif

#define EasyAACEncoder_Handle void*

struct PCMDataStruct{
    char data[2048];                                             
    int len;
};

class Alsa2PCM
{
public:
	Alsa2PCM();
	~Alsa2PCM();
public:
	typedef int (*OnMessages)(unsigned char*buff,int len, void* args);
public:
	int Init(Stream_Record_Info stream_info);
	int Process();
	int UnInit(void);
	pa_simple *					m_pulse_handle;

    pa_simple *                 g_pulse_handle;
	struct Stream_Record_Info*	pStream_Record_Info;
	snd_pcm_t*					record_handle;
	unsigned char* 						Rec_Buff;
	int 						Rec_Buff_Size;
	OnMessages					m_on_proc;
	PcmType						m_pcm_type;
	void*						m_video_audio_proc;
};


class Pcm2AAC
{
    public:
        Pcm2AAC();
        ~Pcm2AAC();

    public:
        typedef int (*OnMessages)(unsigned char*buff,unsigned long len, void* args);

    public:
        int Init(OnMessages on_proc,void*args);
        int Process(char* buff, int len);
        int UnInit();
        int GetFaacEncDecoderSpecificInfo(unsigned char** spec_info, unsigned long* len);

        unsigned long       nSampleRate;  // ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½
        unsigned int        nChannels;         // ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½
        unsigned int        nBit;             // ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½Î»ï¿½ï¿½
        unsigned long       nInputSamples; //ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½
        unsigned long       nMaxOutputBytes; //ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½Õ¼ï¿½
        unsigned long       nMaxInputBytes;     //ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½Ö½ï¿½
        faacEncHandle       hEncoder; //aacï¿½ï¿½ï¿½
        faacEncConfigurationPtr pConfiguration;//aacï¿½ï¿½ï¿½ï¿½Ö¸ï¿½ï¿½ 
        unsigned char*      pbPCMBuffer;
        unsigned char*      pbAACBuffer;
        OnMessages          m_mess_proc;
        EasyAACEncoder_Handle easy_handle;
        void               *m_video_audio_proc;

};


#endif
