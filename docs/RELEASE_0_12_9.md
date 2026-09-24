# Nplay Switch 0.12.9

O relato no console separou os caminhos: um vídeo iniciado do zero reproduziu
sem pausas, enquanto a retomada podia permanecer em “Preparando vídeo”. A
retomada HLS faz um seek antes do primeiro quadro; a 0.12.9 limita a espera
desse seek e, se ele não entregar vídeo, reabre a mesma fonte desde o início.
O progresso salvo não é apagado por uma tentativa que não exibiu nenhum quadro.

O diagnóstico registra `resume-from-start` quando essa recuperação acontece.
Um fim de HLS sem primeiro quadro depois do seek agora é tratado como falha de
retomada, não como conclusão natural do episódio.

Validação local: compilação ARM64, contrato do NRO/site e `git diff --check`.
A execução no hardware Switch não está disponível neste ambiente; não afirmar
que a retomada foi comprovada no console por esses testes.
