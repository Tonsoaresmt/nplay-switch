# Nplay Switch 0.12.10

O botao **Continuar** continua buscando o ponto salvo. Esta versao corrige a
preparacao da retomada HLS: estabelece a linha do tempo antes do seek, busca
pela faixa de video e decodifica os quadros anteriores ao ponto salvo sem
iniciar o audio nem envelhecer o relogio do player durante a leitura da rede.
O primeiro quadro no ponto de retomada inicia a sincronizacao de audio e video.

A recuperacao da versao anterior permanece como protecao se uma fonte nao
entregar nenhum quadro. Tentativas sem quadro nao substituem o progresso salvo.

Validacao: o HLS/R2 de X-Men '97 T1E1 foi lido e decodificado a partir dos
segundos 48 e 600 em sondagens repetidas; compilacao ARM64 e contratos locais do NRO
passaram. A reproducao no hardware Switch nao pode ser comprovada neste ambiente.
