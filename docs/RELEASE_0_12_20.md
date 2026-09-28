# Nplay Switch 0.12.20

O HUD criado para aproximar o Switch do player do site/PC foi portado de verdade
para a linha atual, sem remover as correcoes de HLS, memoria, NVTEGRA,
diagnostico e recuperacao da 0.12.x. Pausa, timeline, abertura, buffering e o
painel de audio/legendas agora pertencem ao mesmo sistema visual.

A selecao de idioma passa a seguir o contrato real do site. Alem de PT-BR
identificado, ela reconhece os pacotes R2 antigos em que a dublagem e a segunda
faixa sem tag ao lado de uma faixa inglesa. Versao da serie, preferencia da conta
e continuidade por idioma entre episodios sao respeitadas. Preferencias manuais
locais ficaram isoladas por perfil.

O painel exibe todas as faixas em duas colunas, com idioma/nome, codec, canais e
indicacao da faixa ativa. A busca analogica usa a timeline moderna e so executa
o seek apos A; B cancela. Buffering preserva o ultimo quadro e mostra spinner,
sem a caixa antiga sobreposta.

Validacao local: build limpo sem avisos, politica de audio com `-Werror`, contrato
site/Switch, relogio, API hot/R2, sequencia de episodios, remux chunked, TLS e
simbolos FFmpeg/NVTEGRA aprovados. Teste no Switch real continua obrigatorio.
