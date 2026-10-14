# Hibernacao e player preso em reconexao — 13/10/2026

## Escopo

Usuario pediu investigacao. Base publicada0.12.53/3d36eb6. Nenhum codigo do
app, versao, NRO, sessao real, banco ou backend foi alterado. Foram criados
apenas este relatorio e harness diagnostico. Nao e uma correcao/publicacao.

Relato: episodio toca; usuario pausa e hiberna; ao retornar nao progride e
fica reconectando; sair e escolher o mesmo episodio resolve.
Titulo/provedor/tempo adormecido e logs novos ainda nao fornecidos.

## Defeito demonstrado: sessao R2 encerrada

`on_player_renew` em main.c, para delivery=R2/session_id>0, chama exclusivamente
`api_refresh_playback_cancel`. A rota refresh usa a sessao numerica antiga.
Se o servidor devolve404 Sessao encerrada/invalida, o callback retorna-1;
nenhum POST/stream/:item e tentado nesse caminho.

O supervisor repete esse callback ate4vezes. Se nenhuma renovacao resolve,
encerra a recuperacao; a alternativa de fonte so e escolhida em outra rodada
de pipeline, nao apos falhar essas quatro resolucoes. Portanto nao e correto
chamar esse loop de infinito por codigo: ele pode demorar, mas e limitado.

O backend implantado c9d0f77 responde404 quando o registro inexiste ou quando
UPDATE...WHERE active=1 nao altera linha. Sessao ativa depende de heartbeat;
admissao de outro Play encerra registros calados ha90s. Dormir90s NAO prova
que a sessao foi encerrada automaticamente em todo caso: o refresh em si nao
exige last_heartbeat<90s, exige active=1. Nao confundir TTL, link e plano.

Ao escolher o episodio novamente, o resolvedor chama POST/stream/:item; esse
caminho faz nova admissao/autorizacao e pode criar outra sessao/link. Essa
assimetria corresponde ao relato, mas nao comprova o incidente sem trace.

## Reproducao

`HOST_CC=gcc node tools/audit_sleep_resume.mjs` extrai on_player_renew real e
compila source/api.c/cJSON/hot_subtitles reais, com HTTP/SDL/servidor simulados.

- Sessao ativa: refresh funciona, conserva session77.
- Sessao fechada: quatro chamadas renovam77; zero nova resolucao.
- Controle: reabrir resolve o mesmo item como session78; checkpoint3000s
  conservado pela politica real de posicao (nao e seek/decodificacao real).
- Conta expirada: tanto refresh quanto resolucao devolvem401/expired. Nunca
  usar essa recuperacao para burlar validade, plano, dispositivo ou telas.
- `test_player_supervisor.mjs` passou, inclusive conservacao de pausa/posicao.

Exit0 do AUDIT significa defeito reproduzido, NAO aprovacao do player.
Nao incorporar como criterio verde de release; converter assertions ao
comportamento corrigido quando a implementacao for autorizada.

## Hibernacao nao e queda curta de Wi-Fi

- NRO nao registra appletHook/OnResume nem geracao de recuperacao de wake.
- SDK instalada oferece AppletHookType_OnResume; SDL_switchvideo.o importa
  appletMainLoop. Nao afirmar que mensagens do applet nunca sao processadas:
  o pump SDL participa, mas nao existe politica de retomada propria no app.
- Pools libcurl/AVIO, estado de erro do demux e clocks continuam sendo os da
  tentativa anterior. Falha de rede apos wake depende da recuperacao ordinaria.
- Flag media-playback evita auto-sleep/dimming, nao e mecanismo de retomada
  nem justificativa para impedir hibernacao manual.
- TorBox MKV sequencial continua sem renew_cb/retomada completa; este caminho
  e outra limitacao, ja documentada na .53. Nao atribuir ao R2 sem identificar
  o descriptor do episodio real.

Referencia primaria:
https://switchbrew.github.io/libnx/applet_8h.html (OnResume/focus/media-playback).

## Correcao indicada, ainda nao implementada

1. Tratamento de wake: hook apenas sinaliza estado atomico, sem rede/SDL/FFmpeg
   dentro dele. Na thread dona, conservar posicao/pausa/audio/legenda e invalidar
   a tentativa/relogios obsoletos com teardown cancelavel e geracao.
2. Separar refresh de link de readmissao: sessao404/encerrada deve provocar
   resolucao autenticada nova do mesmo item, uma vez, com descriptor validado.
   Nao repetir o mesmo ID morto nem entregar magnet/embed ao decoder.
3. Conta/grant vencidos, dispositivo errado e limite de telas: erro claro,
   sem reautorizacao que ignore o servidor. Nada de recomecar em zero.
4. Esperar reconexao Wi-Fi de modo cancelavel/limitado; nao penalizar/desativar
   fonte global por suspensao local. Preservar checkpoints e cache de legenda.

Regressoes necessarias:404 versus401/403/409/429, wake repetido, rede ainda fora,
cancel, fonte removida, resposta tardia, pause preservado, hot/R2/MP4 e MKV
sem busca. Hardware: pausa+sono30s/10min, perda Wi-Fi ao acordar, episodio de
50min e nova selecao. Sem isso, nao anunciar retomada de hibernacao resolvida.

Logs locais disponiveis sao de02/10; anteriores a .53 e insuficientes para
comprovar esse incidente. Pedido de logs/titulo/versao enviado ao usuario.
