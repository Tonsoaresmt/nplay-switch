#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "cJSON.h"
#define SEARCH_FILTERS 4
static cJSON *g_search;
static int g_search_counts[4],g_search_counts_valid,g_srchFilter;
static unsigned matches_queries;
static const char *jstr(cJSON *item,const char *key){
    matches_queries++;
    cJSON *field=cJSON_GetObjectItem(item,key);return cJSON_IsString(field)?field->valuestring:NULL;
}
#include "app_search_types.inc"
#include "app_search.inc"
int main(void){
    g_search=cJSON_CreateObject();assert(g_search);
    cJSON *series=cJSON_AddArrayToObject(g_search,"series"),*items=cJSON_AddArrayToObject(g_search,"items");
    for(int i=0;i<5000;i++){
        cJSON *item=cJSON_CreateObject();assert(item);
        cJSON_AddNumberToObject(item,"id",i);
        cJSON_AddStringToObject(item,"search_scope",i<3000?(i%2?"anime":"series"):"movie");
        cJSON_AddItemToArray(i<3000?series:items,item);
    }
    int counts[]={5000,2000,1500,1500};
    for(int f=0;f<4;f++){
        g_srchFilter=f;assert(srch_count_for(f)==counts[f]);
        cJSON *expected[5000];unsigned char kinds[5000];int count=0;
        for(int i=0;i<5000;i++){
            if((f==1&&i<3000)||(f==2&&(i>=3000||i%2))||(f==3&&(i>=3000||!(i%2))))continue;
            expected[count]=cJSON_GetArrayItem(i<3000?series:items,i<3000?i:i-3000);
            kinds[count++]=(unsigned char)(i<3000);
        }
        assert(count==counts[f]);
        int starts[]={0,12,1499,2995,4999,6000};
        for(unsigned p=0;p<sizeof(starts)/sizeof(starts[0]);p++){
            int first=starts[p];srch_window(first,15);
            int visible=count-first;if(visible<0)visible=0;if(visible>15)visible=15;
            assert(g_search_window.count==visible);
            for(int i=0;i<visible;i++){
                int kind=-1;assert(srch_at(first+i,&kind)==expected[first+i]);assert(kind==kinds[first+i]);
            }
            unsigned before=matches_queries;
            for(int redraw=0;redraw<10000;redraw++)srch_window(first,15);
            assert(before==matches_queries);
        }
        int kind=-1;assert(srch_at(0,&kind)==expected[0]&&kind==kinds[0]);
        assert(!srch_at(count,&kind));
    }
    assert(srch_count_for(-1)==0&&srch_count_for(4)==0);
    srch_window(0,100);assert(g_search_window.count==SEARCH_WINDOW_MAX);
    srch_window(0,0);assert(g_search_window.count==0);
    memset(&g_search_window,0,sizeof(g_search_window));cJSON_Delete(g_search);
    g_search=cJSON_Parse("{\"series\":[],\"items\":[{\"id\":9000,\"search_scope\":\"movie\"}]}");
    g_search_counts_valid=0;g_srchFilter=1;assert(srch_count_for(1)==1);
    srch_window(0,15);int kind=-1;
    assert(g_search_window.count==1&&srch_at(0,&kind)==cJSON_GetArrayItem(cJSON_GetObjectItem(g_search,"items"),0)&&kind==0);
    memset(&g_search_window,0,sizeof(g_search_window));cJSON_Delete(g_search);g_search=NULL;
    puts("PASS actual search window: 5000 results, 4 filters, order/type/boundaries preserved; 240000 warm redraws without filtering JSON again; 32-pointer cap and replacement invalidation");
}
