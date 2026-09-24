# Nplay Switch 0.11.0

Esta versão inicia a reconstrução nativa do Nplay no Switch e troca o ícone do
hbmenu pelo símbolo atual do Nplay (o mesmo usado no site). A interface segue
SDL2/FFmpeg/libcurl, com foco no D-pad e sem incorporar a página web no NRO.

- Busca e detalhes saíram da thread de desenho; consultas são limitadas e podem
  ser canceladas com B.
- Perfil ativo passou a ser escolhido no Switch, validado no login e enviado
  apenas às chamadas autenticadas da API. Listas locais são separadas por perfil.
- Home e busca reduzem varreduras por quadro e recortam cards entre topo/rodapé.
  Configurações > X mostra contagem de quadros de UI acima de 20/33 ms.
- A atualização verifica tamanho, SHA-256 do GitHub Releases e cabeçalho NRO
  antes de substituir o arquivo instalado.

Validação local: build ARM64 limpo, contrato site/Switch, busca e fluxo de
reprodução com banco temporário, `git diff --check`, hash do NRO e presença
exata do novo JPEG 256×256 dentro do pacote. O NRO público 0.10.1 foi baixado,
comparado ao digest oficial e contém o atualizador de GitHub Releases.

Ainda é necessário confirmar no Switch real a atualização iniciada pela versão
instalada, a navegação portátil/dock, troca de perfil e filme/episódio por 30
minutos. Se a versão instalada for anterior ao conserto do atualizador, a
primeira troca de NRO pode exigir instalação manual. O roteiro está em
`docs/SWITCH_0_11_HARDWARE_CHECK.md`.
