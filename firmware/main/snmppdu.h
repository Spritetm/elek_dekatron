#pragma once

#define PRIM_INT 0x02
#define PRIM_OCTSTR 0x04
#define PRIM_NULL 0x05
#define PRIM_OID 0x06
#define PRIM_SEQ 0x30
#define PRIM_CTR32 0x41
#define PRIM_GAUGE32 0x42
#define PRIM_GETREQPDU 0xA0
#define PRIM_GETRESPPDU 0xA2
#define PRIM_SETREQPDU 0xA3

//#define DEBUG
typedef struct PduField PduField;

struct PduField{
	int type;
	int len;
	char *data;
	PduField *contents;
	PduField *next;
};

PduField *pduNewSequence();
PduField *pduNewGetReqPdu();
PduField *pduNewGetRespPdu();
PduField *pduNewSetReqPdu();
PduField *pduNewInt(int n);
PduField *pduNewCtr32(int n);
PduField *pduNewGauge32(int n);
PduField *pduNewOctetString(const char *str);
PduField *pduNewNull();
void pduAscToOid(const char *buff, int *oid);
PduField *pduNewOid(int *oidList);
void pduAddToSequence(PduField *seq, PduField *pdu);
int pduGetIntVal(PduField *f);
void pduGetOctStrVal(PduField *f, char *buff);
void pduGetOidVal(PduField *f, int *oid);
#ifdef DEBUG
void pduDump(PduField *f, int level);
#endif
//Note: this interface sucks. It needs a buffer b which is large enough to contain the pdu,
//but there's no way to figure out that length beforehand. Should sanitize this.
int pduToBin(PduField *f, char *b);
PduField *binToPdu(char *b, int *endpos);
void pduFree(PduField *f);

