// net.h - camada fina de HTTP sobre libcurl para o Meruem.
#pragma once
#include <stddef.h>

struct membuf {
    char  *data;   // sempre terminado em '\0' (ou NULL se vazio)
    size_t len;
};

void membuf_free(struct membuf *m);

// Chamar uma vez no inicio / fim do programa.
int  net_init(void);
void net_exit(void);

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

// Codifica `in` para uso seguro em URL (percent-encoding) escrevendo em `out`.
void net_urlencode(const char *in, char *out, size_t cap);
