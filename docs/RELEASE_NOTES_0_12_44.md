# Nplay Switch 0.12.44

## Próximo episódio mais acessível

- Pause o vídeo com **A**: o cartão **Próximo episódio** fica visível quando há
  continuação conhecida. **Direita** seleciona, **A** inicia; **B** ou **Esquerda**
  cancela a seleção. Não precisa aguardar o fim dos créditos.
- No touch, toque no cartão para selecionar e novamente para confirmar.
- Corrigida a perda da continuação ao abrir por Home/Histórico, preparar vídeo
  e passar de temporada. Pedido manual funciona com autoplay desligado.

## Diagnóstico de animes

O trace distingue a entrega R2 e o provedor informado pela API, sem gravar URLs
assinadas ou credenciais. O NRO respeita a fonte R2 escolhida pelo backend.

**Esta versão não afirma corrigir as legendas ausentes de todos os animes.**
Estamos investigando a diferença de faixas entre navegador e Switch em
Super no Ura de Yani Suu Futari; o pacote/episódio específico ainda precisa ser
comparado. Conteúdo já tocando por outra origem não migra automaticamente para
R2 quando o preparo termina. Fechar e reabrir solicita novamente a fonte.

Build ARM64 sem avisos e validação local completa passaram, incluindo fixtures
HLS/WebVTT/remux, concorrência, rede, seek e fluxo de episódios. Teste físico no
Switch continua necessário.

## Instalar / atualizar

Baixe **Nplay.nro** abaixo e coloque em **/switch/Nplay.nro** na microSD,
substituindo a cópia instalada. Também pode atualizar pelas Configurações do
Nplay. Use hbmenu em modo aplicativo, não pelo Álbum.

Nplay.nro: 24.205.135 bytes.

SHA-256: `9321a245ef44b7a679781480a08b51c8978c28eb6246e57f201651aef046de55`
