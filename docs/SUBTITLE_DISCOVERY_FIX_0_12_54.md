# Descoberta de legendas — candidato local 0.12.54

Data: 14/10/2026. Base: 0deb304, checkout switch-access-expiry.

## Problema corrigido no cliente

A 0.12.53 consultava `/hot/:sid/probe` somente quando nao havia nenhuma
legenda nativa. Uma faixa EN impedia consultar PT/EN/ES externos. Alem disso,
o master HLS so substituia as faixas nativas quando tinha mais entradas:
uma rendition PT e uma faixa nativa EN eram tratadas como cobertura igual.

O catalogo agora conserva a origem de cada entrada: stream nativa, HLS externo
ou WebVTT progressivo hot. O master e a sonda entram antes das streams nativas
na abertura. Idiomas iguais nao bastam para deduplicar; URLs distintas,
queries de faixa e streams nativas continuam distintas. O indice de uma
entrada existente nao muda quando a consulta posterior acrescenta faixas.
Uma duplicata aparente pode representar a mesma lingua em transportes
diferentes; o painel conserva nomes e numeracao, em vez de eliminar texto
por uma equivalencia que nao foi comprovada.

Selecao, decoder, busca, render e cache usam a origem da faixa selecionada.
Na troca para uma faixa externa, a anterior continua aplicada ate o novo
download ser aceito. OFF e a escolha de outra origem cancelam a requisicao
pendente. Uma resposta atrasada nao pode trocar a selecao posterior.

## Descoberta posterior e limites

A sonda inicial continua limitada a 5 segundos e cancelavel na tela de
preparo; agora e consultada mesmo com legendas nativas. Essa etapa pode
acrescentar latencia em fontes hot que antes a pulavam. Nao e uma correcao
de tempo de primeiro quadro.

Quando a sonda retorna vazio, 502/503/504, timeout ou falha transitoria de
conexao/DNS, ha ate tres novas consultas em segundo plano, com esperas de
2/5/10 segundos. 401/403/404/410 e falhas TLS nao entram nessa repeticao.
X/touch permite pedir outra consulta mesmo com faixas ja presentes. A UI
continua processando o video; o resultado so e aplicado depois de done/join.
A tarefa possui SID copiado e cancelamento proprio, e e encerrada antes de
liberar a tentativa do player. Nenhum `demux_abort` e usado para legenda.

Faixas descobertas depois recebem aviso para selecionar com X. A consulta
nao liga uma legenda sozinha nem sobrescreve OFF/manual. A selecao automatica
na abertura continua seguindo as preferencias existentes. O carregamento
da faixa hot selecionada ainda usa a espera modal preexistente; esta rodada
nao elimina essa pausa nem o join de encerramento de extracao progressiva.

O painel continua limitado a 16 faixas. Uma lista ja cheia nao sofre reorder
ou substituicao silenciosa para encaixar um resultado tardio. Nao prometer
inventario ilimitado. TLS, limite de resposta de sonda 256 KiB, limite de VTT,
identidade de cache e guardas contra texto incompleto permanecem intactos.

## Verificacao e alcance

`tools/test_subtitle_discovery.mjs` executa o bloco C real de descoberta com
parsers reais e FFmpeg/HTTP simulados. Cobre EN nativo + PT/EN/ES externos,
master PT com a mesma contagem de EN nativo, nome nativo ausente, falha 503
preservando a faixa nativa, politica de repeticao, indices estaveis, query
semantica, limite de 16, SID invalido e falha de criacao de thread.

Executa tambem os helpers reais de worker com pthread: 100 ciclos alternando
conclusao e cancelamento, incluindo sucesso tardio apesar de cancelamento,
join unico e ausencia de reuso de memoria antes do encerramento. A resposta
e a temporizacao de rede sao simuladas; isto nao exercita o scheduler Horizon.
O teste e obrigatorio no validate_release. O audit historico exige agora
`--baseline-053`; seu exit 0 continua significando defeito reproduzido.

Testes existentes de IO e fetch de legendas passaram nesta rodada. A
compilacao ARM64 completa final com -Werror passou. A bateria completa final
`validate_release.ps1 -SkipBuild`, SEM SkipMediaFixtures, passou no mesmo
artefato, incluindo midia real de fixture e simbolo `subtitle_catalog_add`.
Nenhum rebuild ocorreu depois dessa verificacao.

Artefato final local: `Nplay.nro`, 24.287.055 bytes, SHA-256:
`7e3a3e60460ca39389bc57dc197ed17eed8e84408a56ef1dd167215de09804fc`.
O sidecar foi atualizado. O binario anterior `a3fa8b5...` e o intermediario
`2b8160...` foram substituidos pelo artefato acima.

Nao houve reproducao de Dragon Ball Super: Beerus em producao ou no Switch
fisico. Faltam numero/ID do episodio e traces dessa tentativa para atribuir
aquele incidente a uma fonte especifica. Nenhum backend, pacote R2, conta ou
preferencia remota foi alterado. Publicacao segue suspensa por instrucao do
usuario; a release publica continua .53.
