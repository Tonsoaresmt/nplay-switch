// Unit-test the actual Linux suite assertions, without claiming full Linux playback.
import { readFileSync, writeFileSync, mkdirSync } from 'node:fs';
import { spawnSync } from 'node:child_process';
import { resolve } from 'node:path';
const source = readFileSync('tools/host_player/suite.sh', 'utf8').replace(/\r\n/g, '\n');
const a = source.indexOf('check() {'), b = source.indexOf('\nURL=', a);
if (a < 0 || b < a) throw Error('Review host suite assertion extraction');
const root = resolve('build/host-suite-guards'); mkdirSync(root, { recursive: true });
const good = '0.1 RESULT chosen=0 rc=0\nSUMMARY video_frames=10 presents=11\n';
writeFileSync(`${root}/suite_good.log`, good);
writeFileSync(`${root}/suite_crash.log`, good);
writeFileSync(`${root}/suite_timeout.log`, good);
writeFileSync(`${root}/suite_empty.log`, '');
writeFileSync(`${root}/suite_no_frames.log`, 'RESULT chosen=0 rc=0\nSUMMARY video_frames=0 presents=1\n');
writeFileSync(`${root}/suite_error.log`, good + 'Invalid NAL\nmanifest-load-fail\n');
writeFileSync(`${root}/suite_bad_pattern.log`, good);
writeFileSync(`${root}/check.sh`, `declare -A RUN_STATUS
FAILS=0
${source.slice(a, b)}
RUN_STATUS[good]=0; RUN_STATUS[crash]=1; RUN_STATUS[timeout]=124
RUN_STATUS[empty]=0; RUN_STATUS[no_frames]=0; RUN_STATUS[error]=0; RUN_STATUS[bad_pattern]=0
check good "valid negative assertion" "!manifest-load-fail"
check good "valid NAL assertion" "@nal"
[[ $FAILS == 0 ]] || exit 1
check crash "crash rejected" "!manifest-load-fail"
check timeout "timeout rejected" "!manifest-load-fail"
check empty "empty log rejected" "@nal"
check no_frames "zero frames rejected" "!manifest-load-fail"
check error "NAL before exit rejected" "@nal"
check error "explicit error rejected" "!manifest-load-fail"
check bad_pattern "missing success rejected" "controls/resume"
[[ $FAILS == 7 ]] || exit 1
echo "HOST SUITE GUARDS OK: crash, timeout, empty/zero-frame logs, errors and missing positives cannot pass"
`);
const run = spawnSync(process.env.HOST_BASH || (process.platform === 'win32' ? 'C:/devkitPro/msys2/usr/bin/bash.exe' : 'bash'), ['check.sh'], { cwd: root, encoding: 'utf8', windowsHide: true, timeout: 10000 });
if (run.status !== 0) throw Error(run.stderr || run.error?.message || run.stdout);
process.stdout.write(run.stdout);
