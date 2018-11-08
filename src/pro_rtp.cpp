#include "pro_rtp.h"
#include "ProControl.h"
#include "jrtplib3/rtpsession.h"
#include "jrtplib3/rtpsessionparams.h"
#include "jrtplib3/rtpudpv4transmitter.h"
#include "jrtplib3/rtpipv4address.h"
#include "jrtplib3/rtperrors.h"
#include <iostream>
#include <string.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <queue>

using namespace std;
using namespace jrtplib;

extern void LOG(bool flag, std::string str);
extern bool push_flag;
int live_status = 0;
int sleep_time = 1000 * 500;


RTPSession session;
RTPSessionParams sessionparams;
RTPUDPv4TransmissionParams transparams;

int InitJrtp(struct sockaddr_in addr_in, int dstPort){
    string function = __FUNCTION__;
    printf("recvIP: %s ++++++++++++++++++++++++++++++++++++++\n", inet_ntoa(addr_in.sin_addr));
    
    sessionparams.SetOwnTimestampUnit(1.0 / 90000);
    sessionparams.SetAcceptOwnPackets(true);
    transparams.SetPortbase(7000);
    int ret = session.Create(sessionparams, &transparams);
    if(ret < 0){
	string msg = RTPGetErrorString(ret);
	printf("Error string: %s \n", msg.c_str());
	LOG(false, function + " jrtp create session failed " + msg);
	exit(-1);
    }
    printf("create rtp session success\n");
    uint32_t dstip = ntohl(inet_addr(inet_ntoa(addr_in.sin_addr)));
    RTPIPv4Address addr(dstip, dstPort);
    ret = session.AddDestination(addr);
    if(ret < 0){
        string msg = RTPGetErrorString(ret);
	printf("Error string:%s \n", msg.c_str());
	LOG(false, function + " jrtp add addr failed " + msg);
	exit(-1);
    }
    printf("session add addr success\n");
    session.SetDefaultPayloadType(96);//H264
    session.SetDefaultMark(false);
    session.SetTimestampUnit(1.0/90000.0);
    session.SetDefaultTimestampIncrement(3600);
    return 0;
}

void *UDPSOCKFun(void *ptr){
    std::string function = __FUNCTION__;
    int sock;
    struct sockaddr_in servaddr;
    struct sockaddr_in peeraddr;
    socklen_t peerlen;
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(7088);
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    char recvbuf[1024] = {0};
    peerlen = sizeof(peeraddr);
    if ((sock = socket(PF_INET, SOCK_DGRAM, 0)) < 0)
    {
        printf("socket error \n");
        return NULL;
    }
    printf("UDP:lesten port 7088\n");
    if (bind(sock, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0)
    {
        printf("bind error \n");
        return NULL;
    }
    int ret = 0;
    while(1){
        memset(recvbuf, 0, sizeof(recvbuf));
        ret = recvfrom(sock, recvbuf, sizeof(recvbuf), 0, (struct sockaddr *)&peeraddr, &peerlen);
        if(ret <= 0){
            usleep(1000);
            continue;
        }
        printf("recvbuf:\n");
        if(recvbuf[0] == 0x01){
            ret = InitJrtp(peeraddr, 7078);
            LOG(ret == 0, function + " Init UDP");
            push_flag = true;
        }else if(recvbuf[0] == 0x02){
            push_flag = false;
        }
        usleep(1000);
    }
}

extern std::queue <MediaDataStruct> *video_buf_queue;
extern pthread_mutex_t video_buf_queue_lock;

void *JrtpFun(void *ptr){
    string function = __FUNCTION__;
    int wait_num = 0;
    int ret = -1;
    char sendbuf[MAX_RTP_PKT_LENGTH];
    LOG(true, "In " + function);
    while(1){
       if(!push_flag){
		usleep(1000);
                if(wait_num % 1000 == 0 && wait_num > 0){
			LOG(true, function + " wait push");
		}
		wait_num++;               
		while(video_buf_queue->size() > 0){
			pthread_mutex_lock(&video_buf_queue_lock);
			MediaDataStruct media_data = video_buf_queue->front();
			video_buf_queue->pop();
			pthread_mutex_unlock(&video_buf_queue_lock);
			free(media_data.buff);
			media_data.buff = NULL;
		}
		continue;    
        }
        if(video_buf_queue->size() <= 0){
            usleep(1000);
            if(wait_num % 1000 == 0 && wait_num > 0){
            LOG(true, function + " no video data");
            wait_num = 0;
            }
            wait_num++;
            continue;
        }        
        wait_num = 0;
        pthread_mutex_lock(&video_buf_queue_lock);
        MediaDataStruct media_data = video_buf_queue->front();
        video_buf_queue->pop();
        pthread_mutex_unlock(&video_buf_queue_lock);
	if(media_data.len < MAX_RTP_PKT_LENGTH){
		memset(sendbuf, 0, MAX_RTP_PKT_LENGTH);
		memcpy(sendbuf, media_data.buff, media_data.len);
		ret = session.SendPacket((void*)sendbuf, media_data.len);
		if(ret < 0) {
			string msg = RTPGetErrorString(ret);
			printf("Error string:%s \n", msg.c_str());
			LOG(false, function + " jrtp send packet failed " + msg);
		}	
	}else if(media_data.len > SOCKLENGTH){
		session.SetDefaultMark(false);
		int k = 0, l = 0;
		k = media_data.len / MAX_RTP_PKT_LENGTH;
		l = media_data.len % MAX_RTP_PKT_LENGTH;
		int t = 0;
		char nalHeader = media_data.buff[0];
		while(t < k || (t == k && l > 0)){
			if((0 == t) || (t < k && 0 != t)){
				memset(sendbuf, 0, MAX_RTP_PKT_LENGTH);
				memcpy(sendbuf, media_data.buff+ t * MAX_RTP_PKT_LENGTH, MAX_RTP_PKT_LENGTH);
				ret = session.SendPacket((void*)sendbuf, MAX_RTP_PKT_LENGTH);
				if(ret < 0) {
					string msg = RTPGetErrorString(ret);
					printf("Error string:%s \n", msg.c_str());
					LOG(false, function + " jrtp send packet failed " + msg);
				}
				t++;

			}else if((k == t && l > 0) || (t == (k - 1) && l == 0)){
				session.SetDefaultMark(true);
				int iSendLen;
				if(l > 0){
					iSendLen = media_data.len - t * MAX_RTP_PKT_LENGTH;
				}else{
					iSendLen = MAX_RTP_PKT_LENGTH;
				}
				memset(sendbuf, 0, MAX_RTP_PKT_LENGTH);
				memcpy(sendbuf, media_data.buff + t * MAX_RTP_PKT_LENGTH, iSendLen);
				ret = session.SendPacket((void*)sendbuf, iSendLen);
				if(ret < 0) {
                                        string msg = RTPGetErrorString(ret);
                                        printf("Error string:%s \n", msg.c_str());
                                        LOG(false, function + " jrtp send packet failed " + msg);
                                }
                                t++;
			}
		}
	}
        if(media_data.buff){
		free(media_data.buff);
		media_data.buff = NULL;
	}	
    }
}

