#include "connection_test_ui.h"
#include "connection_test.h"
#include "diag.h"
#include "ui.h"
#include "text.h"
#include <stdio.h>
#include <string.h>

typedef struct {
    const char *base;
    SDL_atomic_t cancel, done, phase, progress;
    ConnectionReport report;
} ConnectionJob;

static ConnectionSummary g_previous_connection;
static int g_previous_connection_valid;

static int connection_worker(void *opaque) {
    ConnectionJob *job = opaque;
    connection_test_run(job->base, &job->cancel, &job->phase, &job->progress, &job->report);
    SDL_AtomicSet(&job->done, 1);
    return 0;
}

static const SDL_Rect CONNECTION_BUTTON = {360, 606, 560, 48};
static int connection_button_hit(int x, int y) {
    return x >= CONNECTION_BUTTON.x && x < CONNECTION_BUTTON.x + CONNECTION_BUTTON.w &&
           y >= CONNECTION_BUTTON.y && y < CONNECTION_BUTTON.y + CONNECTION_BUTTON.h;
}

static int connection_action(const SDL_Event *event) {
    if (event->type == SDL_QUIT) return 3;
    if (event->type == SDL_JOYBUTTONDOWN) {
        if (event->jbutton.button == JOY_A) return 1;
        if (event->jbutton.button == JOY_B || event->jbutton.button == JOY_MINUS) return 2;
    }
    if (event->type == SDL_FINGERDOWN) {
        int x = (int)(event->tfinger.x * WIN_W), y = (int)(event->tfinger.y * WIN_H);
        if (ui_header_action_hit(x, y)) return 2;
        if (connection_button_hit(x, y)) return 1;
    }
    return 0;
}

static void connection_card(int x, int y, const char *label, const char *value,
                             const char *detail) {
    fill_rect(x, y, 498, 124, C_BAR);
    text_draw(gRen, label, x + 20, y + 12, C_MUT, 2);
    text_clip(value, x + 20, y + 34, C_TEXT, 3, 458);
    text_clip(detail, x + 20, y + 94, C_MUT, 2, 458);
}

static void connection_results(const ConnectionSummary *summary,
                                const ConnectionSummary *previous, int has_previous,
                                int failed_thread) {
    char value[64], detail[96];
    snprintf(value, sizeof(value), summary->download_ok ? "%.1f Mbps" : "Nao medido", summary->mbps);
    if (summary->partial_downloads)
        snprintf(detail, sizeof(detail), "%d/2 amostras | %d parcial(is)", summary->download_ok, summary->partial_downloads);
    else snprintf(detail, sizeof(detail), "%d/2 amostras de download", summary->download_ok);
    connection_card(112, 185, "DOWNLOAD MEDIDO", value, detail);
    if (summary->internet_ok) snprintf(value, sizeof(value), "%u ms", summary->internet_ms);
    else snprintf(value, sizeof(value), "Sem resposta");
    snprintf(detail, sizeof(detail), "%d/4 respostas HTTPS; nao e ping ICMP", summary->internet_ok);
    connection_card(670, 185, "RESPOSTA DA INTERNET", value, detail);
    if (summary->internet_ok > 1) snprintf(value, sizeof(value), "%u ms", summary->variation_ms);
    else snprintf(value, sizeof(value), "Nao medido");
    connection_card(112, 330, "VARIACAO DAS RESPOSTAS", value, "Diferenca entre a maior e a menor resposta");
    if (summary->service_ok) snprintf(value, sizeof(value), "%u ms", summary->service_ms);
    else snprintf(value, sizeof(value), "Sem resposta");
    snprintf(detail, sizeof(detail), "%d/3 respostas do Nplay", summary->service_ok);
    connection_card(670, 330, "RESPOSTA DO NPLAY", value, detail);
    const char *advice = failed_thread ? "Nao foi possivel iniciar o teste no aplicativo. Tente novamente." :
                         connection_test_advice(summary->assessment);
    text_clip(advice, 112, 479, summary->assessment == CONNECTION_OK ? C_GREEN : C_ACC2, 0, 1056);
    if (has_previous) {
        snprintf(detail, sizeof(detail), "Anterior: %.1f Mbps | Internet %u ms | variacao %u ms",
                 previous->mbps, previous->internet_ms, previous->variation_ms);
        text_clip(detail, 112, 518, C_MUT, 0, 1056);
    } else text_clip("Teste em outro comodo para comparar os resultados.", 112, 518, C_MUT, 0, 1056);
    text_clip("Uma medicao curta nao garante todos os videos: fonte, Wi-Fi e player tambem influem.",
              112, 560, C_MUT, 2, 1056);
}

void connection_test_show(SDL_Renderer *renderer, const char *base, int *running) {
    if (!renderer || !base || !running || !*running) return;
    ConnectionJob job = {.base=base};
    SDL_Thread *thread = NULL;
    ConnectionSummary summary = {0};
    ConnectionSummary previous = g_previous_connection;
    int has_previous = g_previous_connection_valid;
    int state = 0, closing = 0, failed_thread = 0;
    while (!closing || thread) {
        if (thread && SDL_AtomicGet(&job.done)) {
            SDL_WaitThread(thread, NULL);
            thread = NULL;
            if (!closing && !job.report.cancelled) {
                connection_test_summary(&job.report, &summary);
                state = 2;
                if (summary.download_ok == 2 && !job.report.downloads[0].partial && !job.report.downloads[1].partial) {
                    g_previous_connection = summary;
                    g_previous_connection_valid = 1;
                }
                for (int i = 0; i < 9; i++) {
                    const ConnectionSample *sample = i < 4 ? &job.report.internet[i] :
                        i < 7 ? &job.report.service[i - 4] : &job.report.downloads[i - 7];
                    if (!sample->http_code && !sample->transport_code) continue;
                    diag_network_event("TEST", i < 4 ? "/diagnostics/internet" :
                        i < 7 ? "/diagnostics/nplay" : "/diagnostics/download",
                        sample->transport_code ? -sample->transport_code : sample->http_code,
                        sample->response_ms + sample->body_ms, sample->bytes);
                }
            }
        }
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            int action = connection_action(&event);
            if (action == 3) { *running = 0; closing = 1; }
            if (action == 2 || (action == 1 && thread)) closing = 1;
            if (action == 1 && !thread && !closing) {
                // No background automatic test or charge: A/touch is consent.
                memset(&job, 0, sizeof(job)); job.base = base;
                previous = g_previous_connection;
                has_previous = g_previous_connection_valid;
                failed_thread = 0;
                thread = SDL_CreateThread(connection_worker, "connection-test", &job);
                if (!thread) { memset(&summary, 0, sizeof(summary)); failed_thread = 1; state = 2; }
                else state = 1;
            }
            if (closing) { SDL_AtomicSet(&job.cancel, 1); break; }
        }
        if (!*running) { closing = 1; SDL_AtomicSet(&job.cancel, 1); }
        if (closing && !thread) break;
        SDL_SetRenderDrawColor(renderer, C_BG.r, C_BG.g, C_BG.b, 255);
        SDL_RenderClear(renderer);
        ui_header("CONFIGURACOES", "Teste de internet", "B Voltar");
        ui_panel(96, 128, 1088, 468, C_ACC2);
        if (state == 0) {
            ui_popcorn_draw(renderer, 640, 154, 130);
            text_center_at("Como esta a conexao aqui?", 112, 1056, 308, C_TEXT, 1);
            text_center_at("Meça o download e compare outro comodo, perto do roteador.", 112, 1056, 360, C_MUT, 0);
            text_center_at("Baixa ate 9 MB por teste. Pause outros downloads para comparar.", 112, 1056, 411, C_MUT, 0);
            text_center_at("Consulta a Cloudflare e o Nplay. Sem enviar dados da sua conta.", 112, 1056, 467, C_MUT, 2);
            text_center_at("Esses servicos veem o IP da conexao. Nenhum video sera preparado.", 112, 1056, 497, C_MUT, 2);
        } else if (thread) {
            int phase = SDL_AtomicGet(&job.phase), progress = SDL_AtomicGet(&job.progress);
            ui_popcorn_draw(renderer, 640, 160, 148);
            text_center_at(closing ? "Cancelando o teste..." : phase == 1 ? "Medindo as respostas da internet..." :
                phase == 2 ? "Verificando a resposta do Nplay..." : "Medindo a velocidade de download...",
                112, 1056, 356, C_TEXT, 1);
            ui_progress(320, 430, 640, progress, C_ACC2);
            text_center_at("A tela continua funcionando. B ou toque em Cancelar para sair.", 112, 1056, 482, C_MUT, 0);
        } else connection_results(&summary, &previous, has_previous, failed_thread);
        fill_rect(CONNECTION_BUTTON.x, CONNECTION_BUTTON.y, CONNECTION_BUTTON.w, CONNECTION_BUTTON.h, C_ACC);
        text_center_at(thread ? "Cancelar teste" : state == 0 ? "A  Iniciar teste" : "A  Repetir teste",
                       CONNECTION_BUTTON.x, CONNECTION_BUTTON.w, CONNECTION_BUTTON.y + 10, C_BG, 0);
        ui_footer(thread ? "B Cancelar | Nao feche o app durante o encerramento" : "A Iniciar/repetir    B Voltar");
        SDL_RenderPresent(renderer);
        SDL_Delay(16);
    }
    // All stack-owned work is joined before return, including B/quit/late completion.
}
