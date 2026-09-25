#!/usr/bin/env node
// Confere a ligacao entre as telas/rotas do NRO e os handlers do backend.
// E um contrato estatico: nao substitui executar o NRO no console.
import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import { readFileSync } from 'node:fs';
import { resolve, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = resolve(fileURLToPath(new URL('..', import.meta.url)));
const backendRoot = process.env.NPLAY_BACKEND_ROOT || 'C:/iptv';
const backendRef = process.env.NPLAY_BACKEND_REF || 'origin/main';
const git = process.env.GIT_EXE || (process.platform === 'win32'
  ? 'C:/Program Files/Git/cmd/git.exe' : 'git');
const client = ['source/main.c', 'source/api.c', 'source/curl_avio.c',
  'source/screen_movie.c', 'source/update.c']
  .map((file) => readFileSync(join(root, file), 'utf8')).join('\n');

function backend(file) {
  if (backendRef === 'working-tree') return readFileSync(join(backendRoot, file), 'utf8');
  return execFileSync(git, ['show', `${backendRef}:${file}`], {
    cwd: backendRoot, encoding: 'utf8', maxBuffer: 16 * 1024 * 1024,
  });
}

const server = backend('src/server.js');
const modules = {
  auth: ['authRoutes', '/api/auth', 'src/routes/auth.js'],
  account: ['accountRoutes', '/api/account', 'src/routes/account.js'],
  catalog: ['catalogRoutes', '/api/catalog', 'src/routes/catalog.js'],
  search: ['catalogSearchRoutes', '/api/catalog', 'src/routes/catalog-search.js'],
  sync: ['syncRoutes', '/api/sync', 'src/routes/sync.js'],
  stream: ['streamRoutes', '/api', 'src/routes/stream.js'],
  hot: ['hotStreamRoutes', '/api', 'src/routes/hot-stream.js'],
  accel: ['accelRoutes', '/api', 'src/routes/accelerator.js'],
};
const sources = {};
for (const [name, [plugin, prefix, file]] of Object.entries(modules)) {
  assert.ok(server.includes(`app.register(${plugin}, { prefix: '${prefix}' })`),
    `${plugin} nao esta registrado com prefixo ${prefix}`);
  sources[name] = backend(file);
}

// [area da tela, fragmento usado no cliente, modulo, verbo, caminho relativo]
const routes = [
  ['Entrada', '/api/auth/login', 'auth', 'post', '/login'],
  ['Entrada', '/api/account/profiles', 'account', 'get', '/profiles'],
  ['Configuracoes', '/api/account/me', 'account', 'get', '/me'],
  ['Configuracoes', '/api/auth/me', 'auth', 'get', '/me'],
  ['Configuracoes', '/api/account/prefs', 'account', 'put', '/prefs'],
  ['Inicio', '/api/catalog/home', 'catalog', 'get', '/home'],
  ['Filmes', '/api/catalog/tab-home?tab=movie', 'catalog', 'get', '/tab-home'],
  ['Series', '/api/catalog/tab-home?tab=series', 'catalog', 'get', '/tab-home'],
  ['Animes', '/api/catalog/anime-home', 'catalog', 'get', '/anime-home'],
  ['Sagas', '/api/catalog/sagas', 'catalog', 'get', '/sagas'],
  ['Sagas', '/api/catalog/sagas/%.200s', 'catalog', 'get', '/sagas/:slug'],
  ['Busca', '/api/catalog/search-v2?q=', 'search', 'get', '/search-v2'],
  ['Detalhe de serie', '/api/catalog/series/%d', 'catalog', 'get', '/series/:id'],
  ['Detalhe de filme', '/api/catalog/movie/%d/info', 'catalog', 'get', '/movie/:id/info'],
  ['Minha lista', '/api/sync/favorites', 'sync', 'get', '/favorites'],
  ['Minha lista', '/api/sync/favorites', 'sync', 'post', '/favorites'],
  ['Minha lista', '/api/sync/favorites', 'sync', 'delete', '/favorites'],
  ['Historico', '/api/sync/progress', 'sync', 'get', '/progress'],
  ['Historico', '/api/sync/progress/%d', 'sync', 'get', '/progress/:itemId'],
  ['Historico', '/api/sync/progress', 'sync', 'post', '/progress'],
  ['Player', '/api/sync/item-watched', 'sync', 'post', '/item-watched'],
  ['Listas', '/api/sync/watchlater', 'sync', 'get', '/watchlater'],
  ['Listas', '/api/sync/watchlater', 'sync', 'post', '/watchlater'],
  ['Listas', '/api/sync/watchlater', 'sync', 'delete', '/watchlater'],
  ['Preparados', '/api/accel/status', 'accel', 'get', '/accel/status'],
  ['Preparados', '/api/accel/jobs', 'accel', 'get', '/accel/jobs'],
  ['Preparados', '/api/accel/jobs/%d', 'accel', 'get', '/accel/jobs/:itemId'],
  ['Preparados', '/api/accel/jobs/%d', 'accel', 'delete', '/accel/jobs/:itemId'],
  ['Preparados', '/api/accel/download/%d', 'accel', 'post', '/accel/download/:itemId'],
  ['Preparados', '/api/accel/download-batch', 'accel', 'post', '/accel/download-batch'],
  ['Player', '/api/stream/%d', 'stream', 'post', '/stream/:itemId'],
  ['Player TorBox', '/api/stream/hot/%d', 'hot', 'post', '/stream/hot/:itemId'],
  ['Player', '/api/stream/%d/variants', 'stream', 'get', '/stream/:itemId/variants'],
  ['Player', '/api/stream/session/%d/heartbeat', 'stream', 'post', '/stream/session/:sessionId/heartbeat'],
  ['Player', '/api/stream/session/%d/refresh', 'stream', 'post', '/stream/session/:sessionId/refresh'],
  ['Player', '/api/stream/session/%d/fail', 'stream', 'post', '/stream/session/:sessionId/fail'],
  ['Player', '/api/stream/%d/stop', 'stream', 'post', '/stream/:itemId/stop'],
];
for (const [area, needle, module, method, route] of routes) {
  assert.ok(client.includes(needle), `${area}: cliente nao usa ${needle}`);
  assert.ok(sources[module].includes(`app.${method}('${route}'`),
    `${area}: backend nao registra ${method.toUpperCase()} ${route}`);
}

const main = readFileSync(join(root, 'source/main.c'), 'utf8');
assert.ok(main.includes('#define TAB_SAGAS 4') && main.includes('#define TAB_DOWNLOADS 5') &&
  main.includes('#define NTABS 6') && main.includes('#define SEARCH_FILTERS 4') &&
  !main.includes('/api/catalog/tab-home?tab=dorama') &&
  !main.includes('"Doramas"'),
  'Dorama reapareceu na navegacao ou os indices de aba/filtro divergiram');
const ui = readFileSync(join(root, 'source/ui.h'), 'utf8');
const player = readFileSync(join(root, 'source/player.c'), 'utf8');
const screens = [...ui.matchAll(/\bSC_[A-Z]+\b/g)].map((match) => match[0]);
assert.equal(new Set(screens).size, 9, 'Inventario de telas mudou: atualize esta auditoria');
for (const screen of new Set(screens)) {
  assert.ok(main.includes(`g_screen == ${screen}`), `${screen}: sem rota de desenho/entrada`);
}
for (const field of ['readyMovieShelves', 'readySeriesShelves']) {
  assert.ok(sources.catalog.includes(field) && main.includes(`cJSON_GetObjectItem(g_land, "${field}")`),
    `Home: prateleira ${field} devolvida pelo backend mas nao exibida no Switch`);
}
assert.ok(sources.catalog.includes('progress_updated_at') && main.includes('progress_updated_at'),
  'Detalhe de serie perdeu o criterio de retomada mais recente');
assert.ok(main.includes('handle_history_touch(x, y)'),
  'Historico voltou a abrir a selecao antiga em qualquer toque');
assert.ok(player.includes('SDL_ThreadID() == watch->render_thread') &&
  player.includes('draw_center_state(watch->renderer'),
  'A animacao de preparacao nao e redesenhada na thread do player');
assert.ok(player.indexOf('ui_popcorn_release();') > player.indexOf('if (!logged_first_present) {'),
  'Atlas de pipoca liberado antes do primeiro quadro');

console.log(`SWITCH ROUTES OK: ${screens.length} telas, ${routes.length} contratos HTTP; backend ${backendRef}`);
