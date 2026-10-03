# Retomada, pausa e perda de posição — NRO 0.12.42

## Relato

Usuário confirmou versão 0.12.41: após avanço/retorno ou pausa/retomada, o
player aguarda dados, reconecta e recomeça a obra. Novos traces e título não
foram fornecidos nesta rodada. Não atribuir a causa de todo buffering a esse
fallback; o reinício automático, porém, foi reproduzido no supervisor real.

## Causas confirmadas no cliente

- `player_run` tinha fallback `resume-from-start`: falha HLS após seek, antes
  de apresentar quadro, transformava `attempt_start` em zero. Era intencional
  no código, mas errado para uma recuperação transparente. A seleção de fonte
  posterior também podia herdar esse zero.
- Seek inicial com retorno negativo não impedia a reprodução desde o começo.
- Retomadas até 3 segundos ou a menos de 5 segundos do fim eram ignoradas.
- O tempo de buffering podia incluir o tempo que o usuário permaneceu pausado,
  no seletor de faixas ou na timeline, provocando recuperação antecipada.

## Mudanças

1. Removido fallback automático para zero. Renew/fallback repetem a posição
   preservada e conservam a política de tentativas limitada. Se não conseguem
   retomar, encerram com erro sem apagar o checkpoint do resultado.
2. `player_recovery_position` ignora resultado inválido/sem quadro. Apenas seek
   confirmado pode solicitar zero. Troca de áudio conserva a posição atual.
3. Seek inicial recusado interrompe a tentativa; não apresenta o início como
   sucesso. Todos os pontos positivos passam pelo seek, incluindo perto do fim.
4. Preroll também protege retomada de arquivo não HLS: o quadro anterior à
   posição pedida não deve aparecer como retomada. EOF durante preroll é falha,
   não conclusão/autoplay. Remux sequencial sem seek tenta recuperação/fonte
   alternativa ou devolve erro; não reinicia silenciosamente para fingir retomada.
5. Pausa/menus/timeline zeram a janela de buffering; watchdog de pós-seek
   também suspende contagem nesses estados. Relógios já eram reancorados ao
   retomar. Não criar conexão nova só por pausar.
6. L/R/ZL/ZR continuam abrindo prévia com A confirma/B cancela, já existente na
   0.12.41. Não remover essa proteção contra gatilho acidental. Nova evidência
   do console é necessária se houver seek sem confirmação A.

Referência oficial conferida: [FFmpeg 7.1, demux/seek](https://ffmpeg.org/doxygen/7.1/group__lavf__decoding.html).
Um retorno negativo de seek não comprova reposicionamento bem-sucedido. Não
alterar estruturas privadas de HLS nem executar seek concorrente a av_read_frame.

## Testes e limites

- `tools/test_player_supervisor.mjs --baseline`: extrai `player_run` do commit
  7c85e75 (0.12.41) e executa o C original. Pipeline/rede/SDL são stubs.
  Após seek de 3000 para 3060 s e falha de retomada, a próxima tentativa recebe
  zero: assert falha, reproduzindo a perda de posição.
- Versão atual passa: falhas repetidas de seek/renew conservam 3060; interrupção
  após reprodução conserva 3002; seek explícito para zero funciona; tentativas
  esgotadas e cancelamento preservam 3000; retorno não apresenta quadro não
  pode sobrescrever checkpoint. Não é reprodução no hardware.
- Teste de política também cobre avanço/retorno, NaN e resultado negativo.
- Build ARM64 -Wall -Wextra -Werror e validate_release.ps1 completo passaram,
  sem pular fixtures HLS, seek, áudio, WebVTT, remux ou símbolos de hardware.
- NRO de 24201039 bytes; SHA-256:
  `29cb1dbdff768f1adcd3f27f5ad189ebed0aaa5d49ea8c8a2aec378a2cb218db`.

Não declarar que a causa de espera de rede após qualquer controle foi eliminada.
O que foi comprovado é a remoção do reinício automático nesses cenários. Falta
teste físico: mesma obra aos 10/30/50 min, pausa longa, cancelar gatilho acidental,
confirmar salto para frente/trás, seek ao início e próximo do fim, Wi-Fi oscilando.
Enviar traces atualizados se a posição não retomar. Guardar versão/título/tempo.

## Continuidade

Checkout: C:/NplaySwitch/.codex-tmp/switch-access-expiry. Branch local:
codex/switch-access-expiry; publicação em codex/switch-rebuild. NRO somente como
asset de Release. Servidor, idioma e legendas não foram alterados nesta rodada.
Deploy antigo de expiração segue pendente de reconferência, sem relação direta
com a lógica de reinício automático aqui corrigida.
