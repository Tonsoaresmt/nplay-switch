# Nplay Switch 0.12.38 — legendas de remux em fluxo

Esta versao corrige o caminho que faltava para legendas em fontes remux/debrid.
Nessas fontes, o video fMP4 pode tocar normalmente, mas suas legendas nao aparecem
como `AVStream`; elas sao extraidas pelo servidor depois que o video ja começou.

O Switch agora preserva o identificador textual dessa extracao separadamente da
sessao numérica usada por progresso e heartbeat. Primeiro consulta as faixas
disponiveis e, ao selecionar uma, le o WebVTT progressivamente. O video nao e
reaberto e a legenda passa a aparecer conforme os cues chegam.

Garantias do cliente:

- apenas HTTPS com validacao de CA/hostname, sem redirects;
- identificador e indice estritamente validados, sem URL assinada/SID no trace;
- 4 MiB maximos mesmo se o servidor omitir `Content-Length`;
- cancelamento separado da leitura de video; B/toque cancela so a legenda;
- faixa atual preservada em 404, 503, cancelamento, timeout ou VTT malformado;
- thread da legenda sempre e aguardada antes de limpar a obra/trocar episodio.

Validado localmente em 01/10/2026: compilacao ARM64 limpa com `-Werror`, contratos
de API/remux, parser de VTT parcelado (BOM, CRLF, keepalive, EOF, limite e falha de
callback), cancelamento por B/toque, 404/503, resposta tardia, HLS multifaixa,
seek e fixture WebVTT direto. O arquivo gerado foi `Nplay.nro` com SHA-256 a ser
registrado na release.

Ainda requer teste no Switch real: selecionar uma legenda remux, esperar cues,
fazer seek, desligar/trocar faixa e abrir outro episodio durante a extracao. Isso
e validacao de hardware e rede; nao e possivel simulá-la integralmente no PC.
