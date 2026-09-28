# Nplay Switch 0.12.19

Esta revisao corrige uma regressao da 0.12.18 na selecao de idioma. Uma escolha
antiga salva localmente como ingles podia ganhar da configuracao Dublado da
conta. Agora a conta e a fonte de verdade: Dublado escolhe portugues quando a
faixa existe; Legendado escolhe o audio estrangeiro e procura legenda em
portugues. A escolha salva e a continuidade por indice continuam como fallback
quando a fonte nao oferece a versao solicitada ou nao identifica os idiomas.

O player recebeu uma mudanca visual efetivamente perceptivel. Ao pausar, mostra
um painel amplo no estilo do player do PC, com titulo, estado, acao principal e
atalhos de audio/legendas. A barra inferior exibe o idioma ativo e o estado da
legenda diretamente, com controles e hierarquia mais limpos. O loader, a
timeline, os modais de faixa e todas as correcoes de estabilidade da linha
0.12.x foram preservados.

Validacao local: compilacao devkitA64 sem erros nem avisos e suite completa de
release aprovada. Confirmar no Switch real uma obra dublada, outra legendada,
troca manual de faixa e o novo painel pausado.
