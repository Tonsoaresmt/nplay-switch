# Nplay Switch 0.12.41 — estabilidade durante reprodução

- Corrigida a leitura de segmentos HLS que podia deixar o vídeo preso depois
  de um atraso breve de rede, mesmo quando os dados voltavam a chegar.
- Uma conexão sem receber nenhum byte tenta uma nova conexão após oito segundos,
  sem descartar bytes parciais ou interromper transferências normais.
- Mantidos controles responsivos, recuperação de sessão e validação TLS.

Build ARM64 e suíte local completa passaram, incluindo reprodução HLS multifaixa,
seek, legendas, remux e simulações de espera/cancelamento. Teste prolongado no Switch
físico ainda necessário; conexão lenta ainda pode causar buffering.

Baixe `Nplay.nro` e coloque em `switch` na microSD, substituindo a cópia instalada.
Quem já tem o app pode usar **Configurações → Buscar atualização** e reiniciar.

SHA-256: `afea68abe5dd91d8734eb3a474cff0c812ef05b88e64b57c647fd2cfa0c2fc5b`
