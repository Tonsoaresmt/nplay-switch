#include "response_budget.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
struct membuf {char *data;size_t len,cap,limit;};
#include "audit062_writer.inc"
int main(void){
 const char *paths[]={"https://fixture.invalid/api/catalog/movies","/api/sync/progress/42","/api/accel/jobs","/api/account/me",NULL,"https://fixture.invalid"};
 const size_t sizes[]={16,8,8,4,4,4};char chunk[65536];memset(chunk,'a',sizeof(chunk));
 for(unsigned i=0;i<sizeof(paths)/sizeof(*paths);i++){
  struct membuf m={.limit=response_body_budget(paths[i])};assert(m.limit==sizes[i]*1024*1024);
  while(m.len<m.limit)assert(write_cb(chunk,1,sizeof(chunk),&m)==sizeof(chunk));
  assert(!write_cb(chunk,1,1,&m)&&m.cap<=m.limit+1&&m.data[m.len]==0);
  assert(!write_cb(chunk,SIZE_MAX,2,&m));free(m.data);
 }
 struct membuf explicit={.limit=37};assert(write_cb(chunk,1,37,&explicit)==37&&!write_cb(chunk,1,1,&explicit));free(explicit.data);
 puts("PASS route budgets 4/8/16MiB: actual writer accepts exact boundary, rejects excess/chunked and integer overflow, explicit cap retained; no live HTTP.");return 0;
}
