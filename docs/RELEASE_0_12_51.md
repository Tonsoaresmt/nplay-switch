# Nplay Switch 0.12.51 — reparo da configuração de certificados

Correção focada no erro **“Problem with the SSL CA cert (path? access rights?)”**,
que pode bloquear categorias, pesquisa, capas e atualização ao mesmo tempo.

- Certificados do próprio NRO carregados em memória, sem depender do arquivo CA
  na microSD. **A verificação HTTPS não foi desligada.**
- Falhas de importação agora mostram código nativo seguro para diagnóstico.
- Melhorias de navegação/player da 0.12.50 preservadas; nenhuma alteração de
  backend, filtros ou dados da conta nesta versão.

Compilação ARM64 sem avisos e validação local completa aprovadas, incluindo
fixtures de mídia e regressões TLS/memória/concorrência. **Handshake TLS e
comportamento visual no Switch físico ainda precisam de confirmação.**

## Se o Switch não conseguir atualizar

O erro de certificados também pode impedir o download pelo app. Nesse caso:

1. Baixe **Nplay.nro** nos arquivos desta release pelo computador ou celular.
2. Acesse o microSD/arquivos do Switch e substitua o Nplay.nro instalado na
   pasta **switch** ou na subpasta onde você o colocou.
3. Preserve configurações, dados e perfis; substitua somente o arquivo NRO.
4. Feche/reabra o aplicativo e confira **0.12.51** em Configurações.

Teste as categorias e as capas. Se o erro persistir, envie foto da mensagem
inteira e o nplay-network-trace.log; esta release não presume que toda falha de
importação nativa foi resolvida sem testar no console.

[Diagnóstico, implementação e limites](https://github.com/Tonsoaresmt/nplay-switch/blob/v0.12.51/docs/TLS_CA_0_12_51.md).

Nplay.nro: **24.262.479 bytes**.
SHA-256: `61e0e60075dd1a811bb859939935ba06c09a55aadb17ac8d9c9e55a2888f9b53`.
