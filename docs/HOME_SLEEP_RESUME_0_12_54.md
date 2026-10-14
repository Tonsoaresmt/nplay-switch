# Retorno do HOME e da suspensão — candidato local 0.12.54

## Evidência e alcance

Relato de 14/10/2026: após HOME ou tela apagada/suspensão, a reprodução fica
presa; sair do player e abrir o episódio novamente permite assistir. A auditoria
de 13/10 já demonstrou uma falha na recuperação R2: uma sessão encerrada recebe
404, mas o callback da .53 insiste em renovar o mesmo ID em vez de pedir nova
admissão. O novo teste positivo falha nessa assertion quando usa o callback
original de `3d36eb6` e passa com o callback corrigido.

Isso comprova o defeito no código, **não identifica conclusivamente o incidente
físico**. Os últimos logs fornecidos eram de anime MP4/avanço de episódio, não
de TBBT/R2 acordando. Tela apenas escurecida não é necessariamente suspensão.
Não houve teste de HOME, Wi-Fi, áudio, GPU/NVTEGRA ou sono no Switch nesta rodada.

## Tratamento implementado

- `player_lifecycle.c` registra um hook libnx somente durante `player_run`.
  `OnResume` e retorno de `Background` a `InFocus` sinalizam um flag atômico.
  `OutOfFocus` de applet de teclado não dispara esse caminho. O hook não toca
  rede, decoder, áudio, renderer nem log. O hook é removido ao sair.
- O renderer bombeia eventos **antes** de calcular clocks/decodificar e verifica
  o flag também na espera de apresentação e no callback de abertura. A tentativa
  obsoleta fecha pelo teardown normal; não há acesso concorrente novo ao FFmpeg.
- Após fechar/join dos workers de demux/AVIO/legenda, são descartados sockets
  ociosos e cache curto de metadados assinados. O último quadro pode ser usado
  como fundo da recuperação. O transporte de API usa handles próprios.
- Conservam-se checkpoint, duração conhecida, intenção de pausa, idioma e escolha
  semântica de legenda da mesma reprodução. Não se cria fallback para zero nem
  se marca EOF artificial como episódio visto. Fonte nova remapeia áudio por
  idioma, sem impor o índice manual pertencente à fonte anterior.
- A recuperação de wake não exclui fonte por uma suspensão local. O modo é
  mantido até uma tentativa tocar 30 s ou o usuário realizar outra operação.
- Refresh R2 válido mantém a sessão. Somente **404 definitivo** nesse endpoint
  permite uma nova resolução autenticada do mesmo item, com qualidade mantida.
  O descriptor novo precisa ser nativo/não sequencial; magnet/embed ou R2 sem
  sessão/fonte são rejeitados. Falha conserva o output anterior.
- Não se repete automaticamente um POST de admissão com resultado desconhecido:
  falha desse POST é terminal nessa recuperação. 401/402/403/409/429 também
  encerram a tentativa, exibindo o erro; `401 reason=expired` preserva o retorno
  ao login. Plano, validade, dispositivo e limite de telas não são burlados.
- Falhas transitórias no refresh continuam com retries limitados/canceláveis,
  para Wi-Fi que ainda não voltou. B, toque em Cancelar e sair funcionam também
  durante a espera entre tentativas. Não há promessa de retomada instantânea.
- Outro HOME durante a resolução cancela o job e descarta sua resposta **após
  join**. Nenhum worker pode escrever em uma struct liberada ou aplicar URL
  obtida antes do novo wake.

## Limites que permanecem

Streams hot/MKV sequenciais sem implementação de busca e fontes legadas sem
callback de renovação **não ganham retomada por esta mudança**. O player sai com
aviso e preserva o ponto, em vez de recomeçar em zero ou ficar reconectando.
A implementação de `/seek`/origem temporal nesses streams é outra tarefa.

O cancelamento depende dos callbacks/prazos existentes do transporte. Travamento
nativo da SDK/driver não foi simulado como uma garantia de interrupção imediata.
Notificações do sistema muito separadas podem provocar outra recuperação
limitada; confirmar a ordem real dos eventos no console continua necessário.

TLS, validação de hostname/CA, limites de memória e arquitetura SDL2/FFmpeg/
libcurl permanecem. Nenhum backend, conta, banco, fonte ou pacote R2 foi alterado.

## Validação local

`tools/test_sleep_resume.mjs` compila os módulos C reais e extrai o callback de
`main.c` e a UI/worker de recuperação de `player.c`. Libnx/HTTP/render são
simulados; pthread/ownership/atomics são reais no host.

- 1.000 ciclos de HOME/resume/teclado, registro e remoção do hook.
- Refresh ativo, sessão 404/readmissão uma vez, qualidade/T/E, 401/402/403/409/
  429/503/timeouts/TLS, cancelamento, descriptor parcial/magnet e output anterior.
- 225 recuperações com resposta atrasada, HOME antes e ao terminar join, B/toque/
  quit, thread indisponível, negativa terminal e acesso expirado.
- `test_player_supervisor.mjs`: `player_run` real com pipeline simulado;
  checkpoint/pausa/faixas, retornos repetidos, nova fonte/idioma, negativa sem
  loop, cancelamento e saída segura de stream sequencial.

Controle negativo (deve falhar na assertion de readmissão, não é teste verde):

```powershell
node tools/test_sleep_resume.mjs --baseline-callback
```

Auditoria histórica (exit 0 significa defeito reproduzido na .53):

```powershell
node tools/audit_sleep_resume.mjs --baseline
```

Teste positivo obrigatório e incorporado ao `validate_release.ps1`:

```powershell
node tools/test_sleep_resume.mjs
node tools/test_player_supervisor.mjs
```

Build ARM64 completo `-Werror` e incremental final passaram. Duas execuções da
bateria completa passaram, sem `SkipMediaFixtures`; a última validou o NRO final
abaixo, incluindo os símbolos nativos novos. Não houve rebuild depois dessa
prova. A SDK/SSL/HID/GPU do console não foram executados pelos testes de host.

- NRO 0.12.54 local: **24.282.959 bytes**.
- SHA-256: `a3fa8b5bf2a927e1b898ecb67e79c13c198e2247c36f1ae652b6203c2af899dc`.
- Substitui o candidato local anterior `f441102...`; o teste de internet permanece.
- **Publicação proibida até nova autorização**, conforme resposta explícita do
  usuário em 14/10. Sem push/release/backend/deploy nesta rodada.

## Homologação necessária no console

1. Mesmo episódio R2: HOME por 5/60 s tocando; voltar no mesmo ponto, sem salto
   pelo tempo fora do app. Repetir 5 vezes.
2. Pausar, HOME e suspensão manual de 30 s e 10 min: continuar pausado e depois
   A retomar no checkpoint, com áudio/legenda escolhidos.
3. Acordar sem Wi-Fi, retornar Wi-Fi após 5/20 s; B e toque devem sair. Falha não
   pode recomeçar do zero nem mudar a fonte globalmente.
4. HOME durante preparo e durante recuperação: não aplicar resultado antigo,
   não crashar e não marcar a obra como concluída.
5. Conta expirada, sessão encerrada e limite de telas: mensagens reais/retorno
   ao login, sem tentativas ilimitadas nem bypass de autorização.
6. MP4 direto/TorBox Range e stream sequencial: distinguir retomada suportada
   da limitação explícita. Conferir legenda após wake e depois de seek.
7. Sessão de 60+ min; salvar trace após o incidente sem abrir outra obra.
   Crash nativo exige também relatório do Atmosphère com PC/backtrace.

## Fontes primárias

- [libnx applet.h](https://switchbrew.github.io/libnx/applet_8h.html): hooks,
  focus e modos de suspensão. Nenhum modo `NoSuspend` foi imposto.
- [libnx applet.c](https://github.com/switchbrew/libnx/blob/master/nx/source/services/applet.c):
  despacho de `FocusStateChanged`/`Resume` pelo pump do applet.
- [Port SDL2 oficial devkitPro](https://github.com/devkitPro/pacman-packages/blob/master/switch/SDL2/SDL2-2.28.5.patch):
  `SWITCH_PumpEvents` chama `appletMainLoop`, não eventos de janela genéricos.
  O objeto `SDL_switchvideo.o` da SDK instalada também importa essa função.
- SDK instalada: `C:/devkitPro/libnx/include/switch/services/applet.h`; assinaturas
  conferidas e build ARM64 utiliza a SDK real, não o stub do teste.
