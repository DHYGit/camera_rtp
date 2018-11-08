#include "libpcm_aac.h"
#include "Raspberry_Pi_Record.h"
#include <string.h>
#include <string>
#include <queue>
#include <pulse/pulseaudio.h>

extern void LOG(bool flag, std::string str);

static pa_sample_spec sample_spec = {
    .format = PA_SAMPLE_S16LE,
    .rate = 16000,
    .channels = 1
};

static pa_stream *stream = NULL;
static pa_context *context;
static size_t latency = 0, process_time = 0;
static int32_t latency_msec = 1, process_time_msec = 1;

#define CLEAR_LINE "\n"
#define _(x) x

#define pa_memzero(x, l) (memset((x), 0, (l)))
#define pa_zero(x) (pa_memzero(&(x), sizeof(x)))

int fdout;
//char *fname = "tmp.s16";
int verbose = 1;
int ret;
static pa_stream_flags_t flags;

Alsa2PCM::Alsa2PCM()
:pStream_Record_Info(NULL)
,record_handle(NULL)
,Rec_Buff(NULL)
,Rec_Buff_Size(0)
,m_pulse_handle(NULL)
{
}

Alsa2PCM::~Alsa2PCM()
{
}

void stream_state_callback(pa_stream *s, void *userdata)
{
    assert(s);
    int status = pa_stream_get_state(s);
    printf("get status:%d\n", status);
    switch(status){
        case PA_STREAM_CREATING:
            printf("Creating stream\n");
            //fdout = creat(fname, 0711);
            break;
        case PA_STREAM_TERMINATED:
            //close(fdout);
            break;
        case PA_STREAM_READY:
            if(verbose)
            {
                const pa_buffer_attr *a;
                char cmt[PA_CHANNEL_MAP_SNPRINT_MAX];
                char sst[PA_SAMPLE_SPEC_SNPRINT_MAX];
                printf("Stream successfully created");
                if(!(a = pa_stream_get_buffer_attr(s)))
                {
                    printf("pa_stream_get_buffer_attr() failed:%s\n", pa_strerror(pa_context_errno(pa_stream_get_context(s))));
                }else{
                    printf("Buffer metrics:maxlength=%u, fragsize=%u", a->maxlength, a->fragsize);
                }
                printf("Connected to device %s (%u, %s suspended)\n",
                        pa_stream_get_device_name(s),
                        pa_stream_get_device_index(s),
                        pa_stream_is_suspended(s)?"":"not ");
            }
            break;
        case PA_STREAM_FAILED:
        default:
            printf("Stream error:%s \n", pa_strerror(pa_context_errno(pa_stream_get_context(s))));
            exit(1);
    }
}

std::queue <PCMDataStruct> *pcm_cache_queue;
pthread_mutex_t pcm_cache_lock;

static struct timeval tv1, tv2;
unsigned char pcmbuf[40 * 1024];
unsigned int pcmoffset = 0;
unsigned int pcmlength = 0;

static void stream_read_callback(pa_stream *s, size_t length, void *userdata)
{
    std::string function = __FUNCTION__;
    assert(s);
    assert(length > 0);
    gettimeofday(&tv2, NULL);
    tv1 = tv2;
    int i = 0;
    char *ptr = NULL;
    PCMDataStruct pcm_data;
    while(pa_stream_readable_size(s) > 0){
        //printf("pcm date length:%d \n", pa_stream_readable_size(s));
        if(pa_stream_readable_size(s) > 0)
        {
            const void *data;
            size_t length;
            if(pa_stream_peek(s, &data, &length) < 0){
                fprintf(stderr, "Read failed\n");
                LOG(false, function + " Read failed");
                return;
            }
            
            if(pcmoffset < 2 * 1024)
            {
                memcpy(pcmbuf + pcmoffset, data, length);
                pcmoffset += length;
            }else{
                ptr = (char*)pcmbuf;
                if(pcmoffset >= 2048){
                    pthread_mutex_lock(&pcm_cache_lock);
                    memcpy(pcm_data.data, ptr, 2048);
                    pcm_data.len = 2048;
                    pcm_cache_queue->push(pcm_data);
                    pcmoffset -= 2048;
                    ptr += 2048;
                    pthread_mutex_unlock(&pcm_cache_lock);
                }
            }
            pa_stream_drop(s);
        }
    }
}

void state_cb(pa_context *c, void *userdata)
{
    pa_context_state_t state;
    int *pa_ready = (int*)userdata;
    state = pa_context_get_state(c);
    switch(state)
    {
        case PA_CONTEXT_UNCONNECTED:
        case PA_CONTEXT_CONNECTING:
        case PA_CONTEXT_AUTHORIZING:
        case PA_CONTEXT_SETTING_NAME:
        default:
            break;
        case PA_CONTEXT_FAILED:
        case PA_CONTEXT_TERMINATED:
            *pa_ready = 2;
            break;
        case PA_CONTEXT_READY:
        {
            pa_buffer_attr buffer_attr;
            if(verbose){
                printf("Connection established %s \n", CLEAR_LINE);
            }
            if(!(stream = pa_stream_new(c, "JanCapture", &sample_spec, NULL)))
            {
                printf("pa_stream_new() failed:%s", pa_strerror(pa_context_errno(c)));
                exit(1);
            }
            //Watch for changes in the stream state to create the output file
            pa_stream_set_state_callback(stream, stream_state_callback, NULL);
            //Watch for changes in the stream's read state to write to the output file 
            pa_stream_set_read_callback(stream, stream_read_callback, NULL);
            //Set properties of the record buffer
            pa_zero(buffer_attr);
            buffer_attr.maxlength = (uint32_t)-1;
            buffer_attr.prebuf = (uint32_t)-1;
            buffer_attr.fragsize = buffer_attr.tlength = (uint32_t)-1;
            buffer_attr.minreq = (uint32_t)-1;
            if(latency_msec > 0){
                buffer_attr.fragsize = buffer_attr.tlength = pa_usec_to_bytes(latency_msec * PA_USEC_PER_MSEC, &sample_spec);
                flags = static_cast<pa_stream_flags_t>(PA_STREAM_ADJUST_LATENCY);
            }else if(latency > 0){
                buffer_attr.fragsize = buffer_attr.tlength = (uint32_t)latency;
                flags = PA_STREAM_ADJUST_LATENCY;
            }else{
                buffer_attr.fragsize = buffer_attr.tlength = (uint32_t)-1;
            }
            printf("buffer fragsize:%d\n", buffer_attr.fragsize);
            if(process_time_msec > 0){
                buffer_attr.minreq = pa_usec_to_bytes(process_time_msec * PA_USEC_PER_MSEC, &sample_spec);
            }else if(process_time > 0){
                buffer_attr.minreq = (uint32_t)process_time;
            }else{
                buffer_attr.minreq = (uint32_t)-1;
            }
            printf("buffer fragsize:%d \n", buffer_attr.fragsize);
            printf("buffer minreq:%d \n", buffer_attr.minreq);
            buffer_attr.minreq = 1024;
            buffer_attr.fragsize = 1024;
            if(pa_stream_connect_record(stream, NULL, &buffer_attr, flags) < 0){
                printf("pa_stream_connect_record() failed:%s\n", pa_strerror(pa_context_errno(c)));
                exit(1);
            }
        }
        break;
    }
}

void *thread_pa_async(void *arg){
    pa_mainloop *pa_ml;
    pa_mainloop_api *pa_mlapi;
    int ret = 0;
    //Create a mainloop API and connection to the default server
    pa_ml = pa_mainloop_new();
    pa_mlapi = pa_mainloop_get_api(pa_ml);
    context = pa_context_new(pa_mlapi, "test");

    pa_context_connect(context, NULL, (pa_context_flags_t)0, NULL);
    pa_context_set_state_callback(context, state_cb, NULL);
    sleep(3);
    if(pa_mainloop_run(pa_ml, &ret) < 0){
        printf("pa_mainloop_run() failed");
        exit(1);
    }
}

int Alsa2PCM::Init(Stream_Record_Info stream_info)
{
    std::string function = __FUNCTION__;
    m_pcm_type = stream_info.pcm_type;
    if(stream_info.pcm_type == PCM_TYPE_ALSA)
    {
	    if(!pStream_Record_Info)
	    {
		pStream_Record_Info = (struct Stream_Record_Info *)malloc(sizeof(struct Stream_Record_Info));
		if(pStream_Record_Info == NULL){
			LOG(false, function + " pStream_Record_Info malloc failed");
			return -1;
		}
		memset(pStream_Record_Info, 0, sizeof(Stream_Record_Info));
		memcpy(pStream_Record_Info,&stream_info,sizeof(Stream_Record_Info));
		printf("channelid(%d) frame(%d) rate(%d) format(%d)\n",pStream_Record_Info->Channel,pStream_Record_Info->Frames,pStream_Record_Info->Rate,pStream_Record_Info->Format);
	    }
	    //std::string dev_name = "plughw:0,0";
	    //record_handle=Raspberry_Pi_Record_Init((char*)dev_name.c_str(),pStream_Record_Info);
	    record_handle=Raspberry_Pi_Record_Init("hw:1,0",pStream_Record_Info);
	    if(record_handle<0)
	    {
		    printf("Raspberry Pi Record Init Error!\n");
		    LOG(false, function + " Raspberry Pi Record Init Error");
		    return -1;
	    }
	    Rec_Buff_Size=pStream_Record_Info->Channel*pStream_Record_Info->Frames*2;
	    Rec_Buff=(unsigned char *)malloc(Rec_Buff_Size);
	    if(Rec_Buff == NULL){
		    LOG(false, function + " Rec_Buff malloc failed");
		    return -1;
	    }
	    memset(Rec_Buff,0,Rec_Buff_Size);
    }else if(stream_info.pcm_type == PCM_TYPE_PULSEaUDIO){
            pcm_cache_queue = new std::queue<PCMDataStruct>;
            pthread_mutex_init(&pcm_cache_lock, NULL);

	    static const pa_sample_spec ss = {
		    .format = PA_SAMPLE_S16LE,
		    .rate = 16000,
		    .channels = stream_info.Channel
	    };
	    int res = PulseAudioInit(&g_pulse_handle, ss);
	    if(res){
		    LOG(false, function + " pulse audio player Init failed");
		    return -1;
	    }
	    LOG(true, function + " pulse audio player Init success");
	    pthread_t pa_async_pid;
	    if (pthread_create(&pa_async_pid, NULL, thread_pa_async, NULL) < 0)
	    {
		    printf("Cannot create thread to async pulseaudio!\n");
		    sleep(10);
		    exit(1);
	    }
    }else{
	    return -1;
    }
    return 0;
}

int Alsa2PCM::UnInit(void)
{
	if(pStream_Record_Info)
	{
		free(pStream_Record_Info);
		pStream_Record_Info = NULL;
	}

	if(Rec_Buff)
	{
		free(Rec_Buff);
		Rec_Buff = NULL;
	}
	
}

Pcm2AAC::Pcm2AAC()
    :nSampleRate(16000)
    ,nChannels(1)
    ,nBit(16)
    ,nInputSamples(0)
    ,nMaxInputBytes(0)
    ,nMaxOutputBytes(0)
    ,hEncoder(NULL)
    ,pConfiguration(NULL)
    ,pbPCMBuffer(NULL)
    ,pbAACBuffer(NULL)
     ,easy_handle(NULL)
{

}

Pcm2AAC::~Pcm2AAC()
{

}

int Pcm2AAC::Init(OnMessages on_proc,void* args){
    hEncoder = faacEncOpen(nSampleRate, nChannels, &nInputSamples, &nMaxOutputBytes);
    printf("In %s nInputSamples %d nMaxOutputBytes %d +++++++++++++++++++++++++++\n", __FUNCTION__, nInputSamples, nMaxOutputBytes);
    nMaxInputBytes=nInputSamples*nBit/8;
    if(hEncoder == NULL)
    {
        printf("[ERROR] Failed to call faacEncOpen()\n");
        return -1;
    }
    pbPCMBuffer = new unsigned char[nMaxInputBytes];
    pbAACBuffer = new unsigned char[nMaxOutputBytes];
#if defined(FAAC_ENCODER_)
    pConfiguration = faacEncGetCurrentConfiguration(hEncoder);//ï¿½ï¿½È¡ï¿½ï¿½ï¿½Ã½á¹¹Ö¸ï¿½ï¿½
    pConfiguration->inputFormat = FAAC_INPUT_16BIT;
    pConfiguration->outputFormat= 1;
    pConfiguration->useTns=true;
    pConfiguration->useLfe=false;
    pConfiguration->aacObjectType=LOW;
    pConfiguration->mpegVersion = MPEG4;
    //pConfiguration->shortctl=SHORTCTL_NORMAL;
    pConfiguration->quantqual=50;
    pConfiguration->bandWidth=0;
    pConfiguration->bitRate=0;

    // (2.2) Set encoding configuration
    if(!faacEncSetConfiguration(hEncoder, pConfiguration))//ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½Ã£ï¿½ï¿½ï¿½ï¿½Ý²ï¿½Í¬ï¿½ï¿½ï¿½Ã£ï¿½ï¿½ï¿½Ê±ï¿½ï¿½Ò»ï¿½ï¿½
    {
        printf("faac encoder set configuration failed \n");
        return -2;
    }
#elif defined(EASY_AAC_ENCODER_)

    //easyaacencoder ini
    InitParam initParam;
    initParam.u32AudioSamplerate=16000;
    initParam.ucAudioChannel=1;
    initParam.u32PCMBitSize=16;
    initParam.ucAudioCodec = Law_PCM16;
    //initParam.g726param.ucRateBits=Rate16kBits;include

    easy_handle= Easy_AACEncoder_Init(initParam);
#endif
    m_mess_proc = on_proc;
    m_video_audio_proc = args;
    return 0;
}

int Pcm2AAC::Process(char* buff, int len){
    if(!buff || len<=0)
    {
        printf("----(%s)--(%s)----(%d)- Pcm2AAC::Process(%d)!--\n",__FILE__,__FUNCTION__,__LINE__,len);
        return -1;
    }

    int nRet = 0;
#if defined(FAAC_ENCODER_)
    nInputSamples = len/ (nBit / 8);

    nRet = faacEncEncode(hEncoder, (int*) buff, nInputSamples, pbAACBuffer, nMaxOutputBytes);
    if (nRet<1)
    {
        printf("-----faacEncEncode failed!\n");
        return -1;
    }
    m_mess_proc(pbAACBuffer,nRet,m_video_audio_proc);
#elif defined(EASY_AAC_ENCODER_)
    unsigned int out_len=0;
    nRet = Easy_AACEncoder_Encode(easy_handle, (unsigned char*)buff, len, pbAACBuffer, &out_len);
    if(nRet>0)
    {
        m_mess_proc(pbAACBuffer,out_len,m_video_audio_proc);endif
    }
#endif
    return 0;
}

int Pcm2AAC::UnInit()
{
#if defined(EASY_AAC_ENCODER_)
    if(easy_handle){
        Easy_AACEncoder_Release(easy_handle);
        easy_handle = NULL;
    }
#endif
    if(hEncoder){
        faacEncClose(hEncoder); 
        hEncoder = NULL;
    }
    if(pbPCMBuffer){
        delete[] pbPCMBuffer; 
        pbPCMBuffer = NULL;
    }
    if(pbAACBuffer){
        delete[] pbAACBuffer;
        pbAACBuffer = NULL;
    }
}

int Pcm2AAC::GetFaacEncDecoderSpecificInfo(unsigned char ** spec_info,unsigned long * len)
{
    faacEncGetDecoderSpecificInfo(hEncoder,spec_info,len);
    return 0;
}

