# Integracao do player NRO 0.12.49 — 04/10/2026

## Codigo e continuidade

Checkout ativo: `C:/NplaySwitch/.codex-tmp/switch-access-expiry`, branch `codex/switch-player-hardening`.

Integrados os commits `ba7d46d` (hardening) e `2b8d0d0` (letreiros posicionados), ambos baseados em `ca5a7a8` (0.12.47). As duas linhas tinham o numero 0.12.48; o resultado usa **0.12.49** no Makefile e em `include/update.h`.

A integracao automatica de `player.c` e `subtitle_store.c` foi revisada. O conflito do validador foi resolvido mantendo tanto a politica nova de retry quanto a verificacao de settings posicionados. A regressao antiga de subtitle_store tambem passou a ligar subtitle_queue, agora dependencia real do armazenamento.

Nao foram alterados o checkout principal antigo, arquivos locais do usuario, banco ou servidor. Nenhum NRO e rastreado em Git. Apos a geracao validada, o usuario pediu expressamente a publicacao para testar pelo atualizador. A **Release v0.12.49 foi publicada como latest**, sem atribuir ao build validacao fisica ainda inexistente.

Publicacao confirmada em **04/10/2026, 20:31:27 UTC**: [v0.12.49](https://github.com/Tonsoaresmt/nplay-switch/releases/tag/v0.12.49), release ID `403196321`, alvo `247610129878aaa665e4a147c1698e182f786ba9`, `draft=false`, `prerelease=false`. A API `/releases/latest` retornou essa versao e o asset Nplay.nro `uploaded`, 24241999 bytes, com digest igual ao SHA-256 local registrado abaixo. O arquivo de checksum tambem foi publicado. Documentacao posterior nao altera o binario dessa release.

## Defeitos corrigidos e evidencias

1. `subtitle_signs_add` deduplicava apenas texto. Agora exige tambem posicionamento e ancoras; tolerancia menor que 0.005% corresponde a menos de 0.064 px em 1280px.
2. `subtitle_queue_push_at` confundia placa e fala com os mesmos tempos/texto. Identidade agora inclui o posicionamento, inclusive ausencia dele.
3. A extensao fora de ordem atualiza o horizonte `max_short`, preservado da branch hardening, e foi testada tambem com letreiro.
4. A fila interna continua com 32 slots. Letreiros usam no maximo oito, substituem somente outros letreiros e sao recusados quando so ha falas na fila cheia. Uma fala nova recupera primeiro um slot de letreiro quando necessario. Como qualquer fila limitada, excesso de falas ainda pode substituir falas antigas; nao afirmar armazenamento ilimitado de todos os cues embutidos.

Antes das correcoes, na integracao, `test_positioned_subtitles` falhou em tres pontos (a extensao ja era corrigida pelo hardening). Depois: **11 verificacoes, zero falhas**, incluindo as duas ordens de insercao, camadas duplicadas, capacidade de 32 falas e rajada de 10 mil letreiros protegendo a fala.

O teste existente de store que aceitava a perda de um letreiro foi corrigido: sao quatro objetos na tela, nao tres, quando ha duas onomatopeias iguais em pontos distintos e duas placas adicionais.

## Verificacao de desenho e transporte

- `test_sign_geometry.c` executa a funcao `draw_signs` extraida do fonte real sem modifica-la. Fontes/desenho sao spies deterministas; seis casos cobrem tarjas horizontais/verticais, ancoras, multiline, bordas e limite de oito letreiros. Isso **nao e uma captura SDL real nem teste fisico**.
- Fixture HLS contem video, dois audios, duas legendas e um cue posicionado. `ffprobe` comprova `WebVTT Settings` no packet da rendition HLS selecionada e do VTT direto, com PTS de 10s.
- A primeira versao desse teste interrogava o master A/V inteiro, em que ffprobe nao apresentou packets de legenda apos a descoberta. Foi corrigido para consultar a rendition separada que `load_external_hls_subtitle` realmente abre. Nao se dispensou o teste nem se modificou o player para passar uma verificacao artificial.
- CLI usada nas fixtures: FFmpeg 8.1.1 configurado neste Windows; NRO linkado contra SDK FFmpeg 7.1. Os probes C reais e o build SDK complementam a fixture, mas nao equivalem a rodar a pipeline integral no console.

## Build e validacao

Build completo forcado, com `-Wall -Wextra -Werror`:

```powershell
& 'C:\devkitPro\msys2\usr\bin\bash.exe' -c 'export DEVKITPRO=/opt/devkitpro DEVKITA64=/opt/devkitpro/devkitA64; export PATH=/opt/devkitpro/devkitA64/bin:/opt/devkitpro/tools/bin:$PATH; make -B -j4'
```

Usar os caminhos MSYS `/opt/devkitpro`; a tentativa inicial com `DEVKITPRO=C:/devkitPro` no shell nativo nao encontrou pkg-config. Nao houve instalacao de bibliotecas nem erro de codigo nessa tentativa.

Validacao obrigatoria completa, sem omitir fixtures:

```powershell
$env:HOST_CC='C:\devkitPro\msys2\usr\bin\gcc.exe'
$env:TARGET_NM='C:\devkitPro\devkitA64\bin\aarch64-none-elf-nm.exe'
$env:NPLAY_BACKEND_ROOT='C:\NplaySwitch\.codex-tmp\backend-access-expiry'
& .\tools\validate_release.ps1 -SkipBuild
```

`-SkipBuild` evita apenas repetir o build completo ja realizado; **nao** omite testes de midia. Nao usar `-SkipMediaFixtures` para declarar validacao completa.

NRO: **24.241.999 bytes**.

SHA-256: `b10eea59d6762dc3eedc7453060dc13572ba0214e65ded9c6dc9be91a130fc0f`.

Resultado final: **validador completo terminou com codigo 0**, sem `-SkipMediaFixtures`, e confirmou versao, tamanho, SHA-256 e simbolos obrigatorios do binario. Passaram worker/demux pthread, barreira de seek, supervisor de recuperacao, transporte TLS limitado, retry/cache/cancelamento/join, replay concluido, failover nao destrutivo, clock, audio, touch, legendas, episodios, pairing e fixtures reais de remux/HLS/VTT. `git diff --check` tambem passou.

## Validacao fisica ainda pendente

Nao foi executado NRO em Switch fisico nem a suite Linux integral nesta rodada. Backend novo em producao e o pacote exato das fotos nao foram verificados.

O usuario autorizou promover a versao no atualizador para realizar estes testes no console; continuam pendentes:

1. Mesmo episodio com fala e tres letreiros posicionados: fala embaixo, onomatopeias independentes no quadro.
2. Pausar/retomar, seek para frente e para tras, trocar audio e legenda: fala/letreiro acompanham a posicao e nao desaparecem prematuramente.
3. Conteudo com tarjas, placa longa/acento e HUD aberto; conferir legibilidade e colisao eventual com placas perto da faixa de fala.
4. Perda curta de Wi-Fi, falha na legenda, retorno e episodio seguinte: faixa anterior preservada, retry e cancelamento continuam corretos.
5. Sessao >60 min com traces atuais. Big Bang Theory T2/T3/T4 sem fonte exige ainda investigacao autorizada do backend; nao confundir com a correcao de replay concluido.

Se o arquivo VTT nao possuir `line`/`position`, parar a investigacao de layout no cliente e conferir a geracao do pacote. Nao fazer reprocessamento em massa nem desativar TLS.
