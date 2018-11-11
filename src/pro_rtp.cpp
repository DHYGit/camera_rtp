#include "pro_rtp.h"
#include "ProControl.h"
#include "jrtplib3/rtpsession.h"
#include "jrtplib3/rtpsessionparams.h"
#include "jrtplib3/rtpudpv4transmitter.h"
#include "jrtplib3/rtpipv4address.h"
#include "jrtplib3/rtptimeutilities.h"
#include "jrtplib3/rtppacket.h"
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
    servaddr.sin_port = htons(SRC_PORT);
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    char recvbuf[1024] = {0};
    peerlen = sizeof(peeraddr);
    if ((sock = socket(PF_INET, SOCK_DGRAM, 0)) < 0)
    {
        printf("socket error \n");
        return NULL;
    }
    printf("UDP:lesten port %d\n", SRC_PORT);
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
        if(recvbuf[0] == 0x01){
            ret = InitJrtp(peeraddr, DST_PORT);
            LOG(ret == 0, function + " Init Jrtp");
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
    char* nalu_payload;
    unsigned int timestamp_increse=0,ts_current=0;
    RTPTime starttime = RTPTime::CurrentTime();
    NALU_HEADER *nalu_hdr;
    FU_INDICATOR *fu_ind;
    FU_HEADER *fu_hdr; 
    
    NALU_t *nalu = AllocNALU(8000000);
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
	
        int index = 0;
	int nalustart;
	while(index < media_data.len){
	    if(media_data.buff[index++] == 0x00 && media_data.buff[index++] == 0x00){
		if(media_data.buff[index++] == 0x01){
		    nalu->startcodeprefix_len = 3;
		    goto gotnal;
		}else{
		    index--;
		    if(media_data.buff[index++] == 0x00 && media_data.buff[index++] == 0x01){
			nalu->startcodeprefix_len = 4;
                    	goto gotnal;
		    }
		}
	    }
	    continue;
gotnal:
	    memset(nalu->buf, 0, 8000000);
	    int nalu_type = media_data.buff[index] & 0x1f;
	    if(nalu_type == 0x07){//sps
		int sps_index = index;
		while(sps_index < media_data.len){
		    if(media_data.buff[sps_index++] == 0x00 && media_data.buff[sps_index++] == 0x00){
			if(media_data.buff[sps_index++] == 0x01){
			    break;
			}else{
			    sps_index--;
			    if(media_data.buff[sps_index++] == 0x00 && media_data.buff[sps_index++] == 0x01){
				break;
			    }
			}
		    }
		}
		int type = media_data.buff[sps_index] & 0x1f;
		if(type == 0x08){//pps
		    int pps_index = sps_index;
		    while(pps_index < media_data.len){
                        if(media_data.buff[pps_index++] == 0x00 && media_data.buff[pps_index++] == 0x00){
                            if(media_data.buff[pps_index++] == 0x01){
                                nalu->len = pps_index - 3 - index;
                                break;
                            }else{
                                pps_index--;
                                if(media_data.buff[pps_index++] == 0x00 && media_data.buff[pps_index++] == 0x01){
                                    nalu->len = pps_index - 4 - index;
                                    break;
                                }
                            } 
                        }
                    }
                    if(nalu->len == 0){
			printf("because of 0 break \n");
			break;
		    }
		    memcpy(nalu->buf, media_data.buff + index, nalu->len);
		    
		    index = index + nalu->len;	
		}
	    }else if(nalu_type == 0x05 || nalu_type == 0x01){//I frame and P frame
		nalu->len = media_data.len - index;
		memcpy(nalu->buf, media_data.buff + index, nalu->len);
		index = index + nalu->len;
	    }
	    nalu->forbidden_bit = nalu->buf[0] & 0x80;      //1 bit
            nalu->nal_reference_idc = nalu->buf[0] & 0x60;  // 2 bit
            nalu->nal_unit_type = (nalu->buf[0]) & 0x1f;    // 5 bit 

	    if(nalu->len < MAX_RTP_PKT_LENGTH){
		memset(sendbuf, 0, MAX_RTP_PKT_LENGTH);
		nalu_hdr = (NALU_HEADER*)&sendbuf[0];
		nalu_hdr->F = nalu->forbidden_bit;
		nalu_hdr->NRI = nalu->nal_reference_idc>>5;
		nalu_hdr->TYPE = nalu->nal_unit_type;
		nalu_payload=&sendbuf[1];
		memcpy(nalu_payload, nalu->buf + 1, nalu->len -1);
		ts_current=ts_current+timestamp_increse;    
		ret = session.SendPacket((void*)sendbuf, nalu->len, 96, true, 3600);
		if(ret < 0) {
		    string msg = RTPGetErrorString(ret);
		    printf("Error string:%s \n", msg.c_str());
		    LOG(false, function + " jrtp send packet failed " + msg);
		}
	    }else if(nalu->len > MAX_RTP_PKT_LENGTH){
		//session.SetDefaultMark(false);
	        int k = 0, l = 0;
		k = nalu->len / MAX_RTP_PKT_LENGTH;
		l = nalu->len % MAX_RTP_PKT_LENGTH;
		int t = 0;
		ts_current = ts_current + timestamp_increse;
		while(t <= k){
		    memset(sendbuf, 0, MAX_RTP_PKT_LENGTH);
		    fu_ind = (FU_INDICATOR*)&sendbuf[0];
		    fu_ind->F = nalu->forbidden_bit;
		    fu_ind->NRI = nalu->nal_reference_idc>>5;
		    fu_ind->TYPE=28;
		    if(!t){//fisrt slice
			fu_hdr =(FU_HEADER*)&sendbuf[1];
			fu_hdr->E=0;
			fu_hdr->R=0;
			fu_hdr->S=1;
			fu_hdr->TYPE = nalu->nal_unit_type;
			nalu_payload=&sendbuf[2];
			memcpy(nalu_payload , nalu->buf + 1, MAX_RTP_PKT_LENGTH);
			ret = session.SendPacket((void*)sendbuf, MAX_RTP_PKT_LENGTH + 2, 96, false, 0);
			if(ret < 0) {
			    string msg = RTPGetErrorString(ret);
			    printf("Error string:%s \n", msg.c_str());
			    LOG(false, function + " jrtp send packet failed " + msg);
			}
			t++;
		    }else if(t < k && 0 != t){//middle slice
			fu_hdr =(FU_HEADER*)&sendbuf[1];
			fu_hdr->E=0;
			fu_hdr->R=0;
			fu_hdr->S=0;
			fu_hdr->TYPE = nalu->nal_unit_type;
			nalu_payload=&sendbuf[2];
			memcpy(nalu_payload, nalu->buf+t*MAX_RTP_PKT_LENGTH + 1,MAX_RTP_PKT_LENGTH);
			ret = session.SendPacket((void*)sendbuf, MAX_RTP_PKT_LENGTH + 2, 96, false, 0);
			if(ret < 0) {
			    string msg = RTPGetErrorString(ret);
			    printf("Error string:%s \n", msg.c_str());
			    LOG(false, function + " jrtp send packet failed " + msg);
			}
			t++;
		    }else if(t == k){//last slice
			fu_hdr = (FU_HEADER*)&sendbuf[1];
			fu_hdr->E = 1;
			fu_hdr->R = 0;
			fu_hdr->S = 0;
			fu_hdr->TYPE = nalu->nal_unit_type;
			nalu_payload = &sendbuf[2];
			memcpy(nalu_payload, nalu->buf+t*MAX_RTP_PKT_LENGTH + 1,l - 1);
			ret = session.SendPacket((void *)sendbuf,l+1,96,true,3600);
			if(ret < 0) {
			    string msg = RTPGetErrorString(ret);
			    printf("Error string:%s \n", msg.c_str());
			    LOG(false, function + " jrtp send packet failed " + msg);
			}
			t++;
		    }
		}
		break;
	    }
	}
        if(media_data.buff){
		free(media_data.buff);
		media_data.buff = NULL;
	}	
    }
}

NALU_t *AllocNALU(int buffersize)
{
	NALU_t *nal =NULL;

    if ((nal = (NALU_t*)calloc (1, sizeof (NALU_t))) == NULL)
    {
        printf("AllocNALU: n");
        exit(0);
    }

    nal->max_size=buffersize;

    if ((nal->buf = (unsigned char*)calloc (buffersize, sizeof (char))) == NULL)
    {
        free (nal);
        printf ("AllocNALU: nal->buf");
        exit(0);
    }

    return nal;
}


