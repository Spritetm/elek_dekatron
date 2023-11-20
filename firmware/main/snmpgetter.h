#pragma once

//note: bps here means *bytes* per second
typedef struct {
	int bps_in;
	int bps_out;
} snmpgetter_bw_t;

int snmpgetter_get_bw(snmpgetter_bw_t *bw, int timeout);

int snmpgetter_start(const char *host, int port, char *comstr, char *oid_in, char *oid_out);
void snmpgetter_stop();


