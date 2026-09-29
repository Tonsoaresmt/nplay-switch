# Nplay Switch 0.12.24

## Correcao da selecao inicial de audio

- Corrigida a ordem que permitia uma pista inglesa herdada do episodio anterior
  vencer uma faixa `pt-BR` explicita com o perfil configurado como Dublado.
- Abertura de filme/episodio agora aplica primeiro a preferencia Dublado ou
  Legendado. A continuidade so entra como fallback quando a fonte nao oferece o
  idioma esperado.
- Recuperacao da mesma reproducao continua preservando a faixa realmente ativa,
  inclusive depois de uma troca manual antes de refresh ou fallback de fonte.
- A regra de pacotes antigos com duas faixas `eng + und` continua escolhendo a
  segunda faixa inferida como dublagem.
- `Tanto faz` continua respeitando continuidade e preferencia local do perfil.
- O trace `streams selected` registra preferencia, pista recebida, prioridade e
  mapa compacto das faixas, por exemplo `1:en*,2:pt`.

## Verificacao local

- Suite limpa completa concluida sem erros nem avisos.
- Política testada para PT-BR explicito, pacote legado, anime PT/JPN, comentarios,
  faixas fora de ordem, episodio seguinte e recuperacao da mesma reproducao.
- Contratos site/Switch, API, relogio, legendas, episodios, remux e simbolos
  obrigatorios tambem aprovados.
- Artefato: `Nplay.nro`, 24.151.887 bytes,
  SHA-256 `2405f48c4477dd3aa95cbf87793f3fa996bfe10c55ed5c743995baabdeaff58b`.

## Teste necessario no Switch

- Perfil em Dublado: abrir filme com ingles e PT-BR; deve começar em português.
- Abrir dois episodios em sequencia depois de ter usado ingles anteriormente.
- Trocar manualmente a faixa, interromper brevemente a rede e confirmar que a
  recuperacao da mesma reproducao conserva a escolha.
- Se ainda abrir em ingles, fotografar o painel de faixas e o diagnostico; o mapa
  novo mostrara se o manifesto realmente entregou alguma faixa identificada `pt`.
