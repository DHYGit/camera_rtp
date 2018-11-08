#include "ProControl.h"
#include "pro_rtp.h"
#include "libpcm_aac.h"
#include "spdlog/spdlog.h"
#include "ConsumerThreadTool.h"
#include <NvApplicationProfiler.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <arpa/inet.h>


using namespace ArgusSamples;
namespace spd = spdlog;

std::string get_selfpath() {
    char buff[1024];
    ssize_t len = ::readlink("/proc/self/exe", buff, sizeof(buff)-1);
    if (len != -1) {
        std::string str(buff);
        return str.substr(0, str.rfind('/') + 1);
    }
}

std::string selfpath = get_selfpath() + "/log/camera_push.log";
auto logger = spd::daily_logger_mt("daily_logger", selfpath.c_str(), 0, 0);
pthread_mutex_t log_lock;

void LOG(bool flag, std::string str)
{
    pthread_mutex_lock(&log_lock);
    if (flag)
    {
        logger->info(str);
    }
    else
    {
        logger->error(str + " error");
    }
    pthread_mutex_unlock(&log_lock);
}

std::queue <MediaDataStruct> *video_buf_queue;
pthread_mutex_t video_buf_queue_lock;

std::queue <MediaDataStruct> *audio_buf_queue_faac;
pthread_mutex_t audio_buf_queue_lock_faac;

Alsa2PCM pcm_obj;
Pcm2AAC  aac_obj;
bool push_flag = false;

void *fps_function(void *ptr);
void *FAACAudioEncFun(void *ptr);
int PCM2AACCallback(unsigned char*buff,unsigned long len, void* args);


int main(int argc, char *argv[])
{
    std::string function = __FUNCTION__;
    pthread_mutex_init(&log_lock, NULL);
    spd::set_level(spd::level::trace);
    spd::set_pattern("[%l][%H:%M:%S:%f] %v");
    logger->flush_on(spd::level::trace);
    LOG(true, "Progress start");
    int ret = -1; 
#if VIDEO_STATUS
    video_buf_queue = new std::queue<MediaDataStruct>;
    LOG(NULL != video_buf_queue, function + " Init video_buf_queue");
    pthread_mutex_init(&video_buf_queue_lock, NULL);
#endif

#if AUDIO_STATUS
    Stream_Record_Info s_info;
    s_info.Channel = 1;
    s_info.Frames = 160;
    s_info.Rate = 16000;
    s_info.pcm_type=PCM_TYPE_PULSEaUDIO;
    s_info.Format  = SND_PCM_FORMAT_S16_LE;
    
    ret = pcm_obj.Init(s_info);
    if(ret == 0){
        printf("pcm_obj init success \n");
        LOG(true, function + " pcm_obj init success");
    }else{
        printf("pcm_obj init failed \n");
        LOG(false, function + " pcm_obj init failed");
        return ret;
    } 
    
    aac_obj.nSampleRate = s_info.Rate;
    aac_obj.nChannels = 1;
    aac_obj.nBit = 16;
    aac_obj.nInputSamples = 0;
    aac_obj.nMaxInputBytes = 0;
    aac_obj.nMaxOutputBytes = 0;
    aac_obj.hEncoder = NULL;
    aac_obj.pConfiguration = NULL;
    aac_obj.pbPCMBuffer = NULL;
    aac_obj.pbAACBuffer = NULL;
    aac_obj.easy_handle = NULL;
    ret = aac_obj.Init(PCM2AACCallback,NULL);
    if(ret == 0){
        printf("aac_obj init success \n");
        LOG(true, function + " aac_obj init success");
    }else{
        printf("aac_obj init failed \n");
        LOG(false, function + " aac_obj init failed");
        return ret;
    }

    audio_buf_queue_faac = new std::queue<MediaDataStruct>;
    LOG(NULL != audio_buf_queue_faac, function + " Init audio_buf_queue_faac");
    pthread_mutex_init(&audio_buf_queue_lock_faac, NULL);

    pthread_t faac_thread;
    ret = pthread_create(&faac_thread, NULL, FAACAudioEncFun, NULL);
    if(ret){
        printf("----(%s)--(%s)----(%d)--phtread_create FAACAudioEncFun failed!--\n",__FILE__,__FUNCTION__,__LINE__);
        LOG(false, function + " create FAACAudioEncFun failed");
        return ret;
    }
    LOG(true, function + " create faac encode aac thread success");
#endif    
    ret = -1;    

    pthread_t thread_sock;
    ret = pthread_create(&thread_sock, NULL, UDPSOCKFun, NULL);
    if(ret){
        printf("----(%s)--(%s)----(%d)--phtread_create UDPSOCKFun failed!--\n",__FILE__,__FUNCTION__,__LINE__);
        LOG(false, function + " create UDPSOCKFun failed");
        return ret;
    }
    LOG(true, function + " create UDPSOCKFun thread success");
    pthread_t thread_jrtp;
    ret = pthread_create(&thread_sock, NULL, JrtpFun, NULL);
    if(ret){
        printf("----(%s)--(%s)----(%d)--phtread_create JrtpFun failed!--\n",__FILE__,__FUNCTION__,__LINE__);
        LOG(false, function + " create JrtpFun failed");
        return ret;
    }
    LOG(true, function + " create JrtpFun thread success");
     
    int num = atoi(argv[1]);
    NvApplicationProfiler &profiler = NvApplicationProfiler::getProfilerInstance();
    pthread_t fps_thread;
    pthread_create(&fps_thread, NULL, fps_function, NULL);
    if (!execute(num))
        return EXIT_FAILURE;

    profiler.stop();
    profiler.printProfilerData(std::cout);

    return EXIT_SUCCESS;
}

#if AUDIO_STATUS
extern std::queue <PCMDataStruct> *pcm_cache_queue;
extern pthread_mutex_t pcm_cache_lock;

void *FAACAudioEncFun(void *ptr){
    std::string function = __FUNCTION__;
    int duration = 1000 * 1024 / AUDIO_RATE;
    struct timeval enc_tv1 = {0, 0};
    struct timeval enc_tv2 = {0, 0};
    int ret = -1;
    LOG(true, "In " + function);
    while(1){
        if(!push_flag){
            usleep(1000 * 100);
            continue;
        }
        if(pcm_cache_queue->size() <= 0){
            usleep(1000);
            continue;
        }
        pthread_mutex_lock(&pcm_cache_lock);
        PCMDataStruct pcm_data = pcm_cache_queue->front();
        /*
         *         printf("audio_data len is %d \n", audio_data.len);
         *                 for(int i = 0; i < 2048; i++){
         *                             printf("%2X ", audio_data.data[i]);
         *                                     }
         *                                             printf("\n");*/
        pcm_cache_queue->pop();
        pthread_mutex_unlock(&pcm_cache_lock);
        ret = aac_obj.Process(pcm_data.data, pcm_data.len);
        if(ret < 0){
            LOG(false, function + " encode aac data failed");
            continue;
        }
        gettimeofday(&enc_tv2, NULL);
        long t = (enc_tv2.tv_sec * 1000 * 1000 + enc_tv2.tv_usec) - (enc_tv1.tv_sec * 1000 * 1000 + enc_tv1.tv_usec);
        if(t + 1000 < duration * 1000){
            usleep(duration * 1000 - t - 1000);
            //printf("In %s sleep %ld us \n", __FUNCTION__, duration * 1000 - t);
        }
        gettimeofday(&enc_tv1, NULL);
    }
}

extern int sockfd;
extern struct sockaddr_in remote_addr;
unsigned char *audio_data = (unsigned char *)malloc(BUFFLENGTH);
int PCM2AACCallback(unsigned char*buff,unsigned long len, void* args)
{
    std::string function = __FUNCTION__;
    if(push_flag){
        /*MediaDataStruct media_data;
	media_data.len = len;
	media_data.index = AUDIOINDEX;
	media_data.buff = (unsigned char*)malloc(len);
	memset(media_data.buff, 0, len);
	memcpy(media_data.buff, buff, len);
	gettimeofday(&media_data.tv, NULL);
	pthread_mutex_lock(&audio_buf_queue_lock_faac);
	audio_buf_queue_faac->push(media_data);
	pthread_mutex_unlock(&audio_buf_queue_lock_faac);*/
        int ret = -1;
        if(len < SOCKLENGTH && len > 0){
            memset(audio_data, 0, BUFFLENGTH);
            audio_data[0] = 0x01;//audio data
            audio_data[1] = 0x01;//total count
            audio_data[2] = 0x01;//current count
            memcpy(audio_data + 3, buff, len);
            ret = sendto(sockfd, audio_data, len + 3, 0, (struct sockaddr *)&remote_addr, sizeof(struct sockaddr));
            if(ret >= 0){
                printf("send a single packet to server, len = %d \n", ret);
            }else{
                printf("send single packet to server error, %d \n", ret);
                LOG(false, function + " send single packet to server failed");
            }
        }else if(len > 0 && len >= SOCKLENGTH){
            int count = len / SOCKLENGTH;
            int endLen = len % SOCKLENGTH;
            int i = 0;
            do{
                memset(audio_data, 0, BUFFLENGTH);
                audio_data[0] = 0x01;//audio data
                audio_data[1] = count + 1;//total count
                audio_data[2] = ++i;//current count
                memcpy(audio_data + 3  , buff + SOCKLENGTH * (i-1), SOCKLENGTH);
                ret = sendto(sockfd, audio_data, SOCKLENGTH + 3, 0, (struct sockaddr *)&remote_addr, sizeof(struct sockaddr));
                if(ret >= 0){
                    printf("send %d packet to server, len = %d \n", i, ret);
                }else{
                    LOG(false, function + " udp send data failed");
                    printf("send %d packet to server error, %d \n", i, ret);
                }
                usleep(1000);
            }while(i < count);
            if(endLen > 0){
                memset(audio_data, 0, BUFFLENGTH);
                audio_data[0] = 0x01;//audio data
                audio_data[1] = count + 1;//total conut
                audio_data[2] = ++i; //current count
                memcpy(audio_data + 3, buff + SOCKLENGTH * (i -1), endLen);
                ret = sendto(sockfd, audio_data, endLen + 3, 0, (struct sockaddr *)&remote_addr, sizeof(struct sockaddr));
                if(ret >= 0){
                    printf("send %d packet to server, len = %d \n", i, ret);
                }else{
                    LOG(false, function + " udp send data failed");
                    printf("send %d packet to server error, %d \n", i, ret);
                }
            }
        }       
    }
    return 0;
}
#endif

extern int fps;
extern int capture_count;
void *fps_function(void *ptr){
    while(1){
        fps = 0;
        capture_count = 0;
        usleep(1000 * 1000);
        printf("capture count %d video output frame fps : %d fps\n", capture_count, fps);
    }
}

