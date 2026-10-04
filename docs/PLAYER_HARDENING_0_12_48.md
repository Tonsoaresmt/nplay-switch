# NRO 0.12.48 — correções demonstradas e limites da validação

Data: 04/10/2026. Branch: `codex/switch-player-hardening`.
Base: `ca5a7a8` (`origin/codex/switch-rebuild`, 0.12.47).
Checkout: `C:/NplaySwitch/.codex-tmp/switch-access-expiry`.
Escopo: SDL2/FFmpeg/libcurl nativo. Não é alteração do site/Android.

## O que foi corrigido

| Falha reproduzida | Correção | Evidência local |
|---|---|---|
| `completed=1` suprimia a pergunta de retomada, mas enviava a posição final ao player | Obra concluída começa em zero; obra parcial conserva sua retomada | Teste executa os blocos reais de `main.c` para abertura direta, R2 e remux sequencial. A base .47 falha: esperado 0, recebido 1320 |
| Uma falha local podia acionar `/session/:id/fail`; servidores antigos desativavam a fonte compartilhada | NRO não chama mais essa rota. Solicita `/stream/:id` com `exclude_source_ids`, conservando o descritor anterior até resposta válida | API C real com transporte simulado: nenhuma chamada destrutiva, descriptor completo, servidor que ignora exclusão, formato incompatível, HTTP 409/401/502 e cancelamento |
| A primeira variante incompatível escondia alternativas nativas posteriores | A busca de compatibilidade continua pelas variantes | Fixture com `embed` antes de MP4 passa |
| HTTP 401 de GET perdia `reason=expired`; erro de variantes era substituído por “sem alternativa” | JSON de erro é interpretado e preservado; expiração não vira ausência de fonte | Teste de 401/expired e 502 na consulta de variantes, sem POST posterior |
| Retry de legenda dependia de `scur` (aplicada), não da escolha desejada | Estado desejado separado, tentativas em 2/5/10/20 s; faixa anterior permanece até sucesso | Worker real com pthread/join, falha, resposta atrasada, cancelamento isolado, substituição, desligar, falha de criação da thread e rollover dos ticks |
| Player/HLS anunciavam 8 MiB, mas os dois transportes de texto recusavam acima de 4 MiB | Constante única de 8 MiB em player, libcurl e metadados HLS | Funções reais de rede, com libcurl simulado: 5 e 8 MiB aceitos; 8 MiB + 1 rejeitado, inclusive sem Content-Length e no corpo de HTTP 503 |
| Corpo HTTP de erro não era contado no teto do streaming de texto | Conta todos os bytes, antes de decidir se entrega ao parser | Resposta 503 acima do teto aborta sem entregar texto ao parser |
| Cache de legendas removia a query inteira e truncava URLs grandes | Query integra a identidade; fragmento é ignorado; chave grande não é reutilizada | Código real da chave: `?track=pt` difere de `?track=en`; fragmentos não alteram identidade; URL grande é rejeitada |
| “Oi” era tratado como sílaba e sumia junto de letreiro | Pequenos grupos de texto curto são preservados; nuvens densas continuam filtradas | Regressão de fala curta sobreposta + teste existente de karaokê pesado |
| Reenvio fora de ordem estendia o fim de um cue sem atualizar a janela de busca | Atualiza o horizonte de busca após extensão | Caso mínimo e 200 reenvios em ordem permutada, sem duplicar cues |
| Algumas asserções negativas da suíte Linux podiam passar com crash/log vazio | Exige exit status zero, RESULT, SUMMARY e vídeo apresentado | Teste do `check()` real rejeita crash, timeout, zero frames, log vazio, erro e falta de resultado positivo |

O limite de download não significa memória ilimitada. O armazenamento de cues
mantém seus tetos: 200 mil cues e 4 MiB de texto decodificado, além dos índices.
O limite antigo de 8192 cues saiu; a expressão “sem limite” da rodada anterior
não é tecnicamente correta. O áudio continua com a instrumentação existente;
não foi introduzido um teto novo de SDL audio nesta rodada.

## Validação executada

- Build ARM64 com `-Wall -Wextra -Werror` e devkitPro: sem erro/aviso.
- `tools/validate_release.ps1 -SkipBuild`, **sem** `-SkipMediaFixtures`: passou,
  incluindo os novos testes obrigatórios e as suites existentes de buffer,
  concorrência, clock, barreira de seek, supervisor, áudio, toque, sequência de
  episódios, pareamento, remux chunked e HLS multifaixa/WebVTT.
- Fixture chunked: 203 frames reconhecidos sem Range.
- Fixture HLS: cinco faixas, duas cues, abertura e seek decodificados.
- Karaokê pesado: 33406 cues, cerca de 1280 KiB no armazenamento medido pelo teste.
- `git diff --check`: sem erro de whitespace.

Os testes extraídos executam os blocos e funções C do projeto, não apenas uma
reimplementação do comportamento esperado. Alguns substituem rede/SDL/decoder
por respostas controladas; isso permite provocar falhas, **mas não comprova o
driver do Switch nem a produção**. A fixture de mídia usa FFmpeg do computador,
não a aceleração NVTEGRA do console.

A primeira execução completa parou por falta de `SDL_AtomicGet` em dois stubs
antigos de API; os stubs foram completados e a suite foi repetida. Não se usou
`-SkipMediaFixtures` nem se ignorou a falha para anunciar sucesso.

### Repetir no Windows atual

```powershell
$env:HOST_CC = 'C:/devkitPro/msys2/usr/bin/gcc.exe'
$env:TARGET_NM = 'C:/devkitPro/devkitA64/bin/aarch64-none-elf-nm.exe'
$env:NPLAY_BACKEND_ROOT = 'C:/NplaySwitch/.codex-tmp/backend-access-expiry'
& 'C:/devkitPro/msys2/usr/bin/bash.exe' -lc 'cd /c/NplaySwitch/.codex-tmp/switch-access-expiry && make -j4'
& ./tools/validate_release.ps1 -SkipBuild
```

`node tools/test_completed_resume.mjs --baseline` é uma prova **negativa**:
deve falhar ao executar os blocos antigos de `ca5a7a8`. Não faz parte do
critério de sucesso da build corrigida.

## Big Bang Theory: não confundir duas causas

O usuário informou temporadas 2/3/4, sem episódio exato nem trace atual.
Foi comprovado um erro de retomada de conteúdos concluídos no NRO e ele está
corrigido. Isso **não prova** a origem de um HTTP 409 “Nenhuma fonte ativa”.

O resolvedor do servidor não exclui um conteúdo apenas por ele estar marcado
como visto. Já `/fail`, em uma revisão antiga do backend (`f3b1d5d` local),
podia desativar uma fonte globalmente. `origin/main` observado (`caacb51`)
trata essa troca como escolha de sessão, mas não foi confirmado o deploy nem
o estado das fontes já desativadas. A proteção no NRO independe desse deploy.

Não houve edição de banco, reativação automática de fontes, deploy de backend
ou leitura de credenciais de outros projetos. Não reativar fontes em lote:
algumas podem ter sido desativadas corretamente por conteúdo/pack errado.

Próxima investigação de produção: trace novo com `POST /api/stream/<itemId>`
e código HTTP; conferir nesse item fontes ativas, asset R2 e motivos de
quarentena. Sem esses dados não anunciar que as temporadas estão reparadas.

## Pendências antes de declarar estabilidade no console

1. Reproduzir um episódio concluído de Big Bang Theory (T2/T3/T4) e um parcial.
   Concluído deve iniciar em zero; parcial deve manter a escolha de retomada.
2. Anime R2 com legenda: iniciar, trocar faixa, falhar a rede, trocar novamente,
   desligar e avançar/voltar. Nenhuma legenda tardia deve substituir a escolha.
3. Trocar idioma do áudio, pausar/retomar e usar saltos agrupados. Conferir
   posição, idioma e continuidade; esta rodada não mudou a política de áudio
   nem o seek posicionado trazido pela .45.
4. Reprodução longa (60+ minutos) e retomada após pausa/rede interrompida;
   recolher boot/player/network traces se falhar. Não atribuir automaticamente
   qualquer reconexão a certificado ou expiração.
5. Rodar `tools/host_player/suite.sh` em Linux nas duas redes após recompilar.
   **Não foi executado nesta rodada** (sem runtime Linux configurado). Foi
   testado aqui somente seu guard de asserções, não a suite integrada inteira.

Assinatura renovada na URL de legenda pode causar novo download: o cache agora
favorece correção da faixa, não reutilização insegura. Se necessário otimizar,
o backend deve fornecer uma identidade estável explícita de faixa/pacote.

Um backend antigo que ignore `exclude_source_ids` será rejeitado se devolver
a mesma fonte. Isso evita um loop inseguro, mas não cria uma fonte alternativa.
Uma resposta incompatível também é rejeitada, mesmo se houver outra candidata.

## Referências oficiais consultadas

- [libcurl MAXFILESIZE_LARGE](https://curl.se/libcurl/c/CURLOPT_MAXFILESIZE_LARGE.html):
  o comportamento varia entre versões; manter limite próprio no callback.
- [libcurl WRITEFUNCTION](https://curl.se/libcurl/c/CURLOPT_WRITEFUNCTION.html):
  chunks não têm terminação nula; retornar tamanho diferente aborta a transferência.
- [SDL2 WaitThread](https://wiki.libsdl.org/SDL2/SDL_WaitThread): resultado só é
  consumido depois do join; não reutilizar o ponteiro da thread após liberação.

## Publicação

0.12.48 é uma build **local de teste** neste registro. Não foi enviada como
Release/latest nem instalada no Switch. Não dizer que o atualizador já a oferece.
O código e os testes devem permanecer na branch de hardening para continuidade.

Artefato final recompilado integralmente (`make -B -j4`) e revalidado:

- `Nplay.nro`: 24233807 bytes.
- SHA-256: `c7afd5677c44f0de100de5286e8efc6ed8b6c28be66c38244bd54945e81abeb4`.
- Release pública consultada ao encerrar: `v0.12.44`, não draft/prerelease.
- Não houve promoção automática das .45/.46/.47/.48 para os consoles.
