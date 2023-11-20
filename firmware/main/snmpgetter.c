#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "snmppdu.h"
#include "snmpgetter.h"
#include "esp_log.h"

static int sockfd;
static QueueHandle_t dataq=NULL;
static int req_stop=0;
static char req_in[1024], req_out[1024];
static int req_in_len, req_out_len;

static const char *TAG="snmpgetter";

static int64_t get_octets_from_resp(char *buf) {
	PduField *p=binToPdu(buf, NULL);
	PduField *c;
	//Move to actual value
	c=p->contents;		//SNMP version
	if (c==NULL) return -1;
	c=c->next;			//Comm str
	if (c==NULL) return -1;
	c=c->next;			//PDU
	if (c==NULL) return -1;
	c=c->contents;		//ReqID
	if (c==NULL) return -1;
	c=c->next;		//Error
	if (c==NULL) return -1;
	c=c->next;		//Error idx
	if (c==NULL) return -1;
	c=c->next;		//Varbind list
	if (c==NULL) return -1;
	c=c->contents;		//Varbind
	if (c==NULL) return -1;
	c=c->contents;		//OID
	if (c==NULL) return -1;
	c=c->next;			//Value
	if (c==NULL) return -1;
	//todo: this needs to support xx64 values
	int64_t bytes=pduGetIntVal(c);
	pduFree(p);
	return bytes;
}

static int64_t req_oid(char *req, int len) {
	write(sockfd, req, len);
	fd_set set;
	FD_ZERO(&set);
	FD_SET(sockfd, &set);
	struct timeval tv={
		.tv_sec=1
	};
	int n=select(sockfd+1, &set, NULL, NULL, &tv);
	if (n==1) {
		//got data
		char buff[1024];
		int len=read(sockfd, buff, 1024);
		return get_octets_from_resp(buff);
	} else {
		//timeout or some error
		ESP_LOGI(TAG, "timeout waiting for reply");
		return -1;
	}
}

static void snmpgetter_task(void *arg) {
	ESP_LOGI(TAG, "task started");
	snmpgetter_bw_t bw={0};
	int64_t in_last=0, out_last=0, ts_last=0;
	while(!req_stop) {
		int64_t ts_at_req=esp_timer_get_time();
		int64_t in_bytes=req_oid(req_in, req_in_len);
		int64_t out_bytes=req_oid(req_out, req_out_len);
		if (in_bytes!=-1 && out_bytes!=-1) {
			int64_t time_ms=(ts_at_req-ts_last)/1000;
			int64_t diff_in=in_bytes-in_last;
			int64_t diff_out=out_bytes-out_last;
			//Fix rollover. Note that this assumes a 32-bit response from in_bytes/out_bytes. Given we
			//don't support 64-bit things yet, this will always be the case.
			if (diff_in<0) diff_in+=(1ULL<<32);
			if (diff_out<0) diff_out+=(1ULL<<32);
			bw.bps_in=(diff_in*1000ULL)/time_ms;
			bw.bps_out=(diff_out*1000ULL)/time_ms;
			if (ts_last!=0) xQueueSend(dataq, &bw, portMAX_DELAY);
			ts_last=ts_at_req;
			in_last=in_bytes;
			out_last=out_bytes;
		}
	}
	close(sockfd);
	req_stop=0;
	ESP_LOGI(TAG, "task finished");
	vTaskDelete(NULL);
}

int snmpgetter_get_bw(snmpgetter_bw_t *bw, int timeout) {
	return xQueueReceive(dataq, bw, timeout);
}

static int gen_pdu_packet_for(const char *comstr, const char *oid, char *pkt) {
	int myOid[64];
	pduAscToOid(oid, myOid);

	PduField *req=pduNewSequence();
	pduAddToSequence(req, pduNewInt(1)); //SNMP version
	pduAddToSequence(req, pduNewOctetString(comstr)); //SNMP community
	PduField *getreq=pduNewGetReqPdu();
	pduAddToSequence(getreq, pduNewInt(1)); //Req ID
	pduAddToSequence(getreq, pduNewInt(0)); //Error
	pduAddToSequence(getreq, pduNewInt(0)); //Error idx
	PduField *vbl=pduNewSequence();
	PduField *vb=pduNewSequence();
	pduAddToSequence(vb, pduNewOid(myOid));
	pduAddToSequence(vb, pduNewNull());
	pduAddToSequence(vbl, vb);
	pduAddToSequence(getreq, vbl);
	pduAddToSequence(req, getreq);

//	pduDump(req, 0);

	int packet_len=pduToBin(req, pkt);
	pduFree(req);
	return packet_len;
}

int snmpgetter_start(const char *host, int port, char *comstr, char *oid_in, char *oid_out) {
	struct hostent *he;
	he = gethostbyname(host);
	if (!he) {
		perror("gethostbyname");
		return 0;
	}

	struct sockaddr_in servaddr={0};
	memcpy(&servaddr.sin_addr, he->h_addr_list[0], he->h_length);
	servaddr.sin_family = AF_INET;
	servaddr.sin_port = htons(port);

	sockfd = socket(servaddr.sin_family, SOCK_DGRAM, 0);
	if (sockfd<0) {
		perror("socket");
		return 0;
	}
	
	int optval=1;
	if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval))!=0) {
		perror("setsockopt");
		close(sockfd);
		return 0;
	}
	
	if (connect(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
		perror("connect");
		close(sockfd);
		return 0;
	}
	
	req_in_len=gen_pdu_packet_for(comstr, oid_in, req_in);
	req_out_len=gen_pdu_packet_for(comstr, oid_out, req_out);
	
	if (!dataq) dataq=xQueueCreate(1, sizeof(snmpgetter_bw_t));
	xTaskCreate(snmpgetter_task, "snmpget", 8192, NULL, 5, NULL);
	return 1;
}

void snmpgetter_stop() {
	//kinda hacky but works
	req_stop=1;
	while (req_stop) vTaskDelay(2);
}

