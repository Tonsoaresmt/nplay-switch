# Auditoria adicional — Continuar, player, legendas e abas

## Escopo e estado

Pedido desta rodada: procurar outras falhas, nao implementar/publicar uma nova
versao. Base: `ef5f6e5`, checkout `switch-access-expiry`, branch
`codex/switch-player-hardening`. O codigo do aplicativo e o NRO nao foram
alterados. Foram acrescentados este relato, continuidade e simulacoes locais.

Candidato permanece 0.12.50; ultima release registrada nesta continuidade e
v0.12.49. Nao foi consultada novamente a release remota nesta rodada. Nenhuma
conta, banco de producao, fonte privada, deploy ou pacote R2 foi modificado.

## Achados prioritarios

### 1. P1 — EOF antecipado pode concluir a obra e disparar o proximo episodio

`player_play_internal`, bloco `ret < 0`, considera `AVERROR_EOF` fim natural
depois que a fila de audio baixa. Fora do preroll nao confronta esse EOF com a
duracao conhecida nem com falhas dos recursos HLS. `player_run` aceita rc=1
se algum quadro foi exibido. `finalize_natural_playback` marca como visto se a
posicao supera cinco segundos, mesmo muito longe do final conhecido.

Simulacao com os dois trechos C reais: obra de 1800 s, parada em 300 s, EOF;
o terminal retorna fim natural e o finalizador chama marcar-visto. Controles
negativos: erro explicito de rede e saida do usuario nao seguem esse caminho.

Isso prova a politica errada **se houver EOF antecipado**. Nao prova que foi a
causa do incidente do usuario ou que uma URL real expirou. O
[demuxer HLS do FFmpeg 7.1](https://ffmpeg.org/doxygen/7.1/hls_8c_source.html)
incrementa a sequencia ao desistir de segmentos e pode depois atingir EOF.
O loader de legendas tem guard para esse caso; o caminho de video nao tem
guard equivalente. `player_hls_io_open` registra falhas, mas nao as propaga
como evidencia de fim incompleto na decisao final.

Correcao recomendada: separar EOF de conclusao segura, drenar os decoders,
usar evidencia de transporte/manifesto e uma tolerancia conservadora ao fim
de VOD com duracao conhecida. Nao aplicar a mesma regra cegamente a live ou
duracao desconhecida; nao converter toda falha recuperada em erro permanente.
Fim incompleto deve preservar o ponto e renovar, com tentativas limitadas,
sem marcar visto nem fazer autoplay. Testar segmentos finais e intermediarios
404/503, EOF limpo prematuro, duracao imprecisa, fim normal e fonte live.

### 2. P1 — Historico acompanha indice, nao identidade, durante atualizacao

`pump_history` substitui o JSON e apenas limita `g_history_sel` ao tamanho novo.
O menu aberto em `input_downloads` executa a acao sobre o item nesse indice.
Nao existe snapshot da obra para as acoes Continuar/Recomecar/Concluir/Remover.

Reproducao C: lista `[10,20]`, menu da obra 20 aberto no indice 1; resposta
tardia `[30,10,20]`; indice 1 passa a apontar para 10. A obra alvo muda sem
uma nova selecao do usuario. O titulo do modal tambem muda ao redesenhar.

Correcao: restaurar foco por `item_id`, capturar o ID alvo ao abrir o menu e
revalidar esse ID ao confirmar. Se desaparecer, fechar o menu com aviso, sem
acionar o vizinho. Testar insercao/reordenacao/remocao enquanto menu esta aberto.

### 3. P1 — Pausa nao sobrevive a reabertura interna

Inspecao: cada `player_play_internal` inicia `paused=0`. `PlayerRequest`,
`PlayerResult` e o supervisor nao transportam a intencao de permanecer pausado.
Seek ou troca de audio que usam `PLAYER_RESTART_*` acabam em outra tentativa
com pausa zerada, inclusive quando a operacao foi iniciada pausada.

Esta e uma lacuna de fluxo demonstravel no codigo, **nao um teste fisico nem
uma simulacao completa da pipeline**. O teste existente chamado "pause
recovery" do supervisor valida recuperacao/posicao; nao exercita a flag local
de pausa do decoder real.

Correcao: transportar intencao PLAYING/PAUSED dentro da mesma reproducao.
Permitir preroll ate o primeiro quadro do novo ponto, mas nao liberar audio
nem avancar apos esse quadro quando a intencao e pausa. Nova obra/episodio nao
deve herdar pausa automaticamente. Testar seek/troca pausada, cancelamento,
falha de abertura e recuperacao de rede pausada.

### 4. P2 — Touch nao cobre os modais de timeline e Episodios

Inspecao de evento/desenho: so o modal de faixas converte um toque em uma acao
contextual. A timeline tem `if (... || timeline_seek) continue`; seus toques
nao confirmam/cancelam/movem o alvo. O painel Episodios e desenhado e funciona
com Joy-Con, mas nao tem hit-test de linhas/scroll/fechar. O toque pode cair no
HUD que esta por tras; o canto superior esquerdo encerra o player, nao fecha
apenas o modal. Isso nao significa que todo o touch ou o botao B esteja quebrado.

Correcao: ordem modal antes do HUD; geometria compartilhada com o desenho;
hit-tests e gestos de cada painel, incluindo voltar contextual. Nao executar
seek ao iniciar arrasto nem confirmar um episodio escondido. Testar tap,
arrasto, fora do painel, toque de voltar e controles mistos em 1280x720.

### 5. P2 — Cancelamento de legenda ainda pode bloquear a thread de desenho

`subtitle_fetch_stop` cancela e faz join imediato. `subtitle_fetch_start` chama
esse stop ao substituir uma solicitacao. Desligar/trocar a faixa ou limpar um
store progressivo tambem pode aguardar um worker em andamento.

A [documentacao SDL](https://wiki.libsdl.org/SDL2/SDL_WaitThread) confirma que
join aguarda a thread. O [callback de progresso libcurl](https://curl.se/libcurl/c/CURLOPT_XFERINFOFUNCTION.html)
pode ser chamado menos frequentemente em periodos sem transferencia; a flag
de cancelamento so sera observada quando o worker/transportes voltarem a
consulta-la. Isso nao e um deadlock comprovado nem vazamento comprovado.

Fixture pthread: atrasamos intencionalmente a observacao do cancel por 500 ms;
o stop real bloqueou o chamador cerca de 490 ms. **O atraso e injetado; nao e
uma medida do Wi-Fi do Switch.** A faixa antiga e o cancel independente do
video permaneceram corretos no mesmo teste.

Correcao: pedido mais recente pendente com memoria propria; cancelar worker
anterior, continuar renderizando, reunir quando done e so entao iniciar/aplicar
o novo. Fila de jobs estritamente limitada, sem detach de state emprestado,
sem reaproveitar demux_abort. Testar cancel lento, troca rapida, OFF, saida,
episodio novo, falha de criacao de thread e resultado atrasado.

### 6. P2 — Retorno a uma aba perde selecao e scroll

`load_landing` aplica novamente ate um catalogo ja em cache. `landing_apply`
zera rail/item/scroll/hero. Reproduzido com C real: foco rail 3, card 7, scroll
800, hero 2; retorno ao cache leva ao hero/primeiro item/scroll zero. O retorno
dos detalhes a Busca preserva a busca; **nao e correto dizer que todos os
retornos voltam ao Inicio**. Aqui o defeito e no estado por aba e na reconstrução
da landing apos o player liberar memoria.

Correcao: snapshots por aba usando IDs e scroll, sem ponteiros para JSON
descartado. Restaurar/clamp apos rebuild; aba nova pode ter estado inicial
normal. Testar mudanca de abas, play/voltar, catalogo reordenado e item removido.

### 7. P2 — Payload invalido pode apagar o Historico conhecido

`history_fetch_thread` aceita qualquer JSON parseavel HTTP 200; `pump_history`
substitui o estado sem validar `items` como array. Reproduzido: resposta
`{"error":"unexpected payload"}` elimina a lista valida e mostra vazio.

Isso e um teste de robustez; nao ha evidencia de que producao enviou tal
payload. Validar schema antes de aplicar; distinguir vazio legitimo de erro,
preservar o ultimo estado e oferecer nova tentativa.

### 8. P2 — Assistir mais tarde nao espelha exclusao feita em outro aparelho

O pump apenas adiciona os itens recebidos a lista local. Reproduzido: local
`[10,20]`, remoto `[10]`; depois do pump, 20 permanece. Sem reconciliacao/tombstone,
a lista pode divergir indefinidamente.

Correcao: separar entradas locais pendentes das confirmadas e reconciliar
snapshot remoto autoritativo; nao simplesmente apagar todas as entradas locais
durante falha de rede. Testar exclusao remota, adicao pendente, falha HTTP e
resposta atrasada a uma mutacao.

### 9. P3 — Selecionar uma aba em cache deixa consulta antiga enfileirada

`load_landing` retorna cedo no cache sem atualizar `g_land_queued_tab`.
Reproduzido: consulta em andamento, aba 2 pendente, ultimo destino aba 0 em
cache; o pendente permanece 2. `pump_landing` pode iniciar essa consulta
desnecessaria depois. Nao redireciona a UI para 2, mas gasta rede/memoria.

Correcao: invalidar o intent pendente quando a ultima selecao ja foi satisfeita
pelo cache; nao matar inseguramente a thread ativa. Testar A lento -> B -> A/C
em cache, cancelamentos e finalizacao tardia.

## Legendas: o que nao deve ser confundido com regressao nova

- Regressões de escolha, atraso de resposta, OFF, faixa anterior conservada,
  guards de arquivo incompleto, texto UTF-8, fala vs karaoke e dedup de letreiros
  longos do candidato passaram novamente nesta rodada.
- Ainda nao se distingue extracao progressiva realmente completa de HTTP 200
  limpo encerrado antes da hora sem marcador do backend. Silencio normal entre
  falas nao e motivo para recarregar. As causas de qualquer incidente concreto
  exigem o trace do episodio e o mesmo pacote/fonte.
- A fila nativa continua 32 slots; o limite de 16 faixas, bitmap PGS/DVD e layout
  ASS/WebVTT completo nao foram removidos. Nao prometer paridade total com browser.
- Fila SDL de audio continua monitorada, nao possui teto duro; isso e pendencia
  conhecida, nao uma nova causa confirmada de lag.

## Execucao e evidencias

`HOST_CC=C:\devkitPro\msys2\usr\bin\gcc.exe`:

```
node tools/audit_player_flows.mjs
node tools/test_episode_rewatch.mjs
node tools/test_completed_resume.mjs
node tools/test_subtitle_fetch.mjs
node tools/test_subtitle_usage.mjs
node tools/test_player_supervisor.mjs
node tools/test_seek_barrier.mjs
node tools/test_subtitle_completion.mjs
node tools/test_demux_worker.mjs
```

Todos terminaram com exit 0 nesta rodada. **No comando audit_player_flows, exit
0 significa defeitos reproduzidos; NAO significa player aprovado.** Se uma
correcao mudar o comportamento, o audit deve falhar e ser substituido por
regressao positiva; nao adiciona-lo ao validador de release como teste de saude.

Foram seis cenarios C de Historico/catalogo/EOF mais um experimento de latencia
com pthread, e duas verificacoes de contrato por inspecao (pausa e touch).
O cJSON, funcoes do controle e stores sao reais; rede/decoder/renderizador sao
simulados. Demux/barreira usam pthread real, com leitura simulada. O teste de
retomada de episodios passou oito cenarios; completion de legenda passou 19.
O salto T3 -> T5 relatado pelo usuario ainda nao foi reproduzido nesses cenarios.

Nao houve novo build ARM64 nem nova execucao do validador completo nesta rodada:
o app nao mudou. O build/validador completo da rodada anterior esta registrado
em `SUBTITLE_FIXES_0_12_50.md`, e o hash do NRO foi reconferido sem mudanca:
`909f166fd66f1068b46292d45f6b804d877a7c529db3b134f5c6453724474bbb`.
Switch fisico, Linux integrado, playback real de 50+ minutos e expiracao real
de assinatura nao foram executados. Nao afirmar que todos os crashes foram
explicados, nem atribui-los a certificados sem evidencia.

## Continuidade / ordem de implementacao

1. Proteger identidade de Historico e EOF/conclusao/autoplay com regressoes
   positivas das funcoes reais, inclusive duracao desconhecida e erro recuperado.
2. Preservar pausa entre tentativas e terminar preroll sem liberar audio pausado.
3. Completar touch dos modais, isolar o HUD de baixo, restaurar contexto por aba.
4. Tornar cancelamento/replacement de legenda nao bloqueante, com ownership,
   geracao, limites e join obrigatorio antes de liberar memoria.
5. Reconciliar watchlater e validar schemas; eliminar intent de catalogo obsoleto.
6. Build completo/validador/media fixtures no binario final; depois teste fisico
   pausado/seek/troca/faixas/episodios/rede lenta/reproducao longa. Publicar apenas
   identificando honestamente o que foi validado localmente e o que exige console.

Preservar as correcoes de ef5f6e5 e nao editar a checkout principal antiga.
Nao mudar backend, reprocessar R2, mover branches remotas ou publicar automaticamente
como parte desta auditoria. Estes achados ainda precisam ser implementados.
