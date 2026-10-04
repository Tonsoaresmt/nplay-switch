# Nplay Switch 0.12.49 — letreiros e estabilidade integrados

Versao NRO publicada a pedido do usuario para teste pelo atualizador: [v0.12.49](https://github.com/Tonsoaresmt/nplay-switch/releases/tag/v0.12.49). Ainda requer confirmacao no Switch fisico antes de ser tratada como validada no console.

## Mudancas

- Placas, onomatopeias e creditos com posicao no WebVTT aparecem separados das falas, dentro da area real do video.
- Textos iguais em lugares diferentes continuam visiveis. Camadas repetidas no mesmo lugar sao deduplicadas.
- Uma fala nao desaparece por ter o mesmo texto e tempo de uma placa.
- Muitos letreiros nao expulsam falas da fila. O limite total continua sendo 32 cues, com no maximo oito letreiros na fila interna.
- Letreiros prolongados por reenvio permanecem ate o fim correto.
- Inclui as correcoes de estabilidade da branch de hardening: retomada de episodios concluidos desde zero, recuperacao sem desativar fontes globalmente, retry de legenda mantendo a faixa anterior, limite de download de 8 MiB e cache que distingue faixas pela URL completa.

## Limites importantes

Legendas antigas do R2 sem coordenadas nao podem recuperar a posicao original somente com esta atualizacao. O conversor do backend e o reparo controlado desses pacotes sao etapas separadas; nao houve deploy nem reprocessamento nesta rodada.

Esta versao nao promete eliminar todo buffering ou imitar todas as fontes/cores/animacoes do ASS. Os testes locais nao substituem os drivers, GPU, audio e Wi-Fi do Switch.

Detalhes e roteiro: `docs/PLAYER_INTEGRATION_0_12_49.md`.
