#include "vtt_stream.h"
#include <stdlib.h>
#include <string.h>
void vtt_stream_init(VttStream *p,VttBlockCallback callback,void *userdata) {
    memset(p,0,sizeof(*p));p->callback=callback;p->userdata=userdata;
}
void vtt_stream_free(VttStream *p) {free(p->block);memset(p,0,sizeof(*p));}
static int emit(VttStream *p) {
    while(p->length&&p->block[p->length-1]=='\n')p->length--;
    if(!p->length)return 1;
    p->block[p->length]=0;
    if(!p->header) {
        size_t bom=p->length>=3&&!memcmp(p->block,"\xef\xbb\xbf",3)?3:0;
        if(p->length<bom+6||memcmp(p->block+bom,"WEBVTT",6)||
           (p->length>bom+6&&p->block[bom+6]!=' '&&p->block[bom+6]!='\t'&&p->block[bom+6]!='\n'))return 0;
        p->header=1;
    } else if(p->callback&&!p->callback(p->userdata,p->block,p->length))return 0;
    p->length=0;return 1;
}
int vtt_stream_feed(VttStream *p,const char *data,size_t length) {
    if(!p||p->failed||(!data&&length))return 0;
    for(size_t i=0;i<length;i++) {
        char c=data[i];
        if(!c){p->failed=1;return 0;}
        if(p->skip_lf&&c=='\n'){p->skip_lf=0;continue;}
        p->skip_lf=c=='\r';if(c=='\r')c='\n';
        if(c=='\n'&&(!p->length||p->block[p->length-1]=='\n')) {
            if(!emit(p)){p->failed=1;return 0;}continue;
        }
        if(p->length>=VTT_BLOCK_LIMIT){p->failed=1;return 0;}
        if(p->length+2>p->capacity) {
            size_t capacity=p->capacity?p->capacity*2:1024;
            if(capacity>VTT_BLOCK_LIMIT+1)capacity=VTT_BLOCK_LIMIT+1;
            char *grown=realloc(p->block,capacity);
            if(!grown){p->failed=1;return 0;}p->block=grown;p->capacity=capacity;
        }
        p->block[p->length++]=c;p->block[p->length]=0;
    }
    return 1;
}
int vtt_stream_finish(VttStream *p) {
    if(!p||p->failed||!emit(p)||!p->header)return 0;
    return 1;
}
