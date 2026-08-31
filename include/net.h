// net.h - camada fina de HTTP sobre libcurl para o Meruem.
#pragma once
#include <stddef.h>
#include <curl/curl.h>

struct membuf {
    char  *data;   // sempre terminado em '\0' (ou NULL se vazio)
    size_t len;
    size_t cap;    // capacidade alocada; evita realloc a cada callback do curl
};

void membuf_free(struct membuf *m);

// Chamar uma vez no inicio / fim do programa.
int  net_init(void);
void net_exit(void);

// Aplica cache compartilhado e a cadeia CA embutida a qualquer easy handle.
// Deve ser chamado antes de curl_easy_perform, inclusive no AVIO do player.
void net_configure_curl(CURL *curl);
void net_configure_curl_isolated(CURL *curl);

// Faz uma requisicao HTTP.
//   url    : URL completa (https://...)
//   method : "GET" ou "POST"
//   body   : corpo JSON para POST (ou NULL)
//   bearer : token para o header Authorization: Bearer ... (ou NULL)
//   out    : recebe o corpo da resposta (liberar depois com membuf_free)
//   err    : se != NULL, recebe ponteiro p/ string estatica com a causa do erro
// Retorna o codigo HTTP (>= 100) em sucesso de transporte, ou negativo em falha.
long net_request(const char *url, const char *method,
                 const char *body, const char *bearer,
                 struct membuf *out, const char **err);

long net_request_timeout(const char *url, const char *method,
                         const char *body, const char *bearer,
                         struct membuf *out, const char **err,
                         long connect_timeout, long total_timeout);

// Baixa uma resposta HTTP direto para um arquivo no SD.
// Retorna codigo HTTP (>= 100) ou negativo em falha de transporte.
long net_download_file(const char *url, const char *bearer,
                       const char *path, const char **err);

long net_download_file_timeout(const char *url, const char *bearer,
                               const char *path, const char **err,
                               long connect_timeout, long total_timeout);

// Download com progresso. O callback retorna diferente de zero para cancelar.
typedef int (*net_progress_cb)(long long received, long long total, void *userdata);
long net_download_file_progress(const char *url, const char *bearer,
                                const char *path, const char **err,
                                net_progress_cb progress, void *userdata);

// Codifica `in` para uso seguro em URL (percent-encoding) escrevendo em `out`.
void net_urlencode(const char *in, char *out, size_t cap);
