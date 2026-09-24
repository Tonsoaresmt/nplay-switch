# Nplay Switch 0.12.4

O diagnóstico da 0.12.3 registrou 17 pausas e 24 leituras HLS lentas, com
máximo de 5295 ms, embora só três quadros tenham sido descartados pelo decoder.
Esta versão mantém somente as playlists de vídeo, áudio e legenda selecionadas
na leitura HLS e inicia a transferência do próximo segmento R2 antes de
terminar o atual. Essas mudanças visam reduzir os intervalos de rede; a
fluidez precisa ser comparada no Switch real.

O banner da Home não encobre mais a logo e as abas ao voltar das fileiras.
Títulos relacionados agora têm capas e nomes maiores. O catálogo começa a ser
recarregado assim que o player fecha, antes de voltar à Home. O carregamento do
catálogo e a preparação do vídeo usam a pipoquinha do Nplay; a abertura de
metadados HLS tem prazo definido e o botão B continua cancelando a preparação.

Para validar, confirme 0.12.4 em Configurações e reproduza por dez minutos o
mesmo filme e episódio R2 testados antes. Em Configurações > X, compare pausas,
leituras lentas, maior leitura, descartes e recursos HLS com a 0.12.3. Se
persistirem travamentos, envie apenas os eventos `demux read-wait`, `avio http`
e `avio close` de `switch/.nplay-player-trace.log`, sem URLs ou tokens. A
compilação local verifica o pacote, mas não comprova a fluidez no hardware.
