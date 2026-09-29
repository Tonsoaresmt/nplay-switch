# Nplay Switch 0.12.25

Release de consolidacao do player, voltada aos travamentos ao pausar, buscar e
trocar faixas, e ao painel de legendas vazio em fontes HLS.

## Mudancas

- Leitura/demux de rede saiu da thread SDL e usa uma fila limitada a 32 pacotes
  ou 4 MB. Controles, HUD e cancelamento continuam respondendo durante espera de
  playlist ou segmento.
- Pausar deixa o prefetch atingir o limite e dormir; retomar consome a mesma fila
  sem criar outra pipeline ou permitir crescimento indefinido de memoria.
- Seek de 10/60 segundos, timeline e toque passam por reabertura controlada da
  fonte no ponto escolhido. O demuxer ativo nunca e reposicionado enquanto outra
  leitura esta em andamento.
- Troca de audio e legenda usa a mesma reabertura segura. Audio manual preserva
  indice exato; legenda preserva a faixa exata ou o estado desligado.
- O manifesto HLS raiz e inspecionado em memoria. Quando anuncia legendas que o
  FFmpeg ainda nao expôs, o probe necessario e executado; quando ja expos todas,
  a abertura rapida permanece ativa.
- O relogio do video nao contabiliza como bloqueio o tempo de I/O ocorrido no
  worker, evitando atraso artificial depois de segmentos lentos.

## Validacao local

- `tools/validate_release.ps1`: aprovado.
- Build ARM64 completo: sem erros nem avisos.
- Politica de audio, parser do manifesto HLS, fila de legendas, relogio, fluxo de
  episodios, contrato da API e remux chunked: aprovados.
- Artefato: `Nplay.nro`, 24.147.791 bytes.
- SHA-256: `0887b0b86fc0b57feb2d8d87ae5f0cbea86cd5b01e1fefd7d5cca8d9ae05e35f`.

## Teste obrigatorio no Switch

1. Filme HLS: reproduzir por 60 s, pausar 30 s, retomar e usar L/R/ZL/ZR.
2. Abrir timeline, cancelar com B; repetir e confirmar com A.
3. Alternar todas as faixas de audio e confirmar retorno no mesmo ponto.
4. Abrir legendas, selecionar PT-BR, desligar e ligar novamente.
5. Repetir em episodio de serie/anime e pausar por 120 s.
6. Em qualquer falha, fotografar Configuracoes > X Diagnostico e informar titulo
   e minuto. O trace esperado contem `demux worker-start`, `controlled-restart`
   e `hls-master renditions`.

Compilacao e simulacoes nao substituem esse teste em hardware real.
