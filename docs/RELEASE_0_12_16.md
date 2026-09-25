# Nplay Switch 0.12.16

O diagnóstico real da 0.12.15 registrou 72 intervalos sem quadro: 68 foram
atribuídos à espera de sincronização, 4 à leitura/rede; houve 34 leituras acima
de 250 ms, a pior de 5632 ms. O resumo de transporte mostrou zero requisições
de segmentos HLS nessa tentativa, compatível com uma fonte MP4 direta ou com
remux TorBox, mas não identifica sozinho qual delas foi usada.

O player agora ancora o vídeo num relógio monotônico, aceita apenas correções
pequenas da estimativa de áudio e inicia o áudio junto ao primeiro quadro. A
estimativa não pode mais deslocar o relógio por centenas de milissegundos em
cada quadro. Se uma leitura direta bloquear depois que o áudio em fila acabar,
o relógio para pelo tempo restante: o player retoma o próximo quadro sem pular
segundos da obra. Pausa e retomada reancoram o relógio. A linha de diagnóstico da
última etapa passa a indicar o tipo de entrega e se o fluxo é contínuo, sem
registrar URL ou token.

O HUD foi redesenhado a partir do player do site: vídeo em tela cheia, voltar
no canto superior, degradê inferior, linha do tempo roxa e uma única fileira de
controles. O painel antigo de cartões foi retirado. A tela aceita toque em
voltar, reproduzir/pausar, saltos de 10 segundos, linha do tempo, áudio,
legendas e painel. Fluxos TorBox contínuos continuam sem busca/retomada antes
de o R2 estar pronto.

Validação: compilação ARM64 limpa, simulação do relógio com timestamps de
áudio deslocados, contratos de 37 rotas, simulações TorBox/R2, continuação de
episódios e remux MKV→fMP4 chunked com vídeo e áudio. A compilação e as
simulações não substituem medição de fluidez no Switch real. Leituras diretas
de rede que fiquem sem dados ainda podem causar uma pausa; a correção remove
o atraso adicional criado pela sincronização do cliente.
