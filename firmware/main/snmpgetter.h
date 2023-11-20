#pragma once

typedef struct {
	int bps_in;
	int bps_out;
} snmpgetter_bw_t;

void snmpgetter_get_bw(snmpgetter_bw_t *bw);

int snmpgetter_start(const char *host, int port, char *comstr, char *oid_in, char *oid_out);
void snmpgetter_stop();


