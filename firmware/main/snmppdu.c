/*
Semi-universal SNMP PDU ASN.1 handling. You can use this to build and/or dissect SNMP packets.
*/
/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * Jeroen Domburg <jeroen@spritesmods.com> wrote this file. As long as you retain 
 * this notice you can do whatever you want with this stuff. If we meet some day, 
 * and you think this stuff is worth it, you can buy me a beer in return. 
 * ----------------------------------------------------------------------------
 */

#include <stdlib.h>
#include <stddef.h>
#include <string.h>

#include "snmppdu.h"

#ifdef DEBUG
  #include <stdio.h>
  #define dprintf(a...) printf(a)
#else
  #define dprintf(a...) do{}while(0)
#endif

PduField *pduNewSequence() {
	PduField *f=calloc(sizeof(PduField), 1);
	f->type=PRIM_SEQ;
	f->len=0;
	f->data=NULL;
	f->contents=NULL;
	f->next=NULL;
	return f;
}

PduField *pduNewGetReqPdu() {
	PduField *f=pduNewSequence();
	f->type=PRIM_GETREQPDU;
	return f;
}

PduField *pduNewGetRespPdu() {
	PduField *f=pduNewSequence();
	f->type=PRIM_GETRESPPDU;
	return f;
}

PduField *pduNewSetReqPdu() {
	PduField *f=pduNewSequence();
	f->type=PRIM_SETREQPDU;
	return f;
}

static int encodeLen(char *buff, int n) {
	int i;
	int nb;
	//Calculate amount of bytes we need to put the int in.
	nb=1;
	i=n;
	while (i>=128) {
		i/=128;
		nb++;
	}
//	dprintf("encodeLen: %d in %d bytes\n", n, nb);
	//Store bytes
	i=nb-1;
	while (i>=0) {
		buff[i]=(n&0x7f)|((i==nb-1)?0x0:0x80);
		n/=128;
		--i;
	}
	return nb;
}

static int decodeLen(char *buff, int *len) {
	int p=0;
	int ret=0;
	while (buff[p]&0x80) {
		ret|=(buff[p]&0x7f);
		ret*=128;
		p++;
	}
	ret|=buff[p];
	if (len!=NULL) *len=p+1;
	return ret;
}

//ToDo: Also handle negative ints...
PduField *pduNewInt(int n) {
	char buff[8];
	int nb;
	PduField *f=malloc(sizeof(PduField));
	f->type=PRIM_INT;
	nb=0;
	if (n>=0xffffff) buff[nb++]=(n>>24)&0xff;
	if (n>=0xffff) buff[nb++]=(n>>16)&0xff;
	if (n>=0xff) buff[nb++]=(n>>8)&0xff;
	buff[nb++]=(n)&0xff;
	f->data=malloc(nb);
	memcpy(f->data, buff, nb);
	f->len=nb;
	f->contents=NULL;
	f->next=NULL;
	return f;
}

PduField *pduNewCtr32(int n) {
	PduField *f=pduNewInt(n);
	f->type=PRIM_CTR32;
	return f;
}

PduField *pduNewGauge32(int n) {
	PduField *f=pduNewInt(n);
	f->type=PRIM_GAUGE32;
	return f;
}

PduField *pduNewOctetString(const char *str) {
	PduField *f=malloc(sizeof(PduField));
	f->type=PRIM_OCTSTR;
	f->data=malloc(strlen(str));
	memcpy(f->data, str, strlen(str));
	f->len=strlen(str);
	f->contents=NULL;
	f->next=NULL;
	return f;
}

PduField *pduNewNull() {
	PduField *f=malloc(sizeof(PduField));
	f->type=PRIM_NULL;
	f->data=NULL;
	f->len=0;
	f->contents=NULL;
	f->next=NULL;
	return f;
}

void pduAscToOid(const char *buff, int *oid) {
	int p=0;
	int i=0;
	int l=strlen(buff);
	oid[0]=0;
	oid[1]=-1;
	if (buff[0]=='.') p++;
	while (p!=l) {
		if (buff[p]>='0' && buff[p]<='9') {
			oid[i]=(oid[i]*10)+(buff[p]-'0');
		} else if (buff[p]=='.') {
			i++;
			oid[i]=0;
			oid[i+1]=-1;
		}
		p++;
	}
}

PduField *pduNewOid(int *oidList) {
	char buff[128];
	PduField *f=malloc(sizeof(PduField));
	int i, p,l;
	//Assume we start with .1.3
	buff[0]=0x2b;
	p=1; i=2;
	//Encode the rest
	while (oidList[i]>=0) {
		l=encodeLen(&buff[p], oidList[i]);
		p+=l;
		i++;
	}
	f->type=PRIM_OID;
	f->data=malloc(p);
	memcpy(f->data, buff, p);
	f->len=p;
	f->contents=NULL;
	f->next=NULL;
	return f;
}

void pduAddToSequence(PduField *seq, PduField *pdu) {
	PduField *c=seq->contents; //first field
	if (c==NULL) {
		//First entry
		seq->contents=pdu;
	} else {
		//Add to ll
		while (c->next!=NULL) c=c->next; //find end of linked list
		c->next=pdu;
	}
	return;
}

//Get length of field data, in bytes
int pduGetLen(PduField *f, int incHeader) {
	int len, dlen=0;
	PduField *cf;
	//Get data length
	if (f->type==PRIM_INT || f->type==PRIM_OCTSTR || f->type==PRIM_OID ||
			f->type==PRIM_GAUGE32 || f->type==PRIM_CTR32) {
		dlen=f->len;
	} else if (f->type==PRIM_NULL) {
		dlen=0;
	} else if (f->type==PRIM_SEQ || f->type==PRIM_GETREQPDU || f->type==PRIM_GETRESPPDU ||
				f->type==PRIM_SETREQPDU) {
		cf=f->contents;
		dlen=0;
		while (cf!=NULL) {
			dlen+=pduGetLen(cf, 1);
			cf=cf->next;
		}
	} else {
		dprintf("pduGetLen: Unknown type (0x%x)\n", f->type);
	}

	len=dlen;
	//If needed, add header size
	if (incHeader) {
		while (dlen>=128) {
			len++;
			dlen/=128;
		}
		len+=2; //for 1st byte of data len plus type byte
	}
	return len;
}

int pduGetIntVal(PduField *f) {
	int i, r;
	//todo: support for xx64 types?
	if (f->type!=PRIM_INT && f->type!=PRIM_CTR32 && f->type!=PRIM_GAUGE32) return -1;
	if (f->data[0]&0x80) r=-1; else r=0;
	for (i=0; i<f->len; i++) {
		r=r<<8;
		r|=f->data[i];
	}
	return r;
}

void pduGetOctStrVal(PduField *f, char *buff) {
	if (f->type!=PRIM_OCTSTR) return;
	memcpy(buff, f->data, f->len);
	buff[f->len]=0;
}

void pduGetOidVal(PduField *f, int *oid) {
	int p, i, l;
	if (f->type!=PRIM_OID) {
		oid[0]=-1;
		return;
	}
	//Sidestep weird .1.3 encoding
	oid[0]=1;
	oid[1]=3;
	p=1; i=2;
	while (p<f->len) {
		oid[i++]=decodeLen(&f->data[p], &l);
		p+=l;
	}
	oid[i]=-1;
}

#ifdef DEBUG

void pduDump(PduField *f, int level) {
	char spaces[8];
	int i;
	for (i=0; i<level; i++) spaces[i]=' ';
	spaces[level]=0;
	if (f->type==PRIM_INT || f->type==PRIM_CTR32 || f->type==PRIM_GAUGE32) {
		if (f->type==PRIM_INT) dprintf("%sInt:", spaces);
		if (f->type==PRIM_CTR32) dprintf("%sCounter32:", spaces);
		if (f->type==PRIM_GAUGE32) dprintf("%sGauge32:", spaces);
		dprintf(" %d\n",pduGetIntVal(f));
	} else if (f->type==PRIM_OCTSTR) {
		char buff[256];
		pduGetOctStrVal(f, buff);
		dprintf("%sOctet str: '%s'\n", spaces, buff);
	} else if (f->type==PRIM_OID) {
		int oid[128];
		pduGetOidVal(f, oid);
		i=2;
		dprintf("%sOID: .1.3", spaces);
		while (oid[i]>=0) {
			dprintf(".%d", oid[i]);
			i++;
		}
		dprintf("\n");
	} else if (f->type==PRIM_SEQ || f->type==PRIM_GETREQPDU || f->type==PRIM_GETRESPPDU ||
				f->type==PRIM_SETREQPDU) {
		PduField *c;
		if (f->type==PRIM_SEQ) dprintf("%sSequence: \n", spaces);
		if (f->type==PRIM_GETREQPDU) dprintf("%sGetReqPDU: \n", spaces);
		if (f->type==PRIM_GETRESPPDU) dprintf("%sGetRespPdu: \n", spaces);
		if (f->type==PRIM_SETREQPDU) dprintf("%sSetReqPdu: \n", spaces);
		c=f->contents;
		while (c!=NULL) {
			pduDump(c, level+1);
			c=c->next;
		}
		dprintf("%sSeq end.\n", spaces);
	} else if (f->type==PRIM_NULL) {
		dprintf("%sNULL\n", spaces);
	} else {
		dprintf("%sUNKNOWN (0x%x)\n", spaces, f->type);
	}
}

#endif

int pduToBin(PduField *f, char *b) {
	int lenIncHdr;
	int i=0;
	int len=pduGetLen(f, 0);
	b[i++]=f->type;
	i+=encodeLen(&b[i], len);
	lenIncHdr=len+i;
	if (f->type==PRIM_INT || f->type==PRIM_OCTSTR || f->type==PRIM_OID ||
		f->type==PRIM_CTR32 || f->type==PRIM_GAUGE32) {
		memcpy(&b[i], f->data, f->len);
	} else if (f->type==PRIM_SEQ || f->type==PRIM_GETREQPDU || f->type==PRIM_GETRESPPDU ||
				f->type==PRIM_SETREQPDU) {
		PduField *c;
		c=f->contents;
		while (c!=NULL) {
			i+=pduToBin(c, &b[i]);
			c=c->next;
		}
	} else if (f->type==PRIM_NULL) {
		//Do nothing
	} else {
		dprintf("pduToBin: Unknown type (0x%x)\n", f->type);
	}
	return lenIncHdr;
}

PduField *binToPdu(char *b, int *endpos) {
	int i=0, l;
	PduField *f=malloc(sizeof(PduField));
	f->type=(unsigned char)b[i++];
	f->len=decodeLen(&b[i], &l);
	f->contents=NULL;
	f->next=NULL;
	i+=l;
	if (f->type==PRIM_INT || f->type==PRIM_OCTSTR || f->type==PRIM_OID ||
			f->type==PRIM_CTR32 || f->type==PRIM_GAUGE32) {
		f->data=malloc(f->len);
		memcpy(f->data, &b[i], f->len);
		i+=f->len;
	} else if (f->type==PRIM_SEQ || f->type==PRIM_GETREQPDU || f->type==PRIM_GETRESPPDU ||
				f->type==PRIM_SETREQPDU) {
		while (i<=f->len) {
			PduField *c=binToPdu(&b[i], &l);
			pduAddToSequence(f, c);
			i+=l;
		}
	} else if (f->type==PRIM_NULL) {
		//Nothing to do.
	} else {
		dprintf("binToPdu: Unknown type (0x%x)\n", f->type);
	}
	if (endpos!=NULL) *endpos=i;
	return f;
}

void pduFree(PduField *f) {
	if (f==NULL) return;
	if (f->type==PRIM_INT || f->type==PRIM_OCTSTR || f->type==PRIM_OID || 
				f->type==PRIM_CTR32 || f->type==PRIM_GAUGE32) {
		//Contains data. Free that.
		free(f->data);
	} else if (f->type==PRIM_SEQ || f->type==PRIM_GETREQPDU || f->type==PRIM_GETRESPPDU ||
				f->type==PRIM_SETREQPDU) {
		//Free all children.
		PduField *c, *nc;
		c=f->contents;
		while (c!=NULL) {
			nc=c->next;
			pduFree(c);
			c=nc;
		}
	} else if (f->type==PRIM_NULL) {
		//Do nothing
	}
	free(f);
}

