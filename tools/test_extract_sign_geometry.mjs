// Generated build fixture, not an edit to application code.
import { readFileSync, writeFileSync } from 'node:fs';
const source = readFileSync('source/player_ui.c', 'utf8');
const start = source.indexOf('static void draw_signs(');
const end = source.indexOf('static void draw_buffering(', start);
if (start < 0 || end < 0) throw new Error('draw_signs extraction boundaries not found');
writeFileSync('build/test_draw_signs.inc', source.slice(start, end));
