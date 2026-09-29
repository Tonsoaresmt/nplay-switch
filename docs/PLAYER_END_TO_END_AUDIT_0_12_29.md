# Auditoria ponta a ponta do player — 0.12.29

Data: 29/09/2026. Escopo: NRO SDL2/FFmpeg/libcurl, sem alterar a arquitetura.

## Fluxo verificado

1. A interface resolve `/api/stream/:id` fora da thread SDL e permite cancelar.
2. `player_run` recebe uma fotografia completa da fonte e preserva sessao,
   source, container, entrega, progresso e preferencia de faixas.
3. HLS abre manifesto, renditions e segmentos pelo AVIO libcurl; arquivo remoto
   usa a abertura FFmpeg validada e arquivo local usa `sdmc:/`.
4. A thread de demux tem fila limitada, barreira para seek/troca de faixa e
   cancelamento atomico. A thread SDL continua processando controles e desenho.
5. Video tenta NVDEC e recua para CPU; audio e convertido para S16/48 kHz;
   legendas textuais entram numa fila temporal limitada.
6. Pausa, seek, audio e legenda reancoram clocks e so publicam uma nova geracao
   depois de a operacao ser aceita. Falha parcial provoca reabertura controlada.
7. Queda recuperavel renova a sessao ou troca a fonte, preservando posicao e
   faixas. Progresso, heartbeat e encerramento cuidam do contrato da conta.
8. Todo caminho libera demux, audio, codecs, frames, texturas, AVFormat e AVIO.

## Falhas encontradas e corrigidas

### Saida presa na rede

O heartbeat podia estar dentro de um POST de ate 6 s quando o usuario saisse.
Depois do `join`, a thread SDL ainda fazia outro POST de progresso e o fluxo
fazia `/stop` sincrono. A soma podia deixar voltar, proximo episodio e erros
aparentemente congelados.

Na 0.12.29, progresso final e `/stop` pertencem ao worker. A interface concede
ate 1.200 ms e entao cancela libcurl por callback. Um salvamento ja confirmado
na mesma posicao nao e repetido. Se a thread inicial nao existir, uma tentativa
curta de fechamento e criada sem recorrer ao POST sincrono.

### Recuperacao que fingia ser responsiva

`refresh`, consulta de variantes, `fail` e nova resolucao eram executados na
thread SDL. A tela mostrava "Recuperando sessao", mas deixava de animar e `B`
nao era lido por ate 8 s em cada chamada.

Agora cada tentativa roda em `player-recovery`, todas as etapas HTTP recebem o
mesmo cancelamento atomico e a thread SDL redesenha a animacao a cada 16 ms.
`B` e `-` cancelam inclusive durante DNS/TLS/transferencia. O erro seguro da
thread e copiado para o diagnostico do player.

## Aspectos revisados sem mudanca arriscada

- A fila de demux permanece em 32 pacotes/4 MiB e o AVIO HLS em 4 MiB por
  recurso. Ambos possuem limites e limpeza explicita.
- A fila SDL de audio registra maximo, underruns e eventos acima de 1,5 s. Nao
  foi imposto descarte artificial: apagar audio para reduzir a fila criaria
  silencio futuro e dessincronizacao. O proximo passo correto depende de medidas
  no Switch real, nao de um teto arbitrario.
- Pausa para antes do consumo da fila de demux; retomar reancora o relogio.
- Seek e troca de faixa usam barreira, limpam pacotes antigos e possuem fallback
  de 20 s. Legenda desativada nao faz seek desnecessario.
- Faixas sao limitadas a 16 audios e 16 legendas, com vetores e HUD dimensionados
  para esses mesmos limites. Cues de legenda possuem fila fixa de 32 entradas.
- Abertura, buffering e operacao interna possuem dono unico do renderer.
- NV12 direto tem fallback YUV420P; codecs e alocacoes sao checados antes do uso.
- Fim natural, saida do usuario, proximo episodio e erro convergem pela mesma
  limpeza. Uma fonte que termina antes do primeiro quadro nao e marcada concluida.

## Validacao local

- Build ARM64: `-Wall -Wextra -Werror`.
- Politica nova: `test_player_sync` cobre progresso final, deduplicacao e limite
  de 1.200 ms.
- A suite de release cobre relogio, loader, politica PT-BR, proximo episodio,
  HLS multifaixa, WebVTT, fila de legenda, fluxo de episodio, hot stream e remux.

## Obrigatorio no hardware

O host nao reproduz consumo real de NVDEC, Wi-Fi do Switch, driver SDL de audio
nem comportamento do hbmenu. Antes de chamar a rodada de definitiva, testar:

1. Filme R2 por 10 min: pausa/retoma, quatro seeks e saida por `B`.
2. Serie multiaudio: PT-BR inicial, ingles, PT-BR, legenda e proximo episodio.
3. Desligar Wi-Fi durante a reproducao; confirmar animacao fluida e cancelar a
   recuperacao com `B`; repetir deixando a rede voltar.
4. Sair logo apos pausar e logo apos um seek; o retorno nao deve passar de cerca
   de 1,2 s por sincronizacao local (fora transicoes do hbmenu).
5. Fotografar o Diagnostico e guardar `.nplay-player-trace.log` se houver falha.

Nao declarar NVDEC, driver de audio ou rede fisica validados apenas com esta
suite local.
