#include "media_list_sync.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int allocations,fail_at;
static void *fault_alloc(size_t n){if(fail_at&&++allocations==fail_at)return NULL;return malloc(n);}
static cJSON *parse(const char*s){cJSON*p=cJSON_Parse(s);assert(p);return p;}
static cJSON *test_list;
static int saves;
static cJSON *media_list_at(int i){return i==0?test_list:NULL;}
static cJSON *media_items(int i){return cJSON_GetObjectItem(media_list_at(i),"items");}
static void save_media_lists(void){saves++;}
#include "media_list_wrappers.inc"
int main(void){
 cJSON *local=parse("[{\"id\":1,\"synced\":true},{\"id\":2},{\"id\":3,\"series\":true,\"synced\":true}]");
 cJSON *remote=parse("[{\"item_id\":4,\"title\":\"Remote\"},{\"series_id\":3},{\"item_id\":4}]");
 cJSON *merged=media_list_sync_merge(local,remote);assert(merged&&cJSON_GetArraySize(merged)==3);
 assert(cJSON_GetObjectItem(cJSON_GetArrayItem(merged,0),"id")->valueint==4);
 assert(cJSON_GetObjectItem(cJSON_GetArrayItem(merged,1),"id")->valueint==3);
 assert(cJSON_GetObjectItem(cJSON_GetArrayItem(merged,2),"id")->valueint==2);
 assert(cJSON_IsTrue(cJSON_GetObjectItem(cJSON_GetArrayItem(merged,0),"synced")));
 cJSON *empty=parse("[]"),*after=media_list_sync_merge(merged,empty);
 assert(after&&cJSON_GetArraySize(after)==1&&cJSON_GetObjectItem(cJSON_GetArrayItem(after,0),"id")->valueint==2);
 cJSON_Delete(after);cJSON_Delete(merged);
 cJSON *bad=parse("[{\"item_id\":1.5}]");assert(!media_list_sync_merge(local,bad));cJSON_Delete(bad);
 bad=parse("{}");assert(!media_list_sync_merge(local,bad));cJSON_Delete(bad);
 bad=parse("[{\"item_id\":2147483648}]");assert(!media_list_sync_merge(local,bad));cJSON_Delete(bad);
 bad=parse("[{\"item_id\":3},{\"series_id\":3}]");merged=media_list_sync_merge(empty,bad);
 assert(merged&&cJSON_GetArraySize(merged)==2);cJSON_Delete(merged);cJSON_Delete(bad);
 cJSON *full=cJSON_CreateArray();assert(full);
 for(int i=1;i<=64;i++){cJSON *item=cJSON_CreateObject();cJSON_AddNumberToObject(item,"item_id",i);cJSON_AddItemToArray(full,item);}
 merged=media_list_sync_merge(empty,full);assert(merged&&cJSON_GetArraySize(merged)==64);cJSON_Delete(merged);
 cJSON *pending=parse("[{\"id\":100}]");assert(!media_list_sync_merge(pending,full));cJSON_Delete(pending);
 cJSON *extra=cJSON_CreateObject();cJSON_AddNumberToObject(extra,"item_id",65);cJSON_AddItemToArray(full,extra);
 assert(!media_list_sync_merge(empty,full));cJSON_Delete(full);
 char *original=cJSON_PrintUnformatted(local);assert(original);
 cJSON_Hooks hooks={fault_alloc,free};cJSON_InitHooks(&hooks);
 int successful_allocations=0;
 for(int i=1;i<150;i++){
   allocations=0;fail_at=i;merged=media_list_sync_merge(local,remote);
   if(merged){successful_allocations=allocations;cJSON_Delete(merged);break;}
 }
 assert(successful_allocations>0);fail_at=0;cJSON_InitHooks(NULL);
 char *unchanged=cJSON_PrintUnformatted(local);assert(unchanged&&!strcmp(original,unchanged));free(original);free(unchanged);
 cJSON_Delete(local);cJSON_Delete(remote);cJSON_Delete(empty);
 test_list=parse("{\"items\":[{\"id\":1},{\"id\":1,\"series\":true}]}");
 assert(store_watchlater_confirm(0,1,1)==0&&saves==1);
 assert(!cJSON_HasObjectItem(cJSON_GetArrayItem(media_items(0),0),"synced"));
 assert(cJSON_IsTrue(cJSON_GetObjectItem(cJSON_GetArrayItem(media_items(0),1),"synced")));
 assert(store_watchlater_confirm(0,1,1)==0&&saves==2);
 assert(store_watchlater_confirm(0,99,0)==-1&&saves==2);
 empty=parse("[]");assert(store_watchlater_reconcile(0,empty)==0&&saves==3&&cJSON_GetArraySize(media_items(0))==1);
 bad=parse("{}");cJSON *known=media_items(0);
 assert(store_watchlater_reconcile(0,bad)==-1&&media_items(0)==known&&saves==3);
 assert(store_watchlater_reconcile(9,empty)==-1&&saves==3);
 cJSON_Delete(test_list);cJSON_Delete(empty);cJSON_Delete(bad);
 puts("PASS actual store wrappers: confirmation by ID+kind, confirmed deletion, invalid list/schema retains array and does not save.");
 puts("PASS watchlater: confirmed remote deletions, offline/legacy preservation, dedup by ID+kind, empty/invalid/64-item cap, every allocation failure transactional.");
 return 0;
}
