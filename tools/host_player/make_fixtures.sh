#!/bin/bash
# Gera conteudos de teste (6 min) no formato real de producao:
# - r2/: HLS igual ao empacotador do backend (hls-preparer.js): fMP4 6 s,
#   var_stream_map com audio_0_jpn/audio_1_por, legendas VTT de arquivo unico.
# - anime.mp4: MP4 progressivo (provedor de anime).
# - api/...: remux de torrent (fMP4 frag_keyframe sequencial), sonda e legenda ASS->VTT.
set -e
HERE=$(cd "$(dirname "$0")" && pwd); REPO=$(cd "$HERE/../.." && pwd)
F=${HOST_PLAYER_FIXTURES:-$REPO/build/host_player/fixtures}
SID=hotsid123456789012345678901234567890
mkdir -p "$F/src" "$F/r2" "$F/api/stream/hot/$SID/subtitles" "$F/api/media/hot/$SID"
cd "$F/src"
python3 - <<'PY'
def srt(t):
    h, m, s = int(t // 3600), int(t % 3600 // 60), t % 60
    return f"{h:02d}:{m:02d}:{s:06.3f}".replace('.', ',')
def ass(t):
    h, m, s = int(t // 3600), int(t % 3600 // 60), t % 60
    return f"{h}:{m:02d}:{s:05.2f}"
for lang, txt in (("por", "Legenda em portugues"), ("eng", "English subtitle")):
    with open(f"{lang}.srt", "w") as f:
        for n, t in enumerate(range(2, 360, 5), 1):
            f.write(f"{n}\n{srt(t)} --> {srt(t + 3)}\n{txt} {n} ({t}s)\n\n")
head = """[Script Info]
ScriptType: v4.00+
PlayResX: 1920
PlayResY: 1080

[V4+ Styles]
Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding
Style: Default,Arial,60,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,3,0,2,10,10,40,1
Style: Sign,Arial,50,&H00FFFF00,&H000000FF,&H00000000,&H00000000,1,0,0,0,100,100,0,0,1,2,0,8,10,10,40,1

[Events]
Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text
"""
lines = []
for n, t in enumerate(range(2, 360, 4), 1):
    lines.append(f"Dialogue: 0,{ass(t)},{ass(t + 3)},Default,,0,0,0,,{{\\i1}}Fala {n}{{\\i0}} em {t}s\\Nsegunda linha")
    if n % 5 == 0:
        lines.append(f"Dialogue: 0,{ass(t)},{ass(t + 2)},Sign,,0,0,0,,{{\\an8}}Placa {n}")
open("anime.ass", "w").write(head + "\n".join(lines) + "\n")
PY
Q="-hide_banner -loglevel error -y"
ffmpeg $Q -f lavfi -i "testsrc2=size=1280x720:rate=24:duration=360" \
    -f lavfi -i "sine=frequency=440:sample_rate=48000:duration=360" \
    -f lavfi -i "sine=frequency=880:sample_rate=48000:duration=360" -i por.srt -i eng.srt \
    -map 0:v -map 1:a -map 2:a -map 3 -map 4 -vf "noise=alls=12:allf=t" -c:v libx264 -preset veryfast \
    -b:v 4M -maxrate 5M -bufsize 8M -pix_fmt yuv420p -g 48 -c:a aac -b:a 160k -ac 2 -c:s srt \
    -metadata:s:a:0 language=jpn -metadata:s:a:1 language=por \
    -metadata:s:s:0 language=por -metadata:s:s:1 language=eng source.mkv
# R2: mesmos argumentos do backend (modo copy).
cd "$F/r2"
ffmpeg $Q -fflags +genpts+igndts -i ../src/source.mkv -avoid_negative_ts make_zero -map 0:v:0 -map 0:a:0 \
    -map 0:a:1 -sn -dn -map_metadata -1 -map_chapters -1 -c:v copy -c:a copy -f hls -hls_time 6 \
    -hls_playlist_type vod -hls_segment_type fmp4 -hls_flags independent_segments+temp_file \
    -var_stream_map "a:0,agroup:audio,default:yes,name:audio_0_jpn,language:jpn a:1,agroup:audio,default:no,name:audio_1_por,language:por v:0,agroup:audio,name:video" \
    -master_pl_name index.m3u8 -hls_fmp4_init_filename 'init-%v.mp4' \
    -hls_segment_filename "$F/r2/segment-%v-%06d.m4s" "$F/r2/stream-%v.m3u8"
for i in 0 1; do
    ffmpeg $Q -i ../src/source.mkv -map 0:s:$i -c:s webvtt subtitle-$i.vtt
    printf '#EXTM3U\n#EXT-X-VERSION:3\n#EXT-X-PLAYLIST-TYPE:VOD\n#EXT-X-TARGETDURATION:360\n#EXT-X-MEDIA-SEQUENCE:0\n#EXTINF:360.000,\nsubtitle-%d.vtt\n#EXT-X-ENDLIST\n' $i > subtitle-$i.m3u8
done
python3 - <<'PY'
m = open('index.m3u8').read().rstrip('\n')
if '#EXT-X-STREAM-INF' not in m:   # ensureMasterVideoVariant do backend
    m += ('\n#EXT-X-STREAM-INF:BANDWIDTH=5200000,AVERAGE-BANDWIDTH=4500000,CODECS="avc1.64001f,mp4a.40.2",'
          'FRAME-RATE=24.000,RESOLUTION=1280x720,AUDIO="group_audio"\nstream-video.m3u8\n')
lines = m.split('\n')
media = ['#EXT-X-MEDIA:TYPE=SUBTITLES,GROUP-ID="subs",NAME="Português (Brasil)",LANGUAGE="por",DEFAULT=YES,AUTOSELECT=YES,FORCED=NO,URI="subtitle-0.m3u8"',
         '#EXT-X-MEDIA:TYPE=SUBTITLES,GROUP-ID="subs",NAME="English",LANGUAGE="eng",DEFAULT=NO,AUTOSELECT=YES,FORCED=NO,URI="subtitle-1.m3u8"']
i = next(k for k, l in enumerate(lines) if l.startswith('#EXT-X-STREAM-INF'))
lines[i:i] = media
lines = [l + ',SUBTITLES="subs"' if l.startswith('#EXT-X-STREAM-INF') else l for l in lines]
open('index.m3u8', 'w').write('\n'.join(lines))
PY
cd "$F/src"
ffmpeg $Q -i source.mkv -map 0:v -map 0:a:0 -c copy -movflags +faststart "$F/anime.mp4"
ffmpeg $Q -i source.mkv -i anime.ass -map 0:v -map 0:a:0 -map 1 -c copy -c:s ass \
    -metadata:s:s:0 language=por anime_hot.mkv
ffmpeg $Q -i anime_hot.mkv -map 0:v -map 0:a -c copy \
    -movflags empty_moov+frag_keyframe+default_base_moof+omit_tfhd_offset -f mp4 "$F/api/media/hot/$SID/video"
ffmpeg $Q -i anime_hot.mkv -map 0:s:0 -c:s webvtt "$F/api/stream/hot/$SID/subtitles/2.vtt"
printf '{"ok":true,"video":[{"index":0,"codec":"h264","width":1280,"height":720}],"audio":[{"index":1,"codec":"aac","language":"jpn","title":null}],"subtitles":[{"index":2,"codec":"ass","language":"por","title":"Portugues","label":"Portugues"}],"duration":360}\n' \
    > "$F/api/stream/hot/$SID/probe"
echo "conteudos em $F"
