import {readFileSync,writeFileSync,mkdirSync} from 'node:fs';
import {spawnSync} from 'node:child_process';
import {resolve} from 'node:path';
const read=p=>readFileSync(p,'utf8').replace(/\r\n/g,'\n');
const main=read('source/main.c'),net=read('source/net.c'),text=read('source/text.c');
function section(s,a,b){const start=s.indexOf(a),end=s.indexOf(b,start);if(start<0||end<start)throw Error(a);return s.slice(start,end);}
mkdirSync('build',{recursive:true});
writeFileSync('build/app_cover.inc',section(main,'#define MAX_COV 3000','// ------------------------------------------------------------- card'));
writeFileSync('build/app_cover_suspend.inc',section(main,'static void cover_suspend_and_release(void) {','static int landing_fetch_thread('));
writeFileSync('build/app_topbar.inc',section(main,'static SDL_Rect g_topbar_tabs[NTABS];','static void draw_topbar(void) {'));
writeFileSync('build/app_text_fit.inc',section(text,'typedef struct { char input[1024]','// ---- cache de texturas'));
writeFileSync('build/app_net_write.inc',section(net,'static size_t write_cb(','struct file_download_ctx'));
writeFileSync('build/app_membuf.inc',section(read('include/net.h'),'struct membuf {','void membuf_free'));
writeFileSync('build/app_search_types.inc',section(main,'#define SEARCH_WINDOW_MAX 32','static char g_srchQuery'));
writeFileSync('build/app_search.inc',section(main,'static int srch_matches(','static void url_encode_utf8('));
for(const marker of ['navigation_repeat_press(&g_nav_repeat, b, SDL_GetTicks());','navigation_repeat_poll(&g_nav_repeat, dir, now)',
 'topbar_contains(&TOPBAR_SEARCH, x, y)', 'topbar_contains(&g_topbar_tabs[t], x, y)', 'free(g_cov[i].url);'])
 if(!main.includes(marker))throw Error('Missing integration: '+marker);
if(!main.includes('memset(&g_search_window, 0, sizeof(g_search_window));\n        if (g_search) cJSON_Delete(g_search);'))
 throw Error('Search JSON replacement must invalidate borrowed pointers before deletion');
for(const h of ['app_cover_host','app_navigation_host','app_search_host']){
 const exe=resolve(`build/${h}.exe`);
 let r=spawnSync(process.env.HOST_CC||'gcc',['-std=c11','-Wall','-Wextra','-Werror','-Iinclude','-Ibuild',`tools/${h}.c`,'source/cJSON.c','-lm','-o',exe],{encoding:'utf8',windowsHide:true,timeout:30000});
 if(r.status!==0)throw Error(r.stderr||r.error?.message);
 r=spawnSync(exe,[],{encoding:'utf8',windowsHide:true,timeout:30000});process.stdout.write(r.stdout||'');
 if(r.status!==0)throw Error(r.stderr||r.error?.message||`exit ${r.status}`);
}
