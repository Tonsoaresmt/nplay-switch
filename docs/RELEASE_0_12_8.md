# Nplay Switch 0.12.8

O rastreio real do X-Men '97 T1E1 na 0.12.7 confirmou entrega R2/HLS e
playlists HTTP 200. Uma tentativa abriu o manifesto em 1,56 segundo; as duas
tentativas que permaneceram nos arquivos foram canceladas com B antes do
primeiro quadro. A captura da espera longa nao sobreviveu a novas tentativas,
entao estes dados nao provam que o episodio agora reproduz.

O mesmo rastreio mostrou 76 eventos HLS escritos individualmente no cartao SD
antes do primeiro quadro. A abertura normal deixa de registrar cada recurso.
Erros continuam no arquivo, e uma linha de progresso a cada cinco segundos
mostra a fase, o tempo e a memoria reservada enquanto o player espera. A
consulta do tamanho de um segmento deixa de aguardar ate 15 segundos quando
o servidor ainda nao informou `Content-Length`: nesse caso retorna
imediatamente `ENOSYS`, a mesma resposta que antes vinha apos a espera.

Em Configuracoes > X, Cima e Baixo percorrem paginas do rastreio no proprio
Switch; X atualiza a pagina, B fecha. O arquivo original permanece em
`sdmc:/switch/.nplay-player-trace.log`.

Validacao local: compilacao ARM64, contrato do NRO e `git diff --check`.
O ambiente de desenvolvimento nao possui console ou emulador Switch configurado;
nao registrar estas verificacoes como teste de reproducao no hardware.
