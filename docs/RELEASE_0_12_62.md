# Nplay Switch 0.12.62

Atualização de estabilidade do player e seleção de áudio/legendas.

- Corrige uma recuperação indevida em remux quando o leitor termina, mas ainda
  existe vídeo na fila. Mantém os limites de memória.
- Melhora a escolha do áudio: evita comentário/audiodescrição involuntários e
  respeita a indicação de áudio original quando fornecida pela fonte.
- Prefere legenda PT completa à faixa só de placas/letreiros na escolha automática.
- Refaz consultas limitadas quando a legenda PT ainda não apareceu e ativa o
  idioma descoberto depois, sem contrariar OFF ou uma escolha manual.
- Carregamento tardio e retries de legenda remux em segundo plano, com
  cancelamento e prazo; prioridade PT dentro do teto de 16 faixas.

Instale pelo atualizador ou substitua somente `Nplay.nro` na pasta `switch` do
cartão. Não apague configurações ou perfis. O arquivo SHA-256 acompanha o NRO.

Validação local inclui build ARM64 com -Werror, regressões C, concorrência
pthread e fixtures reais de mídia/HTTP. Não houve homologação física de todos
os provedores/codecs no Switch; detalhes e limites em
[relatório técnico](https://github.com/Tonsoaresmt/nplay-switch/blob/v0.12.62/docs/PLAYER_FIXES_0_12_62.md).

Bateria final completa aprovada, incluindo 55 cenários reais de mídia em seis
perfis e os testes novos de áudio/legenda/remux. NRO: 24.315.727 bytes.

SHA-256: `2899776a1ec2e1a620a0b971b4890a9312fcc06a2ab3dd0a089a25914ba48d31`.
