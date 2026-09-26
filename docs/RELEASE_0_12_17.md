# Nplay Switch 0.12.17

O avatar do perfil aparece na barra superior e abre, por toque ou pelo botão
`-`, um menu com troca de perfil, configurações e saída da conta. A tela de
configurações agora separa perfil, reprodução, conta e aplicativo. Pelo perfil
é possível alterar nome, avatar, cor e modo infantil, além de criar, trocar ou
excluir outro perfil. Preferências de reprodução, senha e e-mail de recuperação
usam as mesmas rotas de conta do site. O diagnóstico permanece em Aplicativo.

O seletor de avatares mostra imagens compatíveis com o decodificador do NRO.
Avatares SVG continuam com a inicial colorida como fallback; a escolha de
imagens já existentes no catálogo é preservada entre aparelhos. Ao atualizar a
lista de perfis, o NRO mantém a anterior até a nova resposta ser validada.

Validação: compilação devkitA64, contratos do site e rotas, simulações de API,
episódios, player e remux. O comportamento visual no console depende de
conferência em hardware e não foi presumido a partir da compilação.
