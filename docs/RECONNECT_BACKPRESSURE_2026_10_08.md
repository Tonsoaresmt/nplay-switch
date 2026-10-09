# Reconexoes: backpressure entre callbacks (08/10/2026)

## Escopo e base

Analise comparou o main local 2aa74a0 (0.12.51.1, HTTPS corrigido por outra
rodada) com o checkout autorizado switch-access-expiry/2d3bb1e. Os arquivos
do transporte/player auditados eram iguais entre as duas bases. Nenhuma
alteracao foi feita no main, no backend, nas contas ou nos certificados.
O candidato .52 preexistente estava sem a correcao HTTPS. Nesta rodada foram
portados source/net.c e tools/test_tls_ca.c de 2aa74a0 e o guard de artefato
de e9671e7 para o checkout autorizado, preservando todas as alteracoes locais.
Main continua inalterado. Nenhuma nova release foi publicada.

## Defeito demonstrado

`xfer_cb` encerrava uma transferencia depois de 8 s sem bytes escritos.
`wr_ring` ja descontava a espera dentro do callback de escrita, mas faltava
proteger o intervalo ENTRE callbacks: o callback anterior pode encher o ring
e retornar normalmente. Enquanto o consumidor esta pausado ou atrasado,
o callback de progresso ainda pode executar e confundir buffer cheio com
rede parada. Isso derruba uma conexao que nao precisava ser renovada.

Regressao com a funcao C extraida: ring cheio, corpo recebido, 60 s locais
sem consumo. Baseline falhou na assertion de transferencia ainda ativa.
Depois da correcao, passou; ao liberar o ring, 8 s de idle real continuam
abortando. Cancelamento/seek/prazo de abertura precedem a protecao.

Correcao pequena: ler a ocupacao sob o mutex existente e rearmar o relogio
de idle enquanto o ring estiver cheio. Sem nova thread, memoria, aumento
de limites ou acesso concorrente ao FFmpeg. TLS e checkpoints inalterados.

## Validacao executada

- `tools/test_curl_avio_wait.mjs`: callbacks C reais, tempo/SDL simulados;
  regressao anterior falhou e corrigida passou, incluindo ring entre callbacks.
- `tools/test_curl_backpressure_probe.mjs`: libcurl HOST real + servidor HTTP
  localhost. Espera local entre callbacks de 9,5 s termina curl=0/1024 bytes;
  idle verdadeiro termina curl=42; politica low-speed antiga curl=28;
  backpressure dentro do callback com politica atual curl=0.
  Callback de progresso C extraido e real; ocupacao/consumidor sao simulados
  neste probe de uma thread, nao um pipeline SDL/FFmpeg integrado.
- `tools/test_demux_worker.mjs`: pthread real, filas/seek/cancel/ownership passam.
- `tools/test_player_supervisor.mjs`: checkpoint, pausa/seek/renew/cancel passam.
- `tools/test_player_flow_guards.mjs`: EOF, Historico e pausa passam.
- Build ARM64 incremental com -Werror passou e recompilou curl_avio.c.
- Bateria completa ANTES da integracao TLS passou. Candidato integrado
  recompilado ARM64 -Werror; segunda bateria completa passou, sem pular
  fixtures de midia, incluindo guard de versao/antiguidade e simbolos ELF.
  Nao confundir testes host com
  handshake Horizon, apresentacao NVTEGRA ou sessao longa no aparelho.

Artefato integrado LOCAL .52: 24266575 bytes; SHA-256
`d908325c24d3dcb74a8fcb1e4bb5a3ab39282bf62e069a34086337f391e8a749`.
Sidecar atualizado apos validacao. Nenhum commit/push/release nesta rodada.

## Outros riscos observados, ainda sem alteracao

Comparacao A/B adicional com libcurl real:
`node tools/test_curl_backpressure_probe.mjs --baseline` extrai o transporte
de 2d3bb1e (transporte identico a 2aa74a0); ring cheio + gap9.5s abortou
curl=42 depois de128B. Usa commit ja publico/ancestral, nao SHA somente local.
O modo --baseline exige REPRODUZIR o defeito e nao e criterio de release.
Modo normal usa a correcao atual; os testes de release nunca usam --baseline.

- O supervisor escala HLS apos 12 s; o transporte tenta novamente apos 8 s.
  Uma segunda conexao lenta pode ser interrompida pelo supervisor. Aumentar
  esses prazos cegamente tambem prolonga uma falha real: medir primeiro.
- `producer_stream` limpa fail_since quando uma transferencia entrega bytes,
  mesmo se terminar incompleta. Isso pode renovar a janela sob respostas
  parciais repetidas. Nao atribuir o incidente do usuario a isso sem trace.
- "Tentando reconectar" aparece pelo tempo de buffering (8 s), antes de
  necessariamente haver reabertura de sessao: o texto nao prova queda Wi-Fi.
- TLS .51.1 importa em estagios. Custo de handshake no Horizon nao medido;
  preservar a correcao e medir, nao trocar o backend ou remover verificacao.

Logs disponiveis sao de 02/10/2026, anteriores ao relato atual. Nao provaram
a causa desta sessao. Precisamos de trace recente para distinguir HTTP
401/403 (acesso/assinatura), 5xx, timeout, EOF parcial e backpressure local.

## Antes de publicar

Antes de publicar, reconferir candidato/NRO/ELF/versao/hash e testar no console:
pausa de 1/10 min, menus, retomada/seek, Wi-Fi intermitente, video >60 min.
Este documento nao anuncia nova release nem fim de todas as reconexoes.

Referencias oficiais: https://curl.se/libcurl/c/CURLOPT_XFERINFOFUNCTION.html
(callback aborta a transferencia ao retornar 1) e
https://curl.se/libcurl/c/CURLOPT_FRESH_CONNECT.html (forcar conexao pode
aumentar custo; nao usar em toda requisicao normal).
