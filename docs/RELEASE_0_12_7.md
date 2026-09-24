# Nplay Switch 0.12.7

Na 0.12.6, pressionar B durante “Preparando vídeo” podia fazer o vídeo começar
em vez de voltar ao catálogo. O FFmpeg podia continuar depois que uma faixa HLS
fosse interrompida: o callback só considerava B enquanto o botão estava
pressionado. Agora o cancelamento permanece ativo até encerrar a tentativa, e
o player o verifica mesmo quando uma etapa do FFmpeg retorna sucesso. O prazo
de 30 segundos para HLS é contado desde o início da tentativa até o primeiro
quadro, sem reiniciar entre etapas.

O diagnóstico de uma reprodução real na 0.12.6 mostrou quatro pausas por
leitura, pior intervalo de 661 ms e fila máxima de áudio de 192 ms. O primeiro
quadro levou 8,6 s; 6,7 s foram gastos na abertura. Ao mesmo tempo, o player
gravava eventos normais na microSD durante `av_read_frame`, na thread que
apresenta o vídeo. Esta versão remove essas gravações do caminho normal após o
primeiro quadro. Erros e esperas de pelo menos um segundo continuam no trace;
ao sair, um resumo informa requisições de segmentos, atrasos até o primeiro
byte, novas conexões e falhas, sem URLs ou tokens. Outro resumo mostra se o
próximo segmento já tinha bytes prontos quando o FFmpeg começou a lê-lo. Isso
ajuda a distinguir pré-abertura tardia de download lento.

Validação local: `tools/validate_release.ps1` e `git diff --check`. A redução
de pausas e o comportamento de B ainda precisam ser confirmados no Switch:
reproduza por dez minutos o mesmo título e compare Configurações > X com a
captura anterior; depois tente cancelar um episódio de X-Men '97 ainda na
tela de preparação.
