# Legenda que some no meio do anime — 0.12.47

Data: 04/10/2026. Relato: assistindo anime do R2 (audio japones), a legenda
"simplesmente parou de aparecer". Reproduzido em `tools/host_player/` com o
player real (FFmpeg 7.1) e legendas no formato que o `hls-preparer.js` publica
(`-c:s webvtt` a partir do ASS do fansub).

## Causas encontradas

1. **Teto de 8192 cues (causa principal, reproduzida na 0.12.44).** Fansub de
   anime traz karaoke/typesetting: cada silaba, camada e letra vira um cue no
   WebVTT (dezenas de milhares). O armazenamento guardava 512 bytes por cue e
   parava em 8192; o resto era descartado em silencio. No teste com karaoke
   entre 20 s e 110 s, a 0.12.44 mostrou as falas ate ~38 s e **nenhuma fala
   depois**. Alem disso, cada cue de 528 bytes levava a ~4 MB so de legenda e a
   realocacao podia falhar com pouca memoria, cortando ainda antes.
2. **Karaoke e placas cobriam a fala.** Todos os cues ativos eram empilhados
   em ordem de inicio e o HUD mostra 4 linhas: durante o karaoke a tela enchia
   de silabas soltas; com placas longas a fala (ultima) era cortada por "...".
3. **Falha momentanea desligava a legenda.** Se o download do VTT falhasse
   (Wi-Fi, CDN), a faixa era desligada sem aviso pelo resto do episodio. Cada
   salto/troca de audio/recuperacao baixava o VTT de novo (acima de 1 MB ele nem
   entra no cache curto), multiplicando a chance de falha.
4. **Torrent (legenda progressiva) sem nova tentativa.** Uma queda da conexao
   da extracao deixava so os cues ja recebidos.
5. VTT acima de 4 MB (karaoke extremo) nao baixava; desenhos vetoriais do ASS
   (`m 0 0 l ...`) apareciam como texto.

## Correcoes

- `source/subtitle_store.c` (novo, testavel): 16 bytes por cue + texto em area
  unica; sem teto pratico (200 mil cues / 4 MB de texto); funde repeticoes
  (camadas, quadro a quadro, reenvio); ordena na insercao; busca binaria;
  ignora desenhos vetoriais. Exibicao: repetidos uma vez; silabas soltas so
  quando nao ha fala; prioridade para o cue mais curto (fala) dentro de 4
  linhas; placas longas em cima, fala embaixo.
- Legenda do master guardada durante a reproducao (`subtitle_session_*`):
  salto, troca de audio e recuperacao reaproveitam os cues, sem novo download.
- Falha ao baixar: a faixa continua escolhida e o download e repetido em 2, 5,
  10 e 20 s, com aviso. Escolher a mesma faixa no painel (X) tenta na hora.
- Torrent: ate 3 novas tentativas da legenda progressiva; reenvio nao duplica.
- Teto de recurso textual 4 -> 8 MB (cresce sob demanda).
- Recuperacao que cai numa fonte sem legenda avisa "Esta fonte nao tem legendas".

## Medicoes (host, mesmo conteudo)

| Cenario | 0.12.44 (instalada) | 0.12.46 | 0.12.47 |
|---|---|---|---|
| Falas depois do karaoke (45 mil cues) | nenhuma apos ~38 s | nenhuma (8192) | todas |
| Fala durante o karaoke | fala + dezenas de silabas | fala + silabas | so a fala |
| Fala sob 6 placas | cortada | cortada (captura) | ultima linha |
| 1 falha de rede no VTT | sem legenda no episodio | sem legenda | volta em ~2,5 s |
| Salto apos carregar | baixa de novo | baixa de novo | reaproveita |
| Memoria (33 mil cues) | ~17 MB (se coubesse) | ~4,3 MB ate cortar | 1,3 MB |

## Testes

- `tools/test_subtitle_store.c` (no `validate_release.ps1`).
- `tools/host_player/make_subtitle_fixtures.py` gera `r2sub` (karaoke e placas)
  e `suite.sh` ganhou 8 verificacoes (karaoke, lixo, nova tentativa R2, cache no
  salto, nova tentativa do torrent). `latency_server.py --fail-match` simula a
  falha so na legenda.
- O harness registra `SUB pos=... [texto]` (texto entregue ao HUD).

## Pendente no Switch

Abrir um anime com karaoke (fansub) em Legendado e assistir a abertura e o
meio do episodio; fazer saltos; desligar o Wi-Fi por alguns segundos. Se a
legenda sumir, enviar o trace: procurar `subtitle/loaded cues=`, `truncated`,
`manifest-load-fail`, `subtitle/retry`, `session-reuse`.
