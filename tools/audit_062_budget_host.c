#include "cJSON.h"
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef union {max_align_t align;size_t size;} Header;
static size_t live,peak;
static void*tracked_malloc(size_t n){Header*h=malloc(sizeof(*h)+n);assert(h);h->size=n;live+=n;if(live>peak)peak=live;return h+1;}
static void tracked_free(void*p){if(p){Header*h=(Header*)p-1;live-=h->size;free(h);}}
static void*tracked_realloc(void*p,size_t n){if(!p)return tracked_malloc(n);Header*h=(Header*)p-1;size_t old=h->size;h=realloc(h,sizeof(*h)+n);assert(h);h->size=n;live=live-old+n;if(live>peak)peak=live;return h+1;}
struct membuf{char*data;size_t len,cap,limit;};
#define realloc tracked_realloc
#include "audit062_writer.inc"
#undef realloc
int main(void){
 cJSON_Hooks hooks={tracked_malloc,tracked_free};cJSON_InitHooks(&hooks);
 char chunk[65536];memset(chunk,'a',sizeof(chunk));
 struct membuf unlimited={0};
 const char*prefix="{\"padding\":\"";
 assert(write_cb((char*)prefix,1,strlen(prefix),&unlimited)==strlen(prefix));
 for(int i=0;i<384;i++)assert(write_cb(chunk,1,sizeof(chunk),&unlimited)==sizeof(chunk));
 assert(write_cb("\"}",1,2,&unlimited)==2);
 cJSON*j=cJSON_Parse(unlimited.data);assert(j&&strlen(cJSON_GetObjectItem(j,"padding")->valuestring)==24u*1024u*1024u);
 assert(peak>=56u*1024u*1024u);
 printf("REPRODUCED unset HTTP budget: %zu body bytes -> %zu buffer bytes, %zu peak allocated bytes including real cJSON parse (allocator headers excluded). Synthetic valid JSON/chunks, real growth/parser C.\n",unlimited.len,unlimited.cap,peak);
 cJSON_Delete(j);tracked_free(unlimited.data);assert(!live);
 struct membuf bounded={.limit=3u*1024u*1024u};int accepted=0;
 for(int i=0;i<384;i++){if(!write_cb(chunk,1,sizeof(chunk),&bounded))break;accepted++;}
 assert(accepted==48&&bounded.len==bounded.limit&&bounded.cap<=bounded.limit+1);
 tracked_free(bounded.data);assert(!live);cJSON_InitHooks(NULL);
 puts("HEALTHY: same real writer enforces explicit3MiB budget; API/catalog callers with limit0 are the missing boundary, not a broken cap implementation.");return 0;
}
