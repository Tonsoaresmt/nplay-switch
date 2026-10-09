# Nplay Switch 0.12.52

## Correcoes

- Reduz falsas reconexoes quando o buffer de video esta cheio: a espera local
  entre callbacks de rede nao e mais tratada como conexao interrompida.
- Preserva a correcao HTTPS da 0.12.51.1: importacao de certificados em
  estagios, tamanho exato sem NUL enviado ao servico SSL, verificacao de
  certificado/hostname/data mantida. Nao apaga contas nem configuracoes.
- Mostra a versao antes do login, inclusive durante pareamento por QR.
- Validador recusa artefato de versao diferente ou anterior ao codigo-fonte.

## Validacao e limites

Build ARM64 -Werror, bateria completa com fixtures de midia e simbolos ELF,
regressoes de pausa/seek/checkpoint/legendas e libcurl real com servidor local.
Comparacao A/B reproduziu abort indevido no codigo anterior; corrigido conclui
a mesma transferencia com buffer cheio e atraso de 9,5 s. Idle real continua
abortando. TLS Horizon nos testes automatizados e simulado.

Disponibilizada por autorizacao do usuario para TESTE no Switch fisico.
Ainda nao comprovada sessao longa >60 min, GPU ou Wi-Fi do console. Nao e
promessa de eliminar todas as reconexoes. Relatorio detalhado:
docs/RECONNECT_BACKPRESSURE_2026_10_08.md.

## Atualizacao

Use Configuracoes > Buscar atualizacao. Se o certificado da versao instalada
impedir o download, substitua somente Nplay.nro na pasta switch da microSD
pelo asset desta release. Preserve os demais arquivos e configuracoes.

Nplay.nro: 24266575 bytes.
SHA-256: d908325c24d3dcb74a8fcb1e4bb5a3ab39282bf62e069a34086337f391e8a749
