# Nplay para Nintendo Switch

Assista ao Nplay no Nintendo Switch pelo Homebrew Menu.

> [!IMPORTANT]
> Este aplicativo requer um Nintendo Switch com ambiente homebrew já configurado e acesso ao Homebrew Menu. Ele não é instalado nem executado em um console original sem homebrew.

## Instalação rápida

1. Abra a página da [versão mais recente](https://github.com/Tonsoaresmt/nplay-switch/releases/latest).
2. Em **Assets**, baixe o arquivo **`Nplay.nro`**. Não baixe o código-fonte (`Source code`).
3. Conecte o Switch ao computador/celular ou abra o cartão microSD em um leitor.
4. No cartão, abra a pasta **`switch`**. Caso ela não exista, crie uma pasta com esse nome.
5. Copie o arquivo para este caminho:

   ```text
   sdmc:/switch/Nplay.nro
   ```

6. Ejete o armazenamento com segurança, abra o **Homebrew Menu** no Switch e selecione **Nplay**.

Pronto: faça login ou use o QR Code mostrado pelo aplicativo para conectar sua conta.

## Atualizar o Nplay

Pelo Switch, abra **Configurações** no Nplay e escolha **Buscar atualização**. Ao terminar, reinicie o aplicativo quando ele solicitar.

Se a atualização automática falhar, faça a atualização manual:

1. Baixe novamente o `Nplay.nro` na [versão mais recente](https://github.com/Tonsoaresmt/nplay-switch/releases/latest).
2. Copie-o para `sdmc:/switch/Nplay.nro`.
3. Quando o sistema perguntar, escolha **substituir** o arquivo anterior.
4. Feche e abra o Nplay novamente pelo Homebrew Menu.

## Dicas se não aparecer no Homebrew Menu

- Confirme que o nome do arquivo é exatamente `Nplay.nro`, e não `Nplay.nro.nro` ou `Nplay.nro.zip`.
- Confirme que ele está dentro da pasta `switch` do cartão, e não dentro da pasta Downloads do dispositivo.
- Extraia o arquivo se o navegador tiver baixado um `.zip`; o Homebrew Menu precisa do arquivo `.nro`.
- Se você possui mais de uma cópia do Nplay no cartão, mantenha apenas a que pretende abrir ou atualize todas para evitar abrir uma versão antiga sem perceber.

## Onde baixar com segurança

Use sempre a página oficial de [Releases do Nplay Switch](https://github.com/Tonsoaresmt/nplay-switch/releases). Cada versão publica o arquivo `Nplay.nro` e seu checksum SHA-256 para conferência opcional.

## Suporte

Ao relatar um problema, informe a versão mostrada em **Configurações**, o título que tentou abrir e, se possível, uma foto de **Configurações → Diagnóstico do player**. Não publique token, QR Code de login ou URLs de reprodução.

---

Desenvolvido para a comunidade homebrew. Nintendo Switch é uma marca da Nintendo; este projeto não é afiliado nem endossado pela Nintendo.
