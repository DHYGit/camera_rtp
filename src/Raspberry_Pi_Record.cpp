#include"Raspberry_Pi_Record.h"

#include <alsa/asoundlib.h>
#include <stdio.h>

#define ALSA_PCM_NEW_HW_PARAMS_API  

snd_pcm_t * Raspberry_Pi_Record_Init(char *dev,struct Stream_Record_Info* Stream)
{
	int err;
	int dir;
	snd_pcm_hw_params_t *params;
	snd_pcm_sw_params_t *s_params;
	snd_pcm_t *handle;
	snd_pcm_uframes_t val=0;
	/* Open PCM device for recording (capture). */  
	err = snd_pcm_open(&handle, dev,SND_PCM_STREAM_CAPTURE, 0);  
	if (err < 0) 
	{  
		fprintf(stderr,"unable to open pcm device: %s/n",snd_strerror(err));  
		exit(1);  
	}
	snd_pcm_hw_params_alloca(&params); 
	snd_pcm_sw_params_alloca(&s_params); 
	snd_pcm_hw_params_any(handle, params);  
	/******************Set the desired hardware parameters. ********************/  
	/* Interleaved mode */  
	snd_pcm_hw_params_set_access(handle, params,SND_PCM_ACCESS_RW_INTERLEAVED);  
	/* Signed 16-bit little-endian format */  
	snd_pcm_hw_params_set_format(handle, params,Stream->Format);  
	/* Two channels (stereo) */  
	snd_pcm_hw_params_set_channels(handle, params, Stream->Channel);  
	/* 16000 bits/second sampling rate (CD quality) */  
	snd_pcm_hw_params_set_rate_near(handle, params,  &Stream->Rate, &dir);  
	/* Set period size to 160 frames. */  
	snd_pcm_hw_params_set_period_size_near(handle,  params, &Stream->Frames, &dir);  
	snd_pcm_sw_params_set_avail_min(handle,s_params,160);
	//err = snd_pcm_sw_params(handle, s_params);
	/* Write the parameters to the driver */  
	err = snd_pcm_hw_params(handle, params);  
	if (err < 0) 
	{  
		fprintf(stderr,  "unable to set hw parameters: %s\n", snd_strerror(err));  
		exit(1);  
	}  
	snd_pcm_hw_params_get_buffer_size(params,&val);
	printf("buffer_size=%d\n",(int)val);
	return handle;
}

snd_pcm_t * Raspberry_Pi_Playback_Init(char * dev,struct Stream_Record_Info * Stream)
{
#if 1
	int ret;  
    unsigned int val;  
    int dir=0;  
    char *buffer;  
    int size;  
    snd_pcm_uframes_t frames;  
    snd_pcm_uframes_t periodsize;  
    snd_pcm_t *playback_handle;//PCM�豸���pcm.h  
    snd_pcm_hw_params_t *hw_params;//Ӳ����Ϣ��PCM������  
      
    //1. ��PCM�����һ������Ϊ0��ζ�ű�׼����  
    ret = snd_pcm_open(&playback_handle, dev, SND_PCM_STREAM_PLAYBACK, 0);  
    if (ret < 0) {  
        printf("snd_pcm_open\n");  
       return NULL;
    }  

	//snd_pcm_nonblock(playback_handle,1);  SND_PCM_NONBLOCK
      
    //2. ����snd_pcm_hw_params_t�ṹ��  
    ret = snd_pcm_hw_params_malloc(&hw_params);  
    if (ret < 0) {  
        printf("snd_pcm_hw_params_malloc");  
        return NULL;  
    }  
    //3. ��ʼ��hw_params  
    ret = snd_pcm_hw_params_any(playback_handle, hw_params);  
    if (ret < 0) {  
        printf("snd_pcm_hw_params_any");  
        return NULL;  
    }  
    //4. ��ʼ������Ȩ��  
    ret = snd_pcm_hw_params_set_access(playback_handle, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED);  
    if (ret < 0) {  
        printf("snd_pcm_hw_params_set_access");  
       return NULL; 
    }  
    //5. ��ʼ��������ʽSND_PCM_FORMAT_U8,16λ  
    ret = snd_pcm_hw_params_set_format(playback_handle, hw_params, Stream->Format);  
    if (ret < 0) {  
        printf("snd_pcm_hw_params_set_format");  
       return NULL;
    }  
    //6. ���ò����ʣ����Ӳ����֧���������õĲ����ʣ���ʹ����ӽ���  
    //val = 44100,��Щ¼�����Ƶ�ʹ̶�Ϊ8KHz  
      
  
    val = Stream->Rate;  
    ret = snd_pcm_hw_params_set_rate_near(playback_handle, hw_params, &val, &dir);  
    if (ret < 0) {  
        printf("snd_pcm_hw_params_set_rate_near");  
        return NULL; 
    }  
    //7. ����ͨ������  
    ret = snd_pcm_hw_params_set_channels(playback_handle, hw_params, Stream->Channel);  
    if (ret < 0) {  
        perror("snd_pcm_hw_params_set_channels");  
        return NULL; 
    }  
      
    /* Set period size to  frames. */  
    //frames = Stream->Frames;
    frames=32;
    periodsize = frames * 2;  
	snd_pcm_uframes_t buffer_size = 4096;
    ret = snd_pcm_hw_params_set_buffer_size_near(playback_handle, hw_params, &buffer_size);  
    if (ret < 0)   
    {  
         printf("Unable to set buffer size %li : %s\n", frames * 2, snd_strerror(ret));  
           
    }  
          periodsize /= 2;  
  
    ret = snd_pcm_hw_params_set_period_size_near(playback_handle, hw_params, &periodsize, 0);  
    if (ret < 0)   
    {  
        printf("Unable to set period size %li : %s\n", periodsize,  snd_strerror(ret));  
    }  
                                    
    //8. ����hw_params  
    ret = snd_pcm_hw_params(playback_handle, hw_params);  
    if (ret < 0) {  
        printf("snd_pcm_hw_params");  
        return NULL;
    }  
      
     /* Use a buffer large enough to hold one period */  
    snd_pcm_hw_params_get_period_size(hw_params, &frames, &dir);  

	 return playback_handle;
#elif 0
	int err;
	int dir;
	snd_pcm_hw_params_t *params;
	snd_pcm_sw_params_t *s_params;
	snd_pcm_t *handle;
	snd_pcm_uframes_t val=0;
	
	/* Open PCM device for recording (capture). */  
	err = snd_pcm_open(&handle, dev,SND_PCM_STREAM_PLAYBACK, 0);  
	if (err < 0) 
	{  
		fprintf(stderr,"unable to open pcm device: %s/n",snd_strerror(err));  
		exit(1);  
	}
	snd_pcm_hw_params_alloca(&params); 
	snd_pcm_sw_params_alloca(&s_params); 
	snd_pcm_hw_params_any(handle, params);  

	/******************Set the desired hardware parameters. ********************/  

	/* Interleaved mode */  
	snd_pcm_hw_params_set_access(handle, params,SND_PCM_ACCESS_RW_INTERLEAVED);  

	/* Signed 16-bit little-endian format */  
	snd_pcm_hw_params_set_format(handle, params,Stream->Format);  

	/* Two channels (stereo) */  
	snd_pcm_hw_params_set_channels(handle, params, Stream->Channel);  

	/* 16000 bits/second sampling rate (CD quality) */  
	snd_pcm_hw_params_set_rate_near(handle, params,  &Stream->Rate, &dir);  

	/* Set period size to 160 frames. */  
	snd_pcm_hw_params_set_period_size_near(handle,  params, &Stream->Frames, &dir);  
	
	snd_pcm_sw_params_set_avail_min(handle,s_params,160);
	//err = snd_pcm_sw_params(handle, s_params);

	/* Write the parameters to the driver */  
	err = snd_pcm_hw_params(handle, params);  

	if (err < 0) 

	{  

		fprintf(stderr,  "unable to set hw parameters: %s\n", snd_strerror(err));  

		exit(1);  

	}  
	snd_pcm_hw_params_get_buffer_size(params,&val);
	printf("buffer_size=%d\n",(int)val);
	return handle;
#endif
	 
}

bool Raspberry_Pi_Playback_Send(snd_pcm_t * handle,unsigned char * buff,int len)
{
	if(!handle || !buff)
		return false;

	int frames = snd_pcm_writei(handle, buff, 2048);
     if (frames == -EPIPE)  
       {  
                  /* EPIPE means underrun */  
          fprintf(stderr, "underrun occurred\n");  
                  //���Ӳ���������ã�ʹ�豸׼����  
           snd_pcm_prepare(handle);
		   //snd_pcm_writei(handle, buff, len);
      }else if(frames == -EAGAIN){

		snd_pcm_wait(handle,1000);
		}
      else if (frames < 0)   
      {  
          fprintf(stderr,  
                      "error from writei: %s\n",  
                      snd_strerror(frames));  
       }
	
	return true;
}

void Raspberry_Pi_Playback_Close(snd_pcm_t * handle)
{
	if(handle)
		snd_pcm_close(handle);	
}

int  PulseAudioInit(pa_simple** handle, pa_sample_spec ss)
{
	int error =0;
	pa_sample_spec ss_init;
	memcpy(&ss_init,&ss,sizeof(pa_sample_spec));
	printf("----(%s)--(%s)----(%d)----!---\n",__FILE__,__FUNCTION__,__LINE__);
	pa_simple *s = pa_simple_new(NULL, "PulseAudio", PA_STREAM_PLAYBACK, NULL, "playback", &ss, NULL, NULL, &error);
	 /* Create a new playback stream */
	if (!s) {
        fprintf(stderr, __FILE__": pa_simple_new() failed: %s\n", pa_strerror(error));
		printf("----(%s)--(%s)----(%d)----!---\n",__FILE__,__FUNCTION__,__LINE__);
		return -1;
    }
	printf("----(%s)--(%s)----(%d)----!---\n",__FILE__,__FUNCTION__,__LINE__);
	*handle =s; 
	return 0;
}

int PulseAudioPlay(pa_simple* handle, unsigned char* buff, int len)
{
	  int error;
	  if (pa_simple_write(handle, buff, (size_t) len, &error) < 0) {
            fprintf(stderr, __FILE__": pa_simple_write() failed: %s\n", pa_strerror(error));
           return -1;
        }

#if 0
	/* Make sure that every single sample was played */
    if (pa_simple_drain(handle, &error) < 0) {
        fprintf(stderr, __FILE__": pa_simple_drain() failed: %s\n", pa_strerror(error));
       return -1;
    }
#endif
return 0;
}

void PulseAudioClose(pa_simple* handle)
{
	if (handle)
        pa_simple_free(handle);
}
