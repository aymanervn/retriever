#define _POSIX_C_SOURCE 200809L
#include "svc.h"

#define DEFAULT_MAX 100000

static void emit_str(emit_fn e, void *c, const char *s)
{
    e(c, s, strlen(s));
}

void ipc_handle_line(index_t *ix, char *line, emit_fn emit, void *ctx)
{
    size_t L = strlen(line);
    while (L && (line[L - 1] == '\n' || line[L - 1] == '\r'))
        line[--L] = 0;
    char buf[RTV_MAX_PATH + 32];

    if (!strncmp(line, "S ", 2))
    {
        char mode[32];
        unsigned long maxn = 0;
        int off = 0;
        if (sscanf(line + 2, "%31s %lu %n", mode, &maxn, &off) < 2)
        {
            emit_str(emit, ctx, "! bad request\n.\n");
            return;
        }
        const char *q = line + 2 + off;
        hits_t h = {0};
        int64_t t0 = plat_usec();
        plat_rdlock(ix->lock);
        int rc = search_run(ix, mode, q, maxn ? maxn : DEFAULT_MAX, &h);
        if (rc < 0)
        {
            plat_rdunlock(ix->lock);
            free(h.v);
            if (rc == -2)
                snprintf(buf, sizeof buf, "! bad pattern: %s\n.\n", re_valid(q));
            else
                snprintf(buf, sizeof buf, "! unknown mode '%s' (have: %s)\n.\n", mode,
                         search_modes());
            emit_str(emit, ctx, buf);
            return;
        }
        int64_t t1 = plat_usec();
        snprintf(buf, sizeof buf, "* %zu hits in %lld us\n", h.n, (long long)(t1 - t0));
        emit_str(emit, ctx, buf);
        for (size_t i = 0; i < h.n; i++)
        {
            size_t pl = idx_path(ix, h.v[i], buf, RTV_MAX_PATH);
            buf[pl] = '\n';
            emit(ctx, buf, pl + 1);
        }
        plat_rdunlock(ix->lock);
        emit_str(emit, ctx, ".\n");
        free(h.v);
    }
    else if (!strcmp(line, "STATS"))
    {
        plat_rdlock(ix->lock);
        snprintf(buf, sizeof buf, "* files=%zu dirs=%zu vols=%d records=%zu mem_bytes=%zu\n.\n",
                 ix->nfiles, ix->ndirs, ix->nvols, ix->nrecs, idx_mem(ix));
        plat_rdunlock(ix->lock);
        emit_str(emit, ctx, buf);
    }
    else if (!strcmp(line, "RESCAN"))
    {
        int64_t t0 = plat_usec();
        int n = svc_rescan_all(ix);
        snprintf(buf, sizeof buf, "* rescanned %d volumes in %lld us\n.\n", n,
                 (long long)(plat_usec() - t0));
        emit_str(emit, ctx, buf);
    }
    else if (!strcmp(line, "PING"))
    {
        emit_str(emit, ctx, "* pong " RTV_VERSION "\n.\n");
    }
    else if (L)
    {
        emit_str(emit, ctx, "! unknown command\n.\n");
    }
}

#ifdef _WIN32
/* windows.h must precede sddl.h */
#include <windows.h>
#include <sddl.h>

int ipc_endpoint_busy(void)
{
    HANDLE m = CreateMutexA(NULL, TRUE, "Global\\retriever_service");
    if (!m)
        return GetLastError() == ERROR_ACCESS_DENIED;
    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        CloseHandle(m);
        return 1;
    }
    return 0;
}

static void pipe_emit(void *ctx, const char *b, size_t n)
{
    HANDLE h = (HANDLE)ctx;
    DWORD w;
    while (n)
    {
        if (!WriteFile(h, b, (DWORD)n, &w, NULL))
            return;
        b += w;
        n -= w;
    }
}

typedef struct
{
    index_t *ix;
    HANDLE p;
} wcl_t;

static void win_client_fn(void *arg)
{
    wcl_t c = *(wcl_t *)arg;
    free(arg);
    char acc[4096];
    int al = 0;
    char rb[2048];
    DWORD rd;
    while (ReadFile(c.p, rb, sizeof rb, &rd, NULL) && rd)
    {
        for (DWORD i = 0; i < rd; i++)
        {
            if (rb[i] == '\n')
            {
                acc[al] = 0;
                ipc_handle_line(c.ix, acc, pipe_emit, c.p);
                al = 0;
            }
            else if (al < (int)sizeof acc - 1)
            {
                acc[al++] = rb[i];
            }
        }
    }
    FlushFileBuffers(c.p);
    DisconnectNamedPipe(c.p);
    CloseHandle(c.p);
}

int ipc_serve(index_t *ix)
{
    SECURITY_ATTRIBUTES sa = {sizeof sa, NULL, FALSE};
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorA(
            "D:(A;;GA;;;SY)(A;;GA;;;BA)(A;;GA;;;OW)(A;;0x12019B;;;AU)", SDDL_REVISION_1,
            &sa.lpSecurityDescriptor, NULL))
    {
        fprintf(stderr,
                "retriever: failed to build pipe security descriptor "
                "(err %lu)\n",
                GetLastError());
        return 1;
    }
    fprintf(stderr, "retriever: serving on %s\n", RTV_PIPE_NAME);
    for (;;)
    {
        HANDLE p = CreateNamedPipeA(RTV_PIPE_NAME, PIPE_ACCESS_DUPLEX,
                                    PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
                                    PIPE_UNLIMITED_INSTANCES, 1 << 16, 1 << 16, 0, &sa);
        if (p == INVALID_HANDLE_VALUE)
        {
            fprintf(stderr, "retriever: CreateNamedPipe failed (err %lu)\n", GetLastError());
            return 1;
        }
        BOOL ok = ConnectNamedPipe(p, NULL) || GetLastError() == ERROR_PIPE_CONNECTED;
        if (!ok)
        {
            CloseHandle(p);
            continue;
        }
        wcl_t *c = malloc(sizeof *c);
        c->ix = ix;
        c->p = p;
        if (plat_thread(win_client_fn, c))
        {
            free(c);
            DisconnectNamedPipe(p);
            CloseHandle(p);
        }
    }
}

#else
#include <signal.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

int ipc_endpoint_busy(void)
{
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
        return 0;
    struct sockaddr_un a;
    memset(&a, 0, sizeof a);
    a.sun_family = AF_UNIX;
    strncpy(a.sun_path, RTV_SOCK_PATH, sizeof a.sun_path - 1);
    int busy = connect(fd, (struct sockaddr *)&a, sizeof a) == 0;
    close(fd);
    return busy;
}

static void sock_emit(void *ctx, const char *b, size_t n)
{
    int fd = *(int *)ctx;
    while (n)
    {
        ssize_t w = write(fd, b, n);
        if (w <= 0)
            return;
        b += w;
        n -= (size_t)w;
    }
}

typedef struct
{
    index_t *ix;
    int fd;
} pcl_t;

static void posix_client_fn(void *arg)
{
    pcl_t c = *(pcl_t *)arg;
    free(arg);
    FILE *fr = fdopen(c.fd, "r");
    if (!fr)
    {
        close(c.fd);
        return;
    }
    char line[4096];
    while (fgets(line, sizeof line, fr))
        ipc_handle_line(c.ix, line, sock_emit, &c.fd);
    fclose(fr);
}

int ipc_serve(index_t *ix)
{
    signal(SIGPIPE, SIG_IGN);
    unlink(RTV_SOCK_PATH);
    int s = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un a;
    memset(&a, 0, sizeof a);
    a.sun_family = AF_UNIX;
    strncpy(a.sun_path, RTV_SOCK_PATH, sizeof a.sun_path - 1);
    if (bind(s, (struct sockaddr *)&a, sizeof a) < 0)
    {
        perror("retriever: bind");
        return 1;
    }
    if (listen(s, 16) < 0)
    {
        perror("retriever: listen");
        return 1;
    }
    fprintf(stderr, "retriever: serving on %s\n", RTV_SOCK_PATH);
    for (;;)
    {
        int c = accept(s, NULL, NULL);
        if (c < 0)
            continue;
        pcl_t *p = malloc(sizeof *p);
        p->ix = ix;
        p->fd = c;
        if (plat_thread(posix_client_fn, p))
        {
            free(p);
            close(c);
        }
    }
}
#endif
