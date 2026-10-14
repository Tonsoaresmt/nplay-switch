# Teste de internet nas Configuracoes — candidato 0.12.54

## Como usar

Configuracoes > Aplicativo > Teste de internet. A tela explica o consumo e
os servicos utilizados; A ou toque em Iniciar confirma. Nada e baixado antes
da confirmacao. B, Minus, Voltar no cabecalho ou Cancelar encerram o teste.

Repita onde o video trava e depois perto do roteador/outro comodo. Pause
outros downloads, mantenha as condicoes parecidas e compare as duas medicoes.
O resultado completo anterior fica somente em memoria, visivel no mesmo
dialogo; e descartado ao fechar o app. Teste cancelado/parcial nao substitui
o comparativo completo anterior. Nenhuma configuracao de Wi-Fi e alterada.

## O que aparece

- Download em Mbps, numero de amostras aproveitaveis e aviso para incompletas.
- Resposta da internet: mediana de quatro respostas HTTPS da Cloudflare.
- Variacao: maior menos menor resposta HTTPS. Nao e perda de pacotes nem
  jitter de uma sonda UDP; inclui processamento e variacao do caminho HTTP.
- Resposta do Nplay: mediana de tres GET /api/health, sem autenticacao.
- Orientacao para repetir perto do roteador ou verificar indisponibilidade
  do teste externo/Nplay. TLS/data/local allocation errors nao viram Wi-Fi ruim.

Nao e teste de uma obra, de um link R2/TorBox ou do decodificador. Internet
boa nao garante que toda fonte de video esteja boa; lentidao de fonte, API,
player e console tambem pode acontecer. Uma amostra curta nao conclui que
todos os travamentos sao culpa do cliente. Nao informa potencia Wi-Fi/RSSI,
SSID, upload ou porcentagem de perda de pacotes.

## Transporte e limites

- Apenas HTTPS verificado, mesma cadeia CA/.51.1 existente, peer=1/hostname=2.
- Download publico documentado: https://speed.cloudflare.com/__down?bytes=...
  (zero bytes para sondas, 4MiB por amostra). Nenhuma infraestrutura/deploy novo.
- Duas amostras de4MiB + sete sondas limitadas a64KiB: corpo recebido maximo
  8.847.360B, abaixo de9MB decimais. Trafego de TLS/headers/retransmissoes e
  adicional; nao interpretar como limite de faturamento da operadora.
- Corpo e contado e descartado no callback, nunca guardado no SD ou em um
  buffer com4MiB. Protecao de tamanho mesmo sem Content-Length; overflow e
  erro, nao velocidade fabricada. CURL_MAXFILESIZE_LARGE tambem configurado.
- Sem redirect, cookies, bearer, perfil, credenciais, POST ou envio de resultados
  ao coletor Cloudflare. Os destinos naturalmente veem IP/UA da conexao: aviso
  explicito antes da confirmacao. Nao faz jobs, fontes, pedidos ou sessoes.
- Um easy handle privado da thread, reset entre sondas conserva conexoes;
  configuracao isolada sem CURLSH. HTTP1.1/identity/sem descompressao automatica.
- Deadline absoluto30s wrap-safe; sondas total2s/conexao1,5s, downloads total6s
  cada. Durante DNS/handshake, o cancel depende do callback libcurl/prazo; a
  tela continua animando ate done+join, nao libera a stack com worker vivo.
- Thread publica somente phase/progress atomicos enquanto mede; report e lido
  depois de done+join. Nao ha renderer, SDL eventos, FFmpeg ou escrita SD no
  worker. Falha de criar thread vira aviso de app, nao diagnostico de internet.
- Log esparso TEST apos terminar: caminhos fixos /diagnostics/internet,
  /diagnostics/nplay, /diagnostics/download, codigos/tamanho/tempo; sem URL/IP/
  JWT/SSID. Nao altera o trace/boot do player que pode estar sendo investigado.

## Metodo e heuristicas

Resposta HTTPS usa STARTTRANSFER_TIME menos PRETRANSFER_TIME: exclui preparo
DNS/TCP/TLS, inclui ida/volta HTTP/processamento. Nao e ping ICMP. Mediana e
range calculados so sobre respostas200 com tipo esperado e tempos validos.
JSON esperado no health; octet-stream no endpoint externo. Pagina HTML,
redirecionamento ou certificado errado nao produzem resultado de sucesso.

Download mede bytes/(TOTAL_TIME-STARTTRANSFER_TIME), em bits/s decimais.
As duas amostras sao agregadas ponderando bytes/tempo; nao escolhe so o pico.
So aceita corpo completo4MiB ou timeout com >=256KiB recebido e >=100ms de
transferencia. Timeout com dados e identificado como parcial, nao sucesso
integral. Arquivo curto/HTML/resposta de erro nao vira download valido.

As mensagens sao heuristicas conservadoras, nao homologacao de qualidade:
menos8Mbps sugere pouca folga, range>120ms ou velocidades com relacao>2x
sugere variacao. Video1080p pode exigir mais/menos conforme o encode. Nplay
com menos2respostas em3, ou resposta>1s e>3x a externa, recebe aviso separado
para nao culpar automaticamente o Wi-Fi. Falha externa com Nplay respondendo
tambem nao vira ausencia de internet. Nao medir bitrate real de cada obra.

## Organizacao visual

Aplicativo agora tem cinco linhas: Atualizacao, Diagnostico, Teste, Reiniciar,
Fechar. Ajustado espacamento apenas nessa secao, painel/status sem sobrepor
rodape. settings_layout.h compartilha desenho/hit-test; a quinta linha e
acessivel por touch/direcional. Outras secoes preservam seus espacamentos.
Dialogo com pipoca, quatro cards legiveis, comparativo e orientacao. Validacao
de geometria/limites host; pixels/fonte/GPU/touch720p fisicos ainda pendentes.

## Validacoes

- Endpoints publicos conferidos por GET sem autenticacao/TLS normal: health200
  JSON1060B, externo200 octet-stream0B/65536B. Isso NAO mede o Wi-Fi do Switch.
- tools/test_connection_test.mjs compila transporte/modelo/UI C reais, curl
  fake e pthreads reais: success/slow/variance/service503/external403/TLS,
  redirect, HTML/curto/oversize, parcial, falta de memoria, prazo/wrap, schema
  de timings, A/touch/B/header/quit, confirmacao, criacao/join e geometria.
- tools/test_connection_probe.mjs compila transporte C real com libcurl host.
  Mapeamento de URL existe APENAS no harness, para HTTPS loopback com CA local
  gerada (chaves em build ignorado). Peer/hostname continuam verificados.
  8MiB completos ~34Mbps na fixture com banda limitada; dois timeouts reais6s,
  2MiB totais ~1,4Mbps, ambos parciais; CA errada rejeitada com60. Verificados
  ausencia de bearer/cookie/profile, limites e cleanup. Nao sao cifras do ISP.
- Ambos integrados ao validate_release.ps1. Build ARM64 completo-Werror e
  incremental FINAL, bateria completa sem SkipMediaFixtures no binario final.
  Inclui as protecoes anteriores de resolucao/retomada, midia, legendas e ELF.
  Suite anterior4f0f9bc tambem passou; apos aviso explicito de parcial, suite
  FINAL passou no f441102... e assertion visual adicional passou separadamente.

Candidato LOCAL .54 (substitui o binario local anterior e937e2a...):
24282959B, SHA256
f441102d4c397b401e60c9c203830da37c43de9e47c796f89f926b24b9d348cb.
Nao houve rebuild do candidato apos essa prova; SHA sidecar conferido.
Nenhum backend/conta/deploy alterado. Publicacao exige autorizacao e processo
normal; nao anunciar atualizador disponivel sem Release publica verificada.
O crash nativo anterior e a retomada R2 apos hibernacao continuam investigacoes
separadas. Este teste nao corrige essas causas nem homologa playback no console.

## Referencias

- https://github.com/cloudflare/speedtest (API publica de download e limites do
  que o SDK mede). Orientacoes da skill Cloudflare influenciaram a escolha de
  servico documentado sem trocar a arquitetura existente ou criar infraestrutura.
- https://curl.se/libcurl/c/CURLOPT_XFERINFOFUNCTION.html (cancelamento)
- https://curl.se/libcurl/c/curl_easy_reset.html (preserva cache de conexoes)
- https://curl.se/libcurl/c/curl_easy_getinfo.html (tempos de transferencia)
