# Nplay Switch 0.12.6

A 0.12.5 reduziu os travamentos, mas houve episódios presos em “Preparando
vídeo”, sem resposta ao botão B. Em outro título, o vídeo começou após demora e
teve oito intervalos de pelo menos 250 ms entre quadros em cerca de 75 segundos;
seis leituras do demuxer demoraram pelo menos 250 ms. Os dados ainda não dizem
quanto de cada pausa foi rede, sincronização ou processamento.

Esta versão consulta B/Minus durante leituras HLS bloqueadas e durante as
transferências síncronas de playlists. O callback só consulta o controle quando
há espera pela rede, evitando custo em cada leitura de dados já disponíveis.
Conexões libcurl ociosas agora também podem ser reutilizadas nas playlists
sequenciais, reduzindo novas conexões TLS quando o servidor permite.

O diagnóstico mostra tempo até o primeiro quadro, duração da abertura e da
leitura das faixas, além de classificar as pausas entre quadros como leitura,
sincronia ou outro trabalho. A classificação é uma aproximação da thread de
reprodução, não uma medida isolada da rede. As métricas ficam no cartão SD
depois de sair do vídeo; a última etapa de abertura e o trace sobrevivem até
mesmo a um fechamento forçado.

Validação local: `tools/validate_release.ps1`, testes de contrato e
`git diff --check`. Ainda é necessário confirmar no Switch real o tempo de
abertura, a fluidez por pelo menos dez minutos e o cancelamento em dois
episódios de X-Men '97. Se um episódio continuar preso, envie a tela de
Configurações > X após reabrir o Nplay, sem reproduzir outro título antes.
