#include "update.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "cJSON.h"
#include "net.h"

static void copy_text(char *dst, size_t cap, const char *src) {
    if (!dst || cap == 0) return;
    snprintf(dst, cap, "%s", src ? src : "");
}

static void copy_summary(char *dst, size_t cap, const char *src) {
    size_t j = 0;
    int last_space = 0;
    if (!dst || cap == 0) return;
    dst[0] = '\0';
    if (!src) return;
    for (size_t i = 0; src[i] && j + 1 < cap; i++) {
        unsigned char c = (unsigned char)src[i];
        if (c == '\r' || c == '\n' || c == '\t') c = ' ';
        if (c < 32) continue;
        if (c == ' ' && last_space) continue;
        dst[j++] = (char)c;
        last_space = (c == ' ');
    }
    while (j > 0 && dst[j - 1] == ' ') j--;
    dst[j] = '\0';
}

static int ends_with_nro(const char *name) {
    size_t len;
    const char *suffix = ".nro";
    int i;
    if (!name) return 0;
    len = strlen(name);
    if (len < 4) return 0;
    name += len - 4;
    for (i = 0; i < 4; i++) {
        if (tolower((unsigned char)name[i]) != tolower((unsigned char)suffix[i])) return 0;
    }
    return 1;
}

static int read_version_part(const char **pp) {
    int value = 0;
    const char *p = *pp;
    while (*p && !isdigit((unsigned char)*p)) p++;
    while (isdigit((unsigned char)*p)) {
        value = value * 10 + (*p - '0');
        p++;
    }
    while (*p == '.') p++;
    *pp = p;
    return value;
}

static int version_cmp(const char *a, const char *b) {
    const char *pa = a ? a : "";
    const char *pb = b ? b : "";
    int i;
    for (i = 0; i < 4; i++) {
        int va = read_version_part(&pa);
        int vb = read_version_part(&pb);
        if (va != vb) return (va > vb) ? 1 : -1;
        if (!*pa && !*pb) break;
    }
    return 0;
}

static int has_repo_config(void) {
    return UPDATE_REPO_OWNER[0] && UPDATE_REPO_NAME[0];
}

static int same_path(const char *a, const char *b) {
    if (!a || !b) return 0;
    return strcasecmp(a, b) == 0;
}

static int join_path(char *out, size_t cap, const char *dir, const char *name) {
    size_t a, b;
    if (!out || cap == 0 || !dir || !name) return -1;
    a = strlen(dir); b = strlen(name);
    if (a + 1 + b + 1 > cap) return -1;
    memcpy(out, dir, a);
    out[a] = '/';
    memcpy(out + a + 1, name, b + 1);
    return 0;
}

#define MAX_INSTALL_PATHS 16

static void add_install_path(char paths[][640], int *count, const char *path) {
    int i;
    if (!paths || !count || !path || !path[0]) return;
    for (i = 0; i < *count; i++) {
        if (same_path(paths[i], path)) return;
    }
    if (*count >= MAX_INSTALL_PATHS) return;
    snprintf(paths[*count], 640, "%s", path);
    (*count)++;
}

static int file_exists(const char *path) {
    FILE *f = path ? fopen(path, "rb") : NULL;
    if (!f) return 0;
    fclose(f);
    return 1;
}

static long long file_size(const char *path) {
    FILE *f = path ? fopen(path, "rb") : NULL;
    long n;
    if (!f) return -1;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return -1; }
    n = ftell(f);
    fclose(f);
    return n < 0 ? -1 : (long long)n;
}

static int is_nplay_nro(const char *name) {
    char lower[256];
    size_t i;
    if (!name || !ends_with_nro(name)) return 0;
    for (i = 0; name[i] && i + 1 < sizeof(lower); i++)
        lower[i] = (char)tolower((unsigned char)name[i]);
    lower[i] = '\0';
    return strstr(lower, "nplay") != NULL || strstr(lower, "meruem") != NULL;
}

// Procura o formato normal do hbmenu: sdmc:/switch/App/App.nro, alem de .nro
// solto em sdmc:/switch. Limita a um nivel para manter a verificacao rapida.
static void scan_install_paths(char paths[][640], int *count) {
    const char *root_path = "sdmc:/switch";
    DIR *root = opendir(root_path);
    struct dirent *ent;
    if (!root) return;
    while ((ent = readdir(root)) != NULL && *count < MAX_INSTALL_PATHS) {
        char path[640];
        if (!strcmp(ent->d_name, ".") || !strcmp(ent->d_name, "..")) continue;
        if (join_path(path, sizeof(path), root_path, ent->d_name) != 0) continue;
        if (is_nplay_nro(ent->d_name) && file_exists(path)) {
            add_install_path(paths, count, path);
            continue;
        }
        DIR *sub = opendir(path);
        if (sub) {
            struct dirent *child;
            while ((child = readdir(sub)) != NULL && *count < MAX_INSTALL_PATHS) {
                char child_path[640];
                if (!is_nplay_nro(child->d_name)) continue;
                if (join_path(child_path, sizeof(child_path), path, child->d_name) != 0) continue;
                if (file_exists(child_path)) add_install_path(paths, count, child_path);
            }
            closedir(sub);
        }
    }
    closedir(root);
}

static int copy_file(const char *src, const char *dst, char *err, size_t errcap) {
    FILE *in = NULL;
    FILE *out = NULL;
    unsigned char buf[256 * 1024];
    size_t n;

    in = fopen(src, "rb");
    if (!in) {
        if (err && errcap) snprintf(err, errcap, "Nao abri o update baixado (%d).", errno);
        return -1;
    }
    out = fopen(dst, "wb");
    if (!out) {
        if (err && errcap) snprintf(err, errcap, "Nao consegui gravar destino (%d).", errno);
        fclose(in);
        return -1;
    }
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) {
            if (err && errcap) snprintf(err, errcap, "Falha escrevendo destino (%d).", errno);
            fclose(out);
            fclose(in);
            return -1;
        }
    }
    if (ferror(in)) {
        if (err && errcap) snprintf(err, errcap, "Falha lendo o update baixado (%d).", errno);
        fclose(out);
        fclose(in);
        return -1;
    }
    fclose(out);
    fclose(in);
    return 0;
}

static int make_backup_path(const char *dst, char *bak, size_t cap) {
    size_t len;
    if (!dst || !bak || cap == 0) return -1;
    len = strlen(dst);
    if (len + 4 >= cap) return -1;
    memcpy(bak, dst, len);
    memcpy(bak + len, ".bak", 5);
    return 0;
}

static int replace_with_file(const char *src, const char *dst, char *err, size_t errcap) {
    char bak[640], staged[640];
    int renamed_old = 0;
    int activate_errno;
    long long expected;
    if (!dst || !dst[0]) {
        if (err && errcap) copy_text(err, errcap, "Caminho de destino vazio.");
        return -1;
    }
    if (make_backup_path(dst, bak, sizeof(bak)) != 0) {
        if (err && errcap) copy_text(err, errcap, "Caminho de destino grande demais.");
        return -1;
    }
    if (snprintf(staged, sizeof(staged), "%s.new", dst) >= (int)sizeof(staged)) {
        if (err && errcap) copy_text(err, errcap, "Caminho temporario grande demais.");
        return -1;
    }
    expected = file_size(src);
    if (expected <= 0) {
        if (err && errcap) copy_text(err, errcap, "Update baixado esta vazio.");
        return -1;
    }
    remove(staged);
    if (copy_file(src, staged, err, errcap) != 0) {
        remove(staged);
        return -1;
    }
    if (file_size(staged) != expected) {
        remove(staged);
        if (err && errcap) copy_text(err, errcap, "Copia gravada ficou incompleta.");
        return -1;
    }
    remove(bak);
    if (rename(dst, bak) == 0) {
        renamed_old = 1;
    } else if (errno != ENOENT) {
        if (err && errcap) snprintf(err, errcap, "Nao preparei destino (%d).", errno);
        remove(staged);
        return -1;
    }
    // A troca final e atomica no mesmo diretorio. O download original permanece
    // disponivel para atualizar outras copias encontradas no SD.
    if (rename(staged, dst) == 0) {
        if (renamed_old) remove(bak);
        return 0;
    }
    activate_errno = errno;
    remove(staged);
    if (renamed_old) rename(bak, dst);
    if (err && errcap) snprintf(err, errcap, "Nao ativei o novo arquivo (%d).", activate_errno);
    return -1;
}

void update_resolve_target_path(const char *argv0, char *out, size_t cap) {
    if (!out || cap == 0) return;
    out[0] = '\0';
    if (argv0 && argv0[0] && strstr(argv0, ".nro")) {
        copy_text(out, cap, argv0);
        return;
    }
    copy_text(out, cap, "sdmc:/switch/Nplay/Nplay.nro");
}

int update_check(struct update_info *info) {
    struct membuf resp = {0};
    char url[256];
    const char *net_err = NULL;
    long http_code = 0;
    cJSON *root = NULL;
    cJSON *assets = NULL;
    cJSON *asset = NULL;
    int result = UPDATE_CHECK_ERROR;

    if (info) memset(info, 0, sizeof(*info));
    if (!has_repo_config()) {
        if (info) copy_text(info->message, sizeof(info->message), "Repositorio de release ainda nao configurado.");
        return UPDATE_CHECK_DISABLED;
    }

    snprintf(url, sizeof(url), "https://api.github.com/repos/%s/%s/releases/latest",
             UPDATE_REPO_OWNER, UPDATE_REPO_NAME);
    http_code = net_request_timeout(url, "GET", NULL, NULL, &resp, &net_err, 4L, 7L);
    if (info) info->http_code = http_code;
    if (http_code != 200 || !resp.data) {
        if (info) {
            if (net_err && net_err[0]) {
                snprintf(info->message, sizeof(info->message), "GitHub falhou: %s (%ld).", net_err, http_code);
            } else {
                snprintf(info->message, sizeof(info->message), "GitHub respondeu HTTP %ld.", http_code);
            }
        }
        result = UPDATE_CHECK_ERROR;
        goto done;
    }

    root = cJSON_Parse(resp.data);
    if (!root) {
        if (info) copy_text(info->message, sizeof(info->message), "Resposta invalida do GitHub Releases.");
        result = UPDATE_CHECK_ERROR;
        goto done;
    }

    {
        cJSON *tag = cJSON_GetObjectItemCaseSensitive(root, "tag_name");
        if (!cJSON_IsString(tag) || !tag->valuestring || !tag->valuestring[0]) {
            if (info) copy_text(info->message, sizeof(info->message), "Release sem tag_name.");
            result = UPDATE_CHECK_ERROR;
            goto done;
        }
        if (info) copy_text(info->latest_version, sizeof(info->latest_version), tag->valuestring);
        {
            cJSON *body = cJSON_GetObjectItemCaseSensitive(root, "body");
            if (info && cJSON_IsString(body) && body->valuestring) {
                copy_summary(info->release_notes, sizeof(info->release_notes), body->valuestring);
            }
        }
        if (version_cmp(tag->valuestring, APP_VERSION_STR) <= 0) {
            if (info) snprintf(info->message, sizeof(info->message), "GitHub latest: %s.", tag->valuestring);
            result = UPDATE_CHECK_UP_TO_DATE;
            goto done;
        }
    }

    assets = cJSON_GetObjectItemCaseSensitive(root, "assets");
    cJSON_ArrayForEach(asset, assets) {
        cJSON *name = cJSON_GetObjectItemCaseSensitive(asset, "name");
        cJSON *dl = cJSON_GetObjectItemCaseSensitive(asset, "browser_download_url");
        if (!cJSON_IsString(name) || !name->valuestring) continue;
        if (!cJSON_IsString(dl) || !dl->valuestring) continue;
        if (!ends_with_nro(name->valuestring)) continue;
        if (info) {
            copy_text(info->asset_name, sizeof(info->asset_name), name->valuestring);
            copy_text(info->download_url, sizeof(info->download_url), dl->valuestring);
        }
        result = UPDATE_CHECK_AVAILABLE;
        goto done;
    }

    if (info) copy_text(info->message, sizeof(info->message), "Nenhum asset .nro encontrado na release.");
    result = UPDATE_CHECK_ERROR;

done:
    if (root) cJSON_Delete(root);
    membuf_free(&resp);
    return result;
}

int update_apply(const struct update_info *info, const char *target_path,
                 char *installed_path, size_t installed_cap,
                 char *err, size_t errcap) {
    char tmp[640];
    char paths[MAX_INSTALL_PATHS][640];
    char first_err[256];
    int path_count = 0;
    int ok_count = 0;
    int i;
    long code;

    if (err && errcap) err[0] = '\0';
    if (installed_path && installed_cap) installed_path[0] = '\0';
    first_err[0] = '\0';
    if (!info || !info->download_url[0]) {
        if (err && errcap) copy_text(err, errcap, "URL do asset nao disponivel.");
        return -1;
    }
    if (!target_path || !target_path[0]) {
        if (err && errcap) copy_text(err, errcap, "Caminho do .nro atual nao identificado.");
        return -1;
    }

    snprintf(tmp, sizeof(tmp), "sdmc:/switch/.nplay-update.download");
    remove(tmp);

    code = net_download_file(info->download_url, NULL, tmp, NULL);
    if (code != 200) {
        if (err && errcap) snprintf(err, errcap, "Download falhou (HTTP %ld).", code);
        remove(tmp);
        return -1;
    }

    // argv[0] nem sempre chega pelo hbmenu. So aceita o alvo se ele realmente
    // existe e procura copias conhecidas para nao anunciar sucesso no arquivo errado.
    if (file_exists(target_path)) add_install_path(paths, &path_count, target_path);
    scan_install_paths(paths, &path_count);
    if (path_count <= 0) {
        if (err && errcap) copy_text(err, errcap, "Nao localizei o Nplay.nro instalado no SD.");
        remove(tmp);
        return -1;
    }

    for (i = 0; i < path_count; i++) {
        char one_err[256] = {0};
        if (replace_with_file(tmp, paths[i], one_err, sizeof(one_err)) == 0) {
            if (ok_count == 0 && installed_path && installed_cap)
                copy_text(installed_path, installed_cap, paths[i]);
            ok_count++;
        } else if (!first_err[0]) {
            copy_text(first_err, sizeof(first_err), one_err);
        }
    }

    remove(tmp);
    if (ok_count <= 0) {
        if (err && errcap) copy_text(err, errcap, first_err[0] ? first_err : "Nao consegui gravar o novo .nro.");
        return -1;
    }
    if (ok_count < path_count && err && errcap) {
        snprintf(err, errcap, "Atualizei %d de %d copia(s): %s", ok_count, path_count,
                 first_err[0] ? first_err : "uma copia falhou");
    }
    return ok_count;
}
