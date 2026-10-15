#include "playback_restart.h"
#include "hot_subtitles.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

int playback_restart_supported(const PlaybackSource *s, const char *base) {
    if (!s || !base || strncmp(base,"https://",8) || !base[8] || strpbrk(base+8,"?#@\r\n") ||
        !s->restart_seek || !s->sequential_stream || strcmp(s->delivery_str,"debrid") ||
        strcmp(s->container,"mp4") || !hot_subtitle_session_valid(s->hot_session_id)) return 0;
    char prefix[512];
    int n=snprintf(prefix,sizeof(prefix),"%s/api/media/debrid/%s/video",base,s->hot_session_id);
    return n>0 && n<(int)sizeof(prefix) && !strncmp(prefix,s->play_url,(size_t)n) &&
        (!s->play_url[n] || s->play_url[n]=='?') && !strpbrk(s->play_url,"#\r\n");
}

int playback_restart_urls(const PlaybackSource *s, const char *base, double target,
                           char *probe, size_t pc, char *video, size_t vc) {
    if (!probe || !pc || !video || !vc) return 0;
    probe[0]=video[0]=0;
    if (!playback_restart_supported(s,base) || !isfinite(target) || target<0 || target>86400) return 0;
    long long ms=(long long)llround(target*1000);
    int n=snprintf(probe,pc,"%s/api/stream/hot/%s/seek?t=%lld.%03lld",
                   base,s->hot_session_id,ms/1000,ms%1000);
    if (n<0 || (size_t)n>=pc) goto fail;
    // Source descriptors remain pristine across attempts. Reject ambiguous
    // pre-positioned descriptors rather than accepting duplicate t parameters.
    const char *query=strchr(s->play_url,'?');
    if (query) for (const char *p=query+1; *p;) {
        size_t key=strcspn(p,"=&");
        if ((key==1 && p[0]=='t') || (key==3 && !strncmp(p,"%74",3))) goto fail;
        const char *next=strchr(p,'&'); if (!next) break; p=next+1;
    }
    n=snprintf(video,vc,"%s%ct=%lld.%03lld",s->play_url,query?'&':'?',ms/1000,ms%1000);
    if (n<0 || (size_t)n>=vc) goto fail;
    return 1;
fail:
    probe[0]=video[0]=0;return 0;
}

int playback_restart_plan(const cJSON *j,double target,double *start,double *duration) {
    if (!j || !start || !duration || !isfinite(target) || target<0 || target>86400 ||
        !cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(j,"ok"))) return 0;
    const cJSON *mode=cJSON_GetObjectItemCaseSensitive(j,"mode");
    const cJSON *at=cJSON_GetObjectItemCaseSensitive(j,"start");
    const cJSON *dur=cJSON_GetObjectItemCaseSensitive(j,"duration");
    if (!cJSON_IsString(mode) || strcmp(mode->valuestring,"restart") ||
        !cJSON_IsNumber(at) || !isfinite(at->valuedouble) || at->valuedouble<0 ||
        at->valuedouble>target+1 || fabs(at->valuedouble-target)>=60) return 0;
    double d=cJSON_IsNumber(dur)?dur->valuedouble:0;
    if (!isfinite(d) || d<0 || d>86400 || (d>0 && at->valuedouble>=d)) return 0;
    *start=at->valuedouble;*duration=d;return 1;
}

double playback_restart_clock(double timestamp,double first_timestamp,double base) {
    return timestamp-first_timestamp+base;
}
