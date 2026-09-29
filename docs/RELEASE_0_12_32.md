# Nplay Switch 0.12.32

## Perfis corrigidos

- O catálogo de avatares passa a carregar junto da tela de perfis, de forma
  assíncrona. Antes ele só era solicitado depois da entrada na Home.
- Avatares de personagens, imagens enviadas e DiceBear são resolvidos no NRO.
  DiceBear usa PNG de 256 px porque SDL_image não decodifica o SVG do site.
- Falhas transitórias recebem duas novas tentativas sem travar os controles.
- O fallback por inicial usa contraste automático e nunca fica branco sobre branco.

## Revisão visual e gráfica

- Retratos circulares com crop central e proporção preservada.
- Perfil selecionado: 184 px, aro azul e foco evidente.
- Demais perfis: 164 px, sem os antigos cartões quadrados dominantes.
- Seletor de avatar: dez retratos de 130 px por página em vez de 18 imagens de 84 px.
- Topbar, menu rápido, configurações e editor usam o mesmo tratamento circular.
- Recorte executado pela GPU com `SDL_RenderGeometry`; nenhuma textura temporária,
  superfície ou máscara é criada durante os quadros.
- Lookup de chave do avatar em hash fixo, integrado ao cache/LRU existente.

## Validação

- Build Nintendo Switch ARM64 com warnings tratados como erro.
- `tools/validate_release.ps1` completo.
- Revisão matemática de limites para 1–4 perfis e duas linhas do picker em 1280x720.
- Artefato: `Nplay.nro`, 24.172.367 bytes, SHA-256
  `8440ff8d4c545e996ed8d68ff0bdc9e4c6a2978e5ffbfb6aa704b9941844649b`.
- Pendente físico: validar formatos reais da conta, overscan e contraste no console.
