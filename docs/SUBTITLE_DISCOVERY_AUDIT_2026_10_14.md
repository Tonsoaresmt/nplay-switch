# PT-BR ausente no NRO e disponível no navegador — auditoria

## Escopo e estado

Usuário confirmou **0.12.53** no Switch. Na foto do NRO, áudio japonês e
somente a legenda inglesa são listados. A foto do navegador identifica
“Dragon Ball Super: Beerus” e anuncia PT-BR, inglês e espanhol. A captura web
ainda mostra carregamento: prova a lista anunciada, não a renderização de cues.

Esta rodada é investigação: **não alterou o aplicativo, versão, NRO, backend,
contas, pacotes ou release**. Foram adicionados este relatório e uma ferramenta
diagnóstica. .54 local permanece no commit de código `0deb304`, NRO SHA256
`a3fa8b5bf2a927e1b898ecb67e79c13c198e2247c36f1ae652b6203c2af899dc`.
Publicação continua não autorizada. Não tratar esta auditoria como correção.

Ainda faltam número/ID do episódio, fonte/variante e traces desta tentativa.
“Mesmo conteúdo” não comprova mesmo arquivo: o resolver pode escolher R2,
MP4 direto ou TorBox/hot e variantes diferentes. Não houve acesso autenticado
ao pacote de produção nem leitura de credenciais para contornar essa ausência.
A inspeção Git de `C:/iptv` foi recusada por permissão; não foi contornada.

## Defeitos demonstrados no código

### 1. Uma legenda nativa impede consultar as externas

Em `player.c`, o probe do hot/remux só é solicitado se `nsub == 0`. Se FFmpeg
já encontrou uma legenda inglesa, `nsub == 1`; o NRO não consulta o endpoint
que poderia anunciar PT-BR e espanhol. Os caminhos de X/toque que permitem
repetir o probe também dependem de `nsub == 0`.

O navegador local inspecionado em `public/js/player.js` usa o probe da sessão
hot quando não possui a bridge nativa. Ele não exige zero legendas embutidas
antes de anexar as faixas externas. A resposta original do MKV e o MP4 remuxado
não precisam conter as mesmas trilhas: o backend oferece a extração WebVTT
separadamente. O snapshot backend c9 contém os aliases `/stream/hot/:sid/probe`
e `/stream/hot/:sid/subtitles/:index.vtt`.

**Reprodução:** uma faixa nativa EN e um probe simulado com PT/EN/ES deixam
o menu do NRO com uma faixa e **zero chamadas ao probe**. O cenário corresponde
ao tipo de diferença da foto, mas falta confirmar que essa tentativa tinha SID
hot válido e usou a mesma fonte do navegador.

### 2. Quantidade de faixas não equivale à identidade

O fallback HLS só substitui a lista quando `manifest_subtitle_count > native_nsub`.
Com uma faixa nativa EN e uma rendition PT no master, as duas contagens são 1;
o manifesto PT é ignorado. Ter o mesmo número não prova mesmos idiomas,
renditions, flags de forced ou nomes.

O controle com master PT/EN/ES e apenas uma faixa nativa funciona: três é maior
que um. Portanto **não se demonstrou falha em todo R2**. Se o master desta obra
anunciar três faixas corretamente, precisamos examinar o caminho/parser/delivery
real, não atribuir automaticamente a este defeito de contagem.

### 3. Probe inicial indisponível não tem retry automático de descoberta

O worker faz um GET limitado a 256 KiB e prazo de 5 s. HTTP 503 deixa a consulta
sem faixas; esse bloco inicial não agenda outro probe. Existe repetição manual
por X quando a lista é vazia, mas não o backoff automático do navegador local,
que tenta três consultas enquanto os metadados ainda podem estar preparando.

Isso é diferente do retry de **download de uma legenda já descoberta**, que o
player já possui. Recuperar o corpo de VTT não recupera uma faixa ausente da lista.

## Reproduções e controles

`tools/audit_subtitle_discovery.mjs` extrai o bloco C real de descoberta e
compila os parsers reais `hot_subtitles.c`/`hls_manifest.c`/cJSON. Fonte, FFmpeg,
resposta HTTP e render são simulados; não decodifica Dragon Ball nem executa GPU.

| Entrada simulada | Resultado atual | Classificação |
| --- | --- | --- |
| Zero nativas, probe PT/EN/ES | Três externas | Controle correto |
| Uma nativa EN, probe PT/EN/ES | Uma nativa, probe não consultado | Defeito reproduzido |
| Uma nativa EN, master com uma PT | Mantém a nativa EN | Defeito de identidade reproduzido |
| Uma nativa, master PT/EN/ES | Três externas | Controle correto |
| Zero nativas, probe HTTP 503 | Uma consulta, zero faixas | Lacuna de retry reproduzida |
| Nova consulta após o probe ficar pronto | PT/EN/ES descobertas | Controle correto |
| SID inválido | Nenhuma chamada | Controle de segurança correto |

Os cenários foram executados na função da .53 (`cadecdb`) e na atual .54 local;
ambas reproduzem os defeitos. **Exit 0 deste AUDIT significa defeitos reproduzidos,
não aprovação do player. Não adicioná-lo como teste verde de release.**

```powershell
node tools/audit_subtitle_discovery.mjs --baseline-053
node tools/audit_subtitle_discovery.mjs
```

O teste preexistente `test_subtitle_io.mjs` também passou (worker/HTTP/SDL
simulados): cancelamento, resposta tardia, join, 404/503 e limite de corpo.
O parser aceitou a faixa `por`/ASS da fixture; não há evidência aqui de uma
limitação do Switch para o idioma português. O teto de 16 faixas não explica
uma lista web com apenas três. O filtro de áudio do master mantém as linhas
de legenda; não foi demonstrado que a otimização de áudio removeu PT-BR.

## Correção indicada — ainda não implementada

1. Inventário único de faixas com origem explícita: nativa, rendition HLS ou
   probe/VTT externo. Descoberta de uma nativa não deve bloquear outras origens.
2. Reconciliar por identidade/rendition/índice na origem, idioma, nome e forced,
   não apenas quantidade ou idioma. Duas PT podem ser diálogo e letreiros;
   deduplicação por idioma perderia opções legítimas.
3. Consulta de metadados em segundo plano, independente do vídeo, com retry
   limitado para 503/timeouts. Usar cancel próprio, geração de reprodução e
   aplicar snapshot somente após join; não reutilizar `demux_abort` do vídeo.
4. Seleção de idioma ocorre depois da descoberta. Se chegar PT tardiamente,
   aplicar a preferência automática somente se o usuário não escolheu OFF ou
   outra faixa explicitamente. Não interromper o vídeo para atualizar o menu.
5. Permitir atualizar a descoberta em X mesmo com uma faixa já listada;
   falha mantém lista/seleção anteriores e não transforma ausência em OFF.

**Não basta remover `!nsub`:** o código atual usa um booleano global para decidir
entre lista nativa e externa, e ainda compara contagens. Uma troca superficial
pode continuar escondendo PT ou aplicar índice de uma origem à outra.

Preservar TLS/hostname, limites, SID textual separado do heartbeat numérico,
guard de resposta completa, cache de cues e seleção semântica já existentes.
Não ampliar tetos, reprocessar R2 em lote nem trocar a fonte por palpite.

Regressões necessárias: nativa EN + externa PT, contagens iguais/ordem diferente,
PT forced + PT diálogo, duplicatas reais, 503→200, 404/410 sem loop, cancel,
resposta tardia após outro episódio, escolha OFF antes da chegada, seek/HOME,
16+ faixas e preservação de reprodução/posição/faixa antiga durante falha.

## Para identificar o incidente da foto

Copiar `nplay-player-trace.log` e `nplay-network-trace.log` imediatamente após
abrir esse mesmo episódio no NRO, antes de iniciar outra obra. Conferir:

- `source/resolved`, item/source/delivery/container/provedor;
- `streams/enumerated`, `subtitle/hot-probe`, `subtitle/manifest-fallback`;
- `tracks/selection` e os metadados do master/probe da **mesma fonte**.

Não registrar nem publicar URLs assinadas, SID textual, JWT ou cookies. A
comparação autenticada deve usar ferramentas/credenciais normalmente configuradas
e autorização adequada; a foto sozinha não autoriza extrair login do navegador.

## Referências primárias

- [RFC 8216, EXT-X-MEDIA](https://www.rfc-editor.org/rfc/rfc8216.html#section-4.3.4.1):
  renditions e atributos LANGUAGE/NAME/URI/FORCED são identidades distintas,
  não uma simples contagem de faixas.
- [FFmpeg, demuxer HLS](https://ffmpeg.org/ffmpeg-formats.html#hls-2): relação de
  streams e programas do manifesto. Enumerar AVStreams não substitui o probe
  da fonte original quando o remux omite faixas.
- Código local do NRO, função da .53 e snapshot c9 de `hot-stream.js`/hot-probe;
  código frontend acessível `public/js/player.js`. Nenhum deploy foi confirmado
  nesta auditoria; não tomar o checkout local como prova da versão de produção.
