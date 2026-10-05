#include "subtitle_store.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>
static int valid_utf8(const unsigned char *p) {
    while(*p){unsigned n=*p<128?1:(*p&224)==192?2:(*p&240)==224?3:(*p&248)==240?4:0;
        if(!n)return 0;
        for(unsigned i=1;i<n;i++)if(!p[i]||(p[i]&192)!=128)return 0;
        p+=n;
    }
    return 1;
}
int main(void) {
    SubtitleStore s={0};
    assert(subtitle_store_add(&s,10,12,"Fala importante\nsegunda linha"));
    assert(subtitle_store_add(&s,10,12,"a"));
    assert(subtitle_store_add(&s,10,12,"b"));
    assert(subtitle_store_add(&s,10,12,"c"));
    const char *out=subtitle_store_text(&s,11);
    printf("FRAGMENTS priority: actual='%s' speech_visible=%d (expected 1)\n",out,strstr(out,"Fala importante")!=NULL);
    assert(!strstr(out,"Fala importante"));subtitle_store_free(&s);
    for(int pass=0;pass<4;pass++) {
        for(int i=0;i<30;i++){char t[80];snprintf(t,sizeof(t),"long-sign-%d",i);assert(subtitle_store_add(&s,i*100,i*100+70,t));}
        printf("REPLAY longs: pass=%d cues=%d expected=30 text_bytes=%zu\n",pass+1,subtitle_store_count(&s),s.text_len);
    }
    assert(s.long_count==120);subtitle_store_free(&s);
    SubtitleQueue q={0};subtitle_queue_push(&q,10,12,"active dialogue");
    for(int i=0;i<32;i++){char t[40];snprintf(t,sizeof(t),"future %d",i);subtitle_queue_push(&q,100+i,101+i,t);}
    printf("NATIVE queue burst: slots=%d active='%s' (expected active dialogue)\n",q.count,subtitle_queue_text(&q,11));
    assert(!strstr(subtitle_queue_text(&q,11),"active dialogue"));
    char t[520];memset(t,'a',510);memcpy(t+510,"\xc3\xa9",3);subtitle_queue_reset(&q);
    subtitle_queue_push(&q,0,2,t);
    printf("NATIVE UTF8 truncation: bytes=%zu valid=%d (expected 1)\n",strlen(q.cues[0].text),valid_utf8((unsigned char*)q.cues[0].text));
    assert(!valid_utf8((unsigned char*)q.cues[0].text));
    puts("AUDIT: 4 defects reproduced using actual queue/store; no app modifications.");
}
