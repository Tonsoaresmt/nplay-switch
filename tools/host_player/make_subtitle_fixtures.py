#!/usr/bin/env python3
"""Legendas de anime "reais" para o pacote R2 do host (fixtures/r2sub).

Fansubs convertidos de ASS para WebVTT (hls-preparer.js usa `-c:s webvtt`)
trazem o karaoke/typesetting como milhares de eventos: cada silaba, camada e
letra vira um cue. Tambem ha placas longas exibidas junto com o dialogo.

- subtitle-0.vtt (PT): fala a cada 5 s + karaoke de 20 s a 110 s com 5 camadas
  a cada 10 ms (~45 mil cues, >1 MB, nao cabe no cache curto de metadados).
- subtitle-1.vtt (EN): fala a cada 5 s + seis placas de 30 s sobrepostas.
Video/audio sao os mesmos de fixtures/r2 (links simbolicos)."""
import os, sys

root = sys.argv[1] if len(sys.argv) > 1 else 'build/host_player/fixtures'
src, dst = os.path.join(root, 'r2'), os.path.join(root, 'r2sub')
os.makedirs(dst, exist_ok=True)
for name in os.listdir(src):
    if name.startswith('subtitle-') or name == 'index.m3u8':
        continue
    link = os.path.join(dst, name)
    if not os.path.lexists(link):
        os.symlink(os.path.join('..', 'r2', name), link)

def ts(t):
    m, s = divmod(t, 60)
    return '%02d:%06.3f' % (int(m), s)

def write_vtt(name, cues):
    cues.sort(key=lambda c: c[0])
    with open(os.path.join(dst, name), 'w', encoding='utf-8') as f:
        f.write('WEBVTT\n\n')
        for start, end, text in cues:
            f.write('%s --> %s\n%s\n\n' % (ts(start), ts(end), text))
    with open(os.path.join(dst, name.replace('.vtt', '.m3u8')), 'w') as f:
        f.write('#EXTM3U\n#EXT-X-VERSION:3\n#EXT-X-PLAYLIST-TYPE:VOD\n#EXT-X-TARGETDURATION:360\n'
                '#EXT-X-MEDIA-SEQUENCE:0\n#EXTINF:360.000,\n%s\n#EXT-X-ENDLIST\n' % name)

dialogue = [(t + 0.5, t + 3.5, 'Fala %d (%ds)' % (t // 5 + 1, t)) for t in range(0, 356, 5)]
# Kana pseudoaleatorio como em karaoke real: quase nenhum cue identico encosta
# no anterior, entao a fusao de repetidos nao reduz a contagem.
kana = list('あいうえおかきくけこさしすせそたちつてとなにぬねのはひふへほまみむめもやゆよらりるれろわをん')
seed = 7
karaoke = []
for i in range(9000):  # 20 s .. 110 s, a cada 10 ms
    t = 20 + i * 0.01
    for layer in range(5):
        seed = (seed * 1103515245 + 12345) % (1 << 32)
        karaoke.append((t, t + 0.3, kana[(seed >> 16) % len(kana)]))
write_vtt('subtitle-0.vtt', dialogue + karaoke)

lines = [(t + 0.5, t + 3.5, 'Line %d (%ds)' % (t // 5 + 1, t)) for t in range(0, 356, 5)]
signs = []
for k, label in enumerate('ABCDEF'):
    for t in range(0, 360, 30):
        signs.append((t + k * 0.2, t + 30, 'SIGN %s placa da cena %d' % (label, t // 30)))
write_vtt('subtitle-1.vtt', lines + signs)

master = open(os.path.join(src, 'index.m3u8'), encoding='utf-8').read()
open(os.path.join(dst, 'index.m3u8'), 'w', encoding='utf-8').write(master)
print('r2sub: %d cues PT (karaoke), %d cues EN (placas)' % (len(dialogue) + len(karaoke), len(lines) + len(signs)))
