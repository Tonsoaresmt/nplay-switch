#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#define DEVICE_F "device_id.txt"
static int fault,random_calls;
static FILE *io_open(const char*p,const char*m){if(fault==1&&!strcmp(p,DEVICE_F)){errno=EACCES;return NULL;}return fopen(p,m);}
static size_t io_write(const void*p,size_t s,size_t n,FILE*f){if(fault==2)return 0;return fwrite(p,s,n,f);}
static int io_flush(FILE*f){return fault==3?EOF:fflush(f);}
static int io_close(FILE*f){int rc=fclose(f);return fault==4?EOF:rc;}
static int io_rename(const char*a,const char*b){if(fault==5)return -1;return rename(a,b);}
#define fopen io_open
#define fwrite io_write
#define fflush io_flush
#define fclose io_close
#define rename io_rename
#include "device_identity_store.inc"
#undef fopen
#undef fwrite
#undef fflush
#undef fclose
#undef rename
static char g_status[192];
static void randomGet(void*p,size_t n){assert(n==16);random_calls++;for(size_t i=0;i<n;i++)((unsigned char*)p)[i]=(unsigned char)(i+random_calls);}
#include "device_identity_ensure.inc"
static void fixture(const void *s,size_t n){FILE*f=fopen(DEVICE_F,"wb");assert(f);assert(fwrite(s,1,n,f)==n);assert(!fclose(f));}
int main(void){
 remove(DEVICE_F);remove(DEVICE_F ".tmp");
 char id[80],again[80];assert(!ensure_device_identity(id,sizeof(id))&&random_calls==1);
 for(int i=0;i<10000;i++){assert(!ensure_device_identity(again,sizeof(again)));assert(!strcmp(id,again)&&random_calls==1);}
 const char *other="nplay-switch-ffffffffffffffffffffffffffffffff";
 assert(!store_save_device_id(other));assert(store_load_device_id(again,sizeof(again))==1&&!strcmp(id,again));
 fault=1;assert(ensure_device_identity(again,sizeof(again))==-1&&random_calls==1);fault=0;
 char newline[84];snprintf(newline,sizeof(newline),"%s\r\n",id);fixture(newline,strlen(newline));assert(store_load_device_id(again,sizeof(again))==1&&!strcmp(id,again));
 const char*bad[]={"","nplay-switch-1234","nplay-switch-fffffffffffffffffffffffffffffffg","nplay-switch-ffffffffffffffffffffffffffffffffunexpected"};
 for(size_t i=0;i<sizeof(bad)/sizeof(bad[0]);i++){
  fixture(bad[i],strlen(bad[i]));int calls=random_calls;assert(store_load_device_id(again,sizeof(again))==-1);assert(ensure_device_identity(again,sizeof(again))==-1&&random_calls==calls);
 }
 char embedded[50];memcpy(embedded,id,46);embedded[45]=0;embedded[46]='x';fixture(embedded,47);assert(store_load_device_id(again,sizeof(again))==-1);
 char oversized[200];memset(oversized,'x',sizeof(oversized));fixture(oversized,sizeof(oversized));assert(store_load_device_id(again,sizeof(again))==-1);
 remove(DEVICE_F);
 for(fault=2;fault<=5;fault++){
  assert(ensure_device_identity(again,sizeof(again))==-1&&!again[0]);
  FILE*f=fopen(DEVICE_F,"rb");assert(!f);f=fopen(DEVICE_F ".tmp","rb");assert(!f);
 }
 fault=0;assert(!ensure_device_identity(id,sizeof(id)));assert(store_load_device_id(again,5)==-1);assert(!store_save_device_id("bad"));
 remove(DEVICE_F);remove(DEVICE_F ".tmp");
 puts("Device identity: 10000 QR/password reuses, immutable existing ID, atomic write, short/embedded-NUL/oversize file, read/write/flush/close/rename failures passed. No real SD/pairing tested.");
 return 0;
}
