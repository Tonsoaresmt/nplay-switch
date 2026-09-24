# Nplay Switch 0.12.5

Alguns episódios de X-Men '97 permaneciam em "Preparando vídeo" na 0.12.4.
O leitor HLS podia repetir indefinidamente a espera pelo primeiro byte de um
segmento. Agora a falta de dados retorna erro após 20 segundos; a tentativa
de abertura HLS e chegada do primeiro quadro também têm limite. O supervisor
do player pode então renovar a sessão ou escolher outra fonte, e apresenta
erro quando nenhuma delas inicia.

O diagnóstico em Configurações > X mostra os eventos recentes sem o prefixo de
memória que cortava código HTTP e motivo na tela. As métricas de quadros são do
último vídeo que chegou ao decoder; um vídeo preso antes disso pode deixar as
métricas anteriores visíveis. O rastreio completo continua em
`switch/.nplay-player-trace.log`.

Teste pelo menos dois episódios de X-Men '97 e um título que já reproduzia.
Se algum falhar, fotografe Configurações > X logo após sair da tentativa e
informe temporada/episódio. Esta versão evita espera sem fim; ainda é preciso
medir no console se a causa dos segmentos lentos está no R2, na rede ou no
pacote da obra.
