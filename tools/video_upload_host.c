#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
typedef unsigned Uint32;
typedef struct {int num,den;} AVRational;
typedef struct {int x,y,w,h;} SDL_Rect;
typedef int SDL_Renderer;
typedef struct {int format,width,height;uint8_t*data[3];int linesize[3];AVRational sample_aspect_ratio;} AVFrame;
typedef struct {int w,h;unsigned fmt;} SDL_Texture;
typedef struct {int video_x,video_y,video_w,video_h;} PlayerHud;
enum {AV_PIX_FMT_YUV420P=1,AV_PIX_FMT_NV12,AV_PIX_FMT_NVTEGRA,AV_PIX_FMT_P010,
 SDL_PIXELFORMAT_NV12=10,SDL_PIXELFORMAT_IYUV,SDL_TEXTUREACCESS_STREAMING,SWS_BILINEAR};
typedef struct {uint8_t*ptr;size_t cap;} Plane;
static Plane planes[16];
static int fail_alloc,fail_sws,fail_scale,fail_writable,fail_texture,fail_upload,fail_hw,nv12_missing;
static int textures,frames,uploads,scales,dropped_video,hardware_decode,running,playback_error,nv12_attempts;
static int vw=1920,vh=1080;
static SDL_Texture*tex;
static AVFrame*transfer,*yuv;
static Uint32 texture_format;
static int nv12_unavailable;
static struct {AVRational sample_aspect_ratio;} decoder={{1,1}},*vctx=&decoder;
static PlayerHud hud_base;
static SDL_Rect dst;
static struct SwsContext {int w,h;} context,*sws;
static void diag_player_event(const char*a,const char*b,const char*c,...){(void)a;(void)b;(void)c;}
static uint8_t*plane_new(size_t cap){for(int i=0;i<16;i++)if(!planes[i].ptr){uint8_t*p=malloc(cap);assert(p);planes[i]=(Plane){p,cap};return p;}abort();}
static void plane_free(uint8_t*p){if(!p)return;for(int i=0;i<16;i++)if(planes[i].ptr==p){free(p);planes[i]=(Plane){0};return;}abort();}
static void plane_check(const uint8_t*p,int pitch,int rows,int width){
 assert(p&&rows>0&&pitch>=width);
 for(int i=0;i<16;i++)if(planes[i].ptr==p){
  size_t needed=(size_t)(rows-1)*pitch+(unsigned)width;
  if(needed>planes[i].cap){fprintf(stderr,"UPLOAD OUT OF BOUNDS: need=%zu available=%zu texture=%dx%d\n",needed,planes[i].cap,vw,vh);abort();}
  return;
 }abort();
}
static void av_frame_unref(AVFrame*f){for(int i=0;i<3;i++){plane_free(f->data[i]);f->data[i]=NULL;} }
static AVFrame*av_frame_alloc(void){if(fail_alloc)return NULL;frames++;return calloc(1,sizeof(AVFrame));}
static void frame_free(AVFrame**f){if(*f){av_frame_unref(*f);free(*f);*f=NULL;frames--;}}
static int av_frame_get_buffer(AVFrame*f,int alignment){(void)alignment;
 f->linesize[0]=f->width;f->data[0]=plane_new((size_t)f->width*f->height);
 int cw=(f->width+1)/2,ch=(f->height+1)/2;
 if(f->format==AV_PIX_FMT_NV12){f->linesize[1]=cw*2;f->data[1]=plane_new((size_t)cw*2*ch);}
 else {for(int i=1;i<3;i++){f->linesize[i]=cw;f->data[i]=plane_new((size_t)cw*ch);}}
 return 0;
}
static int av_frame_make_writable(AVFrame*f){(void)f;return fail_writable?-1:0;}
static int av_hwframe_transfer_data(AVFrame*t,const AVFrame*f,int flags){(void)flags;if(fail_hw)return -1;
 t->width=f->width;t->height=f->height;t->format=AV_PIX_FMT_NV12;t->sample_aspect_ratio=f->sample_aspect_ratio;return av_frame_get_buffer(t,32);}
static struct SwsContext*sws_getCachedContext(struct SwsContext*s,int iw,int ih,int fmt,int ow,int oh,int outfmt,int filter,void*a,void*b,void*c){
 (void)s;(void)fmt;(void)filter;(void)a;(void)b;(void)c;assert(iw>0&&ih>0&&outfmt==AV_PIX_FMT_YUV420P);
 if(fail_sws)return NULL;
 context.w=ow;context.h=oh;return &context;
}
static int sws_scale(struct SwsContext*s,const uint8_t*const*src,const int*pitch,int y,int height,uint8_t*const*out,const int*opitch){
 (void)src;(void)pitch;(void)y;assert(height>0);scales++;if(fail_scale)return -1;plane_check(out[0],opitch[0],s->h,s->w);return s->h;}
static SDL_Texture*SDL_CreateTexture(SDL_Renderer*r,unsigned fmt,int access,int w,int h){(void)r;(void)access;
 if(fmt==SDL_PIXELFORMAT_NV12)nv12_attempts++;
 if(fail_texture||(nv12_missing&&fmt==SDL_PIXELFORMAT_NV12))return NULL;
 SDL_Texture*t=malloc(sizeof(*t));assert(t);*t=(SDL_Texture){w,h,fmt};textures++;return t;}
static void SDL_DestroyTexture(SDL_Texture*t){if(t){free(t);textures--;}}
static int SDL_UpdateNVTexture(SDL_Texture*t,const void*rect,const uint8_t*y,int yp,const uint8_t*uv,int uvp){(void)rect;
 assert(t->fmt==SDL_PIXELFORMAT_NV12);plane_check(y,yp,t->h,t->w);plane_check(uv,uvp,(t->h+1)/2,((t->w+1)/2)*2);uploads++;return fail_upload?-1:0;}
static int SDL_UpdateYUVTexture(SDL_Texture*t,const void*rect,const uint8_t*y,int yp,const uint8_t*u,int up,const uint8_t*v,int vp){(void)rect;
 assert(t->fmt==SDL_PIXELFORMAT_IYUV);plane_check(y,yp,t->h,t->w);plane_check(u,up,(t->h+1)/2,(t->w+1)/2);plane_check(v,vp,(t->h+1)/2,(t->w+1)/2);uploads++;return fail_upload?-1:0;}
#include "player_video_layout.h"
static int upload(int w,int h,int format){
 AVFrame f={.width=w,.height=h,.format=format,.sample_aspect_ratio={1,1}};
 if(format!=AV_PIX_FMT_NVTEGRA&&w>0&&h>0)av_frame_get_buffer(&f,32);
 AVFrame*frame=&f;SDL_Renderer*ren=NULL;running=1;playback_error=0;
 for(int once=0;once<1;once++){
#include "video_upload.inc"
 }
 av_frame_unref(&f);return playback_error;
}
static void clean(void){SDL_DestroyTexture(tex);tex=NULL;frame_free(&yuv);frame_free(&transfer);texture_format=0;sws=NULL;
 for(int i=0;i<16;i++)assert(!planes[i].ptr);
 assert(!textures&&!frames);
 fail_alloc=fail_sws=fail_scale=fail_writable=fail_texture=fail_upload=fail_hw=nv12_missing=0;
 nv12_unavailable=nv12_attempts=0;
}
int main(void){setvbuf(stdout,NULL,_IONBF,0);(void)hud_base;(void)dst;(void)vctx;
 transfer=av_frame_alloc();assert(!upload(1920,1080,AV_PIX_FMT_YUV420P));
 assert(!upload(640,360,AV_PIX_FMT_YUV420P)); // Published .56 fails bounds here.
 assert(scales==1);clean();
 const int sizes[][2]={{640,360},{720,480},{1280,720},{1920,1080},{1080,1920},{641,361}};
 int cases=0;
 for(unsigned initial=0;initial<6;initial++)for(int format=1;format<=4;format++){
  vw=sizes[initial][0];vh=sizes[initial][1];transfer=av_frame_alloc();
  for(int pass=0;pass<8;pass++)for(unsigned next=0;next<6;next++){assert(!upload(sizes[next][0],sizes[next][1],format));assert(running);cases++;}
  clean();
 }
 transfer=av_frame_alloc();vw=1920;vh=1080;nv12_missing=1;
 for(int i=0;i<100;i++)assert(!upload(vw,vh,AV_PIX_FMT_NV12)&&tex->fmt==SDL_PIXELFORMAT_IYUV);
 assert(nv12_attempts==1&&textures==1);clean();
 for(int failure=0;failure<6;failure++){
  transfer=av_frame_alloc();if(failure==0)fail_alloc=1;if(failure==1)fail_sws=1;if(failure==2)fail_writable=1;if(failure==3)fail_texture=1;if(failure==4)fail_upload=1;
  if(failure==5)fail_scale=1;
  assert(upload(640,360,AV_PIX_FMT_P010)==-4&&!running);clean();
 }
 transfer=av_frame_alloc();fail_hw=1;int before=dropped_video;assert(!upload(1280,720,AV_PIX_FMT_NVTEGRA));assert(dropped_video==before+1);clean();
 transfer=av_frame_alloc();assert(upload(0,720,AV_PIX_FMT_YUV420P)==-4);assert(upload(1280,0,AV_PIX_FMT_NV12)==-4);clean();
 SDL_Rect r=player_video_rect(720,480,8,9);assert(r.w==960&&r.h==720&&r.x==160);
 r=player_video_rect(720,576,16,15);assert(r.w==960&&r.h==720&&r.x==160);
 r=player_video_rect(1080,1920,1,1);assert(r.w==405&&r.h==720);
 r=player_video_rect(1920,800,0,0);assert(r.w==1280&&r.h==533);
 for(int w=1;w<4000;w+=83)for(int h=1;h<2200;h+=79){r=player_video_rect(w,h,1,1);assert(r.w>=1&&r.h>=1&&r.x>=0&&r.y>=0&&r.x+r.w<=1280&&r.y+r.h<=720);}
 printf("PASS actual upload C: %d resolution/pixel-format transitions, bounded planes, fixed texture budget, fallback NV12, allocation/scaler/upload/hardware failures and cleanup. GPU/decoder simulated.\n",cases);
 return 0;
}
