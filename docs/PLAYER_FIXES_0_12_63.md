# Player 0.12.63 — candidato local

Data: 24/11/2026. Base: `989a71d`, runtime publicado da .62 em `563245f`.
Checkout: `.codex-tmp/switch-access-expiry`, branch `codex/switch-player-hardening`.

Inicialmente o usuario autorizou somente NRO; depois autorizou corrigir o
encerramento no servidor e o limite no re-login, e publicar a atualizacao.
Backend corrigido/validado enviado ao main em `1c60ef4f`. Nao houve edicao de
contas/DB/pacotes R2. A .63 ainda aguarda confirmacao do deploy antes da release.

## Correcoes implementadas

1. **Quadros futuros/VFR.** O limite antigo de espera de 350 ms podia apresentar
   um quadro varios segundos antes de seu PTS. Agora uma unica referencia de
   `AVFrame` fica retida ate o prazo. O loop continua tratando controles, audio
   e legendas nativas. Nao decodifica um segundo quadro futuro nem altera os
   timestamps do decoder. Seek/troca de geracao libera a referencia; OOM falha
   controladamente. O receive do decoder continua antes de enviar outro pacote.
   Nos ultimos 50 ms antes do prazo, evita redesenho redundante do quadro antigo
   que poderia consumir outro vblank. O teste inclui custo simulado de 16 ms
   por apresentacao, sem alegar medir a GPU real.
2. **Audio depois da ultima imagem.** EOF do produtor nao autoriza despejar toda
   a cauda no SDL. Drena primeiro os quadros atrasados e aplica contrapressao ao
   PCM. Enquanto o audio toca, a posicao acompanha o que efetivamente saiu da
   fila, nao seu fim ainda nao ouvido. EOF prematuro continua sendo erro.
   Limites de PCM e demux nao foram aumentados.
3. **Legenda automatica versus manual.** `SubtitleChoice` conserva essa origem
   na mesma reproducao e nos reopens. Se uma PT forced chega antes da PT
   completa, a descoberta posterior pode promover a completa. Escolha manual,
   inclusive OFF ou forced, nao e substituida. A faixa em uso permanece ate a
   substituta estar pronta. O retry nao e consumido enquanto outro carregador
   progressivo ainda ocupa o slot.
4. **Audio HLS com audiodescricao.** Preserva o token exato
   `public.accessibility.describes-video` do master ate a politica de idioma.
   Permite preferir PT de dialogo a PT de audiodescricao quando os nomes sao
   tecnicos. Nao transforma um idioma estrangeiro em portugues nem ignora
   selecao manual. Tokens apenas parecidos nao sao classificados como AD.
5. **Retorno ao login na mesma execucao.** Ao receber uma recusa definitiva,
   cancela os trabalhos da identidade antiga e aguarda `done`/join sem bloquear
   o desenho. Token e perfil em memoria permanecem imutaveis ate todos os donos
   terminarem; respostas antigas nao sao aplicadas. So entao limpa caches,
   intencoes, preferencias carregadas e libera outro login. A troca normal de
   perfil continua com o restart ja existente. Arquivos pessoais sao preservados.
6. **Orcamento de respostas HTTP.** Requests gerais antes sem limite recebem
   teto por rota: catalogo 16 MiB, progresso/jobs 8 MiB, demais 4 MiB. Limites
   explicitos menores, como descriptor 64 KiB, prevalecem. O writer conta os
   bytes reais, independentemente de Content-Length, e rejeita excesso sem
   entregar uma resposta como sucesso. Isto nao e um teto do heap total: cJSON,
   imagens, decoder e texturas continuam exigindo seus proprios orcamentos.
7. **Endereco de reproducao integral.** Resolver normal, refresh e hot rejeitam
   overflow/CRLF, em vez de aceitar uma URL assinada truncada. URLs relativas
   ainda sao absolutizadas; descriptors de preparacao podem nao ter URL.
   O buffer nao foi ampliado: endereco acima de 1535 bytes falha explicitamente.

## Evidencia local e limites

- `tools/test_media_deadlines.mjs`: fixtures FFmpeg de video 2 s/audio 8 s e
  VFR com imagens em 0/4/8 s, dentro dos 512 pacotes/4 MiB existentes. Executa
  scheduler, hold e PCM reais extraidos do player. Modelo de decoder/dispositivo
  e clock, nao hardware: 48 imagens/47 holds e 3 imagens/2 holds; max PCM
  196032 bytes. Inclui audio final audivel, EOF truncado, referencia liberada,
  legenda nativa durante hold, falta de audio/rede e OOM.
- `tools/test_identity_barrier.mjs`: 100 ciclos, 1000 joins de pthread reais,
  inclusive resposta que demora a obedecer cancelamento. HTTP, SD e render
  simulados. Token/perfil nao mudam antes do ultimo join; caches nao atravessam.
- `tools/test_system_contracts.mjs`: parser/mapeamento HLS reais, forced/full
  tardio, manual forced/OFF, writer real nos limites 4/8/16 MiB, overflow de
  multiplicacao e resolver real nos limites de URL. HTTP simulado.
- Regressao existente preserva 400 seeks/40000 blocos PCM, 6000 publicacoes
  tardias de legenda e 2200 ciclos de login. Os testes novos entram no validador.
- Os audits `--reproduce-062`/`--reproduce-061` sao negativos: exit zero significa
  defeito reproduzido na base, nunca aprovacao do candidato.

CLI FFmpeg 8.1.1 nao equivale ao FFmpeg 7.1/NVTEGRA do NRO. Nao foram executados
Switch fisico, GPU/SSL Horizon, sono real, Wi-Fi domestico, sessao de 60+ minutos
ou os episodios de producao relatados pelo usuario. Nao atribuir os incidentes
reais a estas sete causas sem trace correspondente.

## Encerramento exato e QR — complemento autorizado

O novo NRO usa somente `/api/stream/session/:sid/stop`, inclusive na limpeza
antes de abrir o player e falha de progresso. Nao ha fallback amplo por item.
Admissoes enviam `new_session:true`: sem um ID novo, o reuso da mesma sessao ao
reabrir a mesma obra anularia a protecao. Refresh conserva o SID atual.

Backend `1c60ef4f` acrescenta a rota idempotente com ownership de conta/aparelho
e o opt-in de nova admissao. HTTP cancelado/handler atrasado e replay real na
fixture R2 preservaram a nova reproducao e outra tela. Clientes legados mantem
seu contrato; fencing de progresso atrasado sem SID nao entrou nesta rodada.

Outra lacuna: o celular deslogado podia ocupar uma vaga antes de autorizar o
Switch do QR. Agora usuario/senha aprovam SOMENTE o aparelho do QR, reutilizando
o Switch antes da quota, sem cadastrar/logar o celular, emitir JWT para ele ou
apagar aparelhos. Novo Switch continua sujeito ao limite. Contexto do QR fica
preservado mesmo em login?next=pair apos expiracao do token do celular.

Suite completa do backend e check passaram; push main confirmado, mas deploy
ainda adiado por espectadores na ultima consulta. Implantar servidor antes de
promover a release NRO. Relatorio completo no repo Nplay:
`docs/SWITCH_SESSION_AND_QR_2026_11_24.md`.

Rejeitar um descriptor invalido nao comprova que o servidor desfez uma admissao
ja criada. Resposta perdida/invalida pode continuar dependendo da expiracao da
sessao no servidor. Esta rodada nao acrescenta stops amplos para compensar esse
caso: isso ampliaria justamente a corrida de identidade ainda aberta.

## Compilacao, validacao e artefato

Concluidos com exit zero: build ARM64 integral `make -B -j4` com `-Werror` e
suite completa `tools/validate_release.ps1 -SkipBuild`, **sem**
`-SkipMediaFixtures`. SHA antes/depois identico; nenhum codigo do app nem NRO
alterado apos essa prova. `git diff --check` passou.

- NRO final: **24319823 bytes**, versao **0.12.63**.
- SHA-256: `4f287b3a1df2b22900ba2eb79a719e5be943039c710ac13a02f9bc1f20271002`.
- Suite: 55 cenarios de midia/6 perfis/118 requests HTTP locais, 1087 blocos
  AAC/resampled reais e todos os testes novos/regressoes do validador passaram.
- ELF: simbolos obrigatorios de TLS, HLS, WebVTT e NVTEGRA conferidos.
- Sidecar local atualizado somente apos a prova. NRO/sidecar nao commitados.
- Publicacao NRO autorizada em preparacao; ainda nao afirmar disponibilidade.

Uma rodada completa preliminar passou; houve ajuste de cadencia depois dela.
A evidencia FINAL desta revisao e `build/build-0.12.63-session-final2.log` e
`build/validate-0.12.63-session-final4.log`, exit0/SHA antes-depois identico.
Contrato conferido contra backend corrigido. Testes auxiliares antigos foram
atualizados para o wrapper controlPost e nova admissao; negativo historico fica
preso ao snapshot antigo, nunca origin/main atual. Logs `final` anteriores nao
sao prova do ultimo codigo. Nenhum rebuild apos a prova final.

Nao publicar ou reutilizar este SHA se o NRO for reconstruido/modificado.

## Roteiro fisico antes de homologar

1. Entrar por QR/senha, provocar sessao expirada e autenticar outra conta;
   conferir preferencias, historico, listas e navegacao sem dados anteriores.
2. Mesmo episodio com PT forced e completa: selecao automatica, OFF manual,
   troca, seek, reconexao; verificar que fala/letreiro continuam legiveis.
3. R2 e hot/TorBox: iniciar, pausar, retomar, seeks curtos e longos, HOME/sono,
   troca de episodio e sessao longa, preservando posicao e idioma.
4. Conferir fluidez 720p/1080p, legendas nativas e externas e memoria ao fim
   de episodios; nenhum crash pode ser considerado descartado apenas pelo build.

## Referencias oficiais

- [SDL_QueueAudio](https://wiki.libsdl.org/SDL2/SDL_QueueAudio)
- [SDL_GetQueuedAudioSize](https://wiki.libsdl.org/SDL2/SDL_GetQueuedAudioSize)
- [Contrato send/receive do FFmpeg 7.1](https://ffmpeg.org/doxygen/7.1/group__lavc__decoding.html)
- [RFC 8216: HLS](https://www.rfc-editor.org/rfc/rfc8216)
