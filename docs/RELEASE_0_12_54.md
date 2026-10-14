# Nplay Switch 0.12.54

- Legendas: consulta as faixas externas mesmo quando existe uma legenda
  embutida em outro idioma. Reune faixas do master HLS, da sonda e do arquivo,
  preservando a origem e a escolha do usuario.
- Descoberta de legendas: novas tentativas em segundo plano apos falhas
  temporarias; X permite consultar novamente sem apagar a lista existente.
  Faixas descobertas depois recebem aviso para escolher com X.
- Retorno de HOME/hibernacao: recupera a tentativa com posicao, pausa e faixas
  preservadas nos caminhos suportados. Sessao R2 encerrada pode ser resolvida
  novamente; negativas de acesso e limites de telas continuam respeitados.
- Configuracoes > Aplicativo > Teste de internet: medicao opcional e cancelavel,
  com aproximadamente 9 MB de trafego, para comparar o comportamento da conexao
  em diferentes locais. O resultado auxilia o diagnostico e nao prova sozinho
  que um problema de reproducao seja causado pelo Wi-Fi.
- Entrada no player: progresso indisponivel nao e mais tratado como zero;
  falha ao obter a fonte permite tentar novamente ou voltar.

## Validacao

Build ARM64 completo com -Werror e bateria completa local, sem pular fixtures
de midia, aprovados. Inclui regressao de legendas, cancelamento/threads,
retomada, episodios, navegacao, HTTPS e simbolos do binario final.

Esta versao esta disponivel para teste no console. A validacao local nao
substitui teste fisico: o episodio especifico de Dragon Ball Super: Beerus e
o crash nativo relatado anteriormente ainda precisam de confirmacao/logs.
Fontes sequenciais sem busca e MKV TorBox com reinicio de remux ainda possuem
limitacoes de retomada. O carregamento de legenda hot selecionada conserva
a espera preexistente; o painel continua limitado a 16 faixas.

## Atualizacao

No aplicativo, abra Configuracoes > Buscar atualizacao. Para instalar
manualmente, baixe Nplay.nro e substitua somente esse arquivo na pasta switch
da microSD. Preserve configuracoes e demais arquivos; reabra o aplicativo.

Nplay.nro: 24.287.055 bytes.
SHA-256: `7e3a3e60460ca39389bc57dc197ed17eed8e84408a56ef1dd167215de09804fc`.
