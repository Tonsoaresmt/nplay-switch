# Nplay Switch 0.12.57 — controles e estabilidade do player

## Controles

- L/R avancam ou voltam 10 segundos; ZL/ZR, 1 minuto. Segurar acumula o destino sem
  recarregar a cada passo; soltar aplica um unico salto. B cancela.
- Configuracoes > Reproducao > Analogico direito: minutagem. Desligado
  por padrao, salvo por perfil neste Switch. Segure para escolher o tempo;
  A confirma e B cancela. Soltar o analogico nao aplica o salto.
- Uma busca pendente nao confirma menus de faixas ou episodios.

## Estabilidade e imagem

- Protege o upload quando a resolucao do quadro muda: planos menores nao
  sao enviados diretamente para uma textura maior. A conversao conserva
  o tamanho reservado, sem crescimento a cada mudanca de resolucao.
- Fallback de NV12 para IYUV e reaproveitado; renderer sem NV12 deixa de
  tentar criar e destruir texturas a cada quadro.
- Proporcao considera pixels nao quadrados e o tamanho atual do quadro.
  Letreiros acompanham o retangulo do video. Erros de conversao sao tratados.
- Busca em clipes com duracao conhecida de ate 1 segundo nao salta para fora deles.
- Inclui as melhorias de busca e retomada em TorBox, R2 e arquivos com Range.
- TorBox com suporte explicito a restart usa o plano do servidor para
  saltar/retomar; progresso e legendas acompanham o trecho. Heartbeat
  considera o tempo real gasto em rede e reconfirma pedidos renovados.

## Validacao e limites

Validacao local completa aprovada: ARM64 `-Werror`, 200 mil passos simulados,
1.152 transicoes de upload instrumentadas e 55 cenarios de midia via
FFmpeg/HTTP local (360p a 1080p60, retrato, audio mono/stereo/5.1).
Inclui testes de pausa/retomada/faixas/legendas/episodios/TLS e falhas de rede.

Nao e homologacao fisica: CLI usa FFmpeg 8.1.1, NRO 7.1; GPU/NVTEGRA, rede
domestica, producao TorBox/R2 e sessao longa precisam de teste no console.
Normalizar resolucao variavel tem custo de CPU a medir. Fonte sequencial
legada sem contrato de busca continua sem suporte a saltos.

## Atualizacao

No Nplay, abra Configuracoes > Aplicativo > Buscar atualizacao. Para instalar
manualmente, baixe Nplay.nro e substitua somente esse arquivo na pasta switch
da microSD. Preserve configuracoes e demais arquivos; reabra o aplicativo.

Nplay.nro: 24.291.151 bytes. SHA-256:
`85ade00f99127a22600fbdfd8e629d3df426629dc4af93d996dd264779e63e23`.

Release para teste no console; a aprovacao local nao garante ausencia de
regressoes no aparelho. Relatorio:
`docs/PLAYER_SCENARIO_MATRIX_0_12_57.md`.
