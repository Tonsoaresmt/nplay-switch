# Compatibilidade site/NRO — 13/10/2026

## Bases verificadas

- NRO: branch codex/switch-player-hardening, base717f33f/.52 publicada.
- Site local C:/iptv: HEAD423b19ab; TorBox/remux7693c48f, legendas24540282,
  sessoes1e45df0a/299f8182, buscaecf9bd8f, planos9e50b50b.
- Quatro alteracoes locais do usuario no site (migrate, series-pack-health-test,
  series-health-sync, remote-register) foram preservadas. Nenhuma escrita em
  C:/iptv, banco de producao, provedor TorBox real ou deploy nesta rodada.
- API publica health identificou producao c9d0f77b. Esse objeto existe no git
  local; snapshot isolado de src/scripts/docs/public-js em build/site-c9d0f77.
  Ultima validacao compara esse snapshot, NAO so o checkout423b. Objetos locais
  posteriores4639980c mudam scripts de sinopse, sem diferenca src/public-js.
  Comparacao inicial GitHub por SHA423..c9 retornou404; repo e acessivel, sem
  bypass de permissao. Objetos locais permitiram comparar os snapshots exatos.
- Metadata git do site se apresenta como bare; status somente leitura foi
  obtido com override core.bare=false/work-tree explicito, sem mudar config.

## Incompatibilidades corrigidas no cliente

1. **MP4 TorBox:** .52 forcava sequential_stream em qualquer URL /media/debrid,
   mesmo com seekable=true/seek_mode=range. .53 respeita o contrato; MKV com
   restart permanece sequencial. Resposta legada com nome MP4 tambem funciona;
   resposta sem metadados fica conservadora. Nao finge suporte a seek de MKV.
2. **Renovacao Range:** MP4 TorBox tinha renew_cb=NULL. Agora solicita de novo
   a mesma fonte pela rota hot, preservando checkpoint e contexto. Nao aceita
   remux sem busca nem outra fonte silenciosa; R2-ready exige descriptor R2.
   Falha nao altera a saida. Supervisor conserva posicao/limites de retry.
3. **Pedidos:** request_id numerico e SID textual preservados separadamente.
   /stream/hot/request/:id/started recebe a confirmacao depois de um quadro
   realmente apresentado, na thread auxiliar existente. Max3tentativas, waits
   2/4s, timeout5s, cancel proprio; nunca I/O na thread de render. Sucesso envia
   uma vez por reproducao, nao a cada seek. Endpoint e quem aplica as regras
   de contagem/promocao do site; nao alteramos limites nem criamos lote R2.
4. **Anime/contexto:** troca para hot conserva kind/section/T/E, nao a sessao
   numerica ja fechada nem URL antiga. file_size aceita tamanho64bits. IDs
   invalidos/fracionarios rejeitados; uma obra diferente nao herda contexto.
5. **Embed novo:** HTML nao vai para FFmpeg. O NRO tenta o hot existente com
   fonte torrent validada pelo servidor, sem dizer torrent_capable=true nem
   usar motor P2P inexistente. Se nao houver alternativa/plano, falha segura.
6. **Erros antigos:** sucesso de uma nova tentativa hot limpa a flag expired
   anterior no mesmo worker; nao causa logout por erro antigo.
7. **HLS TorBox:** resposta container=hls normalizada para m3u8, em vez de
   forcar MP4. Nao pedimos delivery=hls automaticamente: esse novo fluxo EVENT
   tem limites de disco/CPU, espera ate90/150s e precisa homologacao especifica.
8. **Vitrine atual:** novas prateleiras newSeasons/topRated de anime, popular
   chamado Em alta; selo Pronto removido de posters/hero. Priorizacao R2 mantida,
   rail antiga renomeada Selecionados para voce. Campos ausentes nao criam rail.

## Areas que continuam compativeis sem alteracao

- R2 continua no resolver normal; anime direto usa animesdrive/hinatasoul.
  Producao agora prioriza DarkMahou verificado antes do embed (R2 ainda acima);
  por isso contexto e integracao TorBox dos episodios sao especialmente importantes.
- search-v2 ja e usado pelo NRO: tolerancia de texto/numero fica no servidor.
- Conta/plano/preferencias usam API; nomes/limites sao dados, nao tabelas fixas.
- O backend atual isola progresso por sessao/aparelho; fallback de cliente
  sem session_id numerico ja e limitado ao aparelho. SID hot NUNCA vira inteiro.
- Curadoria/identidade/packs sao responsabilidade do servidor: nao reativamos
  fontes, nao alteramos mappings nem migramos dados pelo console.

## Lacunas reais ainda abertas

- TorBox MKV seek_mode=restart exige /seek?t, ?t=, origem temporal do keyframe
  e legenda ?from=. NRO nao implementou essa combinacao. Seek/retomada/recovery
  desse remux continuam restritos; nao liberar timeline apenas por existir duracao.
- Pagina de pedidos/notificacoes do site nao tem equivalente completo no NRO.
  Esta rodada corrige integracao do pedido de Play, nao cria nova central visual.
- Mudancas de planos do site nao representam checkout/pagamento nativo no NRO.
- Nenhum handshake/playback TorBox real no Switch, sessao longa ou GPU testado.
- HLS EVENT TorBox gerado pela VPS nao equivale a VOD R2 imutavel: reconhecer
  o descriptor nao comprova duracao final, reload de playlist ou seek no futuro.
  Nao anunciar paridade completa com player web/TV.

## Testes

- Site: debrid-resolver-test49checks, anime-source-fallback-test e
  debrid-subtitles-test passaram. Banco tmp isolado, chamadas TorBox simuladas,
  rotas/fetch/cancel fixtures; nao prova de disponibilidade de titulo em producao.
  Estes tres testes executam o checkout423b+alteracoes locais. Separadamente,
  test_site_compatibility extrai a funcao REAL de descriptor do commit implantado
  c9 e valida MP4/MKV/HLS/IDs com provider/jobs simulados, sem import de servicos.
- NRO: contrato API real em C com JSON/transporte fake: MP4/MKV/capabilities,
  request/SIDs, arquivos10GiB, contexto, renovacao/R2, 401/503/cancel/rejeicoes.
- Worker real extraido com SDL/tempo simulados: nada antes de quadro, sucesso
  uma vez, retries limitados, cancelamento e snapshot imutavel da sessao.
- Supervisor/demux/episodios/rewatch/subtitulos e bateria completa com fixtures
  de midia e simbolos passaram. Build ARM64 completo -Werror + rebuild final.

Artefato LOCAL .53:24270671B; SHA256
8d877c7463f58649454353899535b94acb5e0afc087bdf136684019614cba5b7.
Primeira bateria completa passou no candidato anterior c0aacb60...; bateria
FINAL contra c9d0f77 e com HLS/vitrine PASSOU, sem SkipMediaFixtures, no hash
8d877c74... acima. Sidecar final renovado. Nao confundir os hashes.
Publicacao posteriormente autorizada e concluida: v0.12.53 latest publica,
13/10/2026 11:16:41UTC (08:16:41 Sao Paulo). Tag em fa29bf0805499deb0caa4fdc2d1c31b89813a167;
API latest e download publico sem autenticacao conferidos, tamanho/SHA identicos
ao artefato FINAL acima. Nenhum deploy/backend/R2 alterado.
https://github.com/Tonsoaresmt/nplay-switch/releases/tag/v0.12.53

Checklist fisico ainda pendente:
TorBoxMP4 seek/retomada/queda, MKV inicio/legenda (sem anunciar seek), anime
R2/direto/embed com alternativa e pedido sem quadro/cancelado.
