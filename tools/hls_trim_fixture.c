#include "hls_manifest.h"
#include <stdio.h>
#include <stdlib.h>
int main(int argc,char**argv){
 if(argc!=3)return 2;
 FILE*f=fopen(argv[1],"rb");if(!f)return 3;
 char*body=malloc(1024*1024+1);if(!body){fclose(f);return 4;}
 size_t len=fread(body,1,1024*1024+1,f);fclose(f);
 if(len>1024*1024){free(body);return 5;}
 char*out=NULL;size_t size=0;double base=0,total=0;
 int rc=hls_media_playlist_trim(body,len,strtod(argv[2],NULL),&out,&size,&base,&total);
 fprintf(stderr,"%.6f %.6f\n",base,total);
 size_t written=rc==1?fwrite(out,1,size,stdout):fwrite(body,1,len,stdout);
 int ok=written==(rc==1?size:len);free(out);free(body);return ok?0:6;
}
