#pragma once
#include <pthread.h>

int InitJrtp(struct sockaddr_in addr_in, int dstPort);

void *JrtpFun(void *ptr);

void *UDPSOCKFun(void *ptr);

typedef struct
{
	unsigned char forbidden_bit;         
	unsigned char nal_reference_idc;        
	unsigned char nal_unit_type;           
	unsigned int startcodeprefix_len;       
	unsigned int len;                       
	unsigned int max_size;                 
	unsigned char * buf;                  
	unsigned int lost_packets;            
} NALU_t;

typedef struct
{
	//byte 0   
	unsigned char TYPE : 5;
	unsigned char NRI : 2;
	unsigned char F : 1;
} NALU_HEADER;

typedef struct
{
	//byte 0   
	unsigned char TYPE : 5;
	unsigned char NRI : 2;
	unsigned char F : 1;
} FU_INDICATOR; // 1 BYTE

typedef struct
{
	//byte 0   
	unsigned char TYPE : 5;
	unsigned char R : 1;
	unsigned char E : 1;
	unsigned char S : 1;
} FU_HEADER;   // 1 BYTES   

NALU_t *AllocNALU(int buffersize);
 
