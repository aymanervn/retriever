#define _POSIX_C_SOURCE 200809L
#include "cli.h"

#define RTV_CLIENT_TIMEOUT_MS 30000

typedef struct
{
#ifdef _WIN32
    void *h;
    void *ev;
#else
    int fd;
#endif
    char buf[8192];
    int bl, bo;
} conn_t;

#ifdef _WIN32
#include <windows.h>

static int c_open(conn_t *c)
{
    memset(c, 0, sizeof *c);
    for (;;)
    {
        c->h = CreateFileA(RTV_PIPE_NAME, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING,
                           FILE_FLAG_OVERLAPPED, NULL);
        if (c->h != INVALID_HANDLE_VALUE)
            break;
        if (GetLastError() == ERROR_PIPE_BUSY)
        {
            WaitNamedPipeA(RTV_PIPE_NAME, 3000);
            continue;
        }
        fprintf(stderr, "rtv: cannot reach service (err %lu). Is `retriever` running?\n",
                GetLastError());
        return -1;
    }
    c->ev = CreateEventA(NULL, TRUE, FALSE, NULL);
    if (!c->ev)
    {
        CloseHandle(c->h);
        return -1;
    }
    return 0;
}
static int c_io(conn_t *c, void *b, int n, int write)
{
    OVERLAPPED ov;
    memset(&ov, 0, sizeof ov);
    ov.hEvent = c->ev;
    ResetEvent(c->ev);
    BOOL ok =
        write ? WriteFile(c->h, b, (DWORD)n, NULL, &ov) : ReadFile(c->h, b, (DWORD)n, NULL, &ov);
    if (!ok && GetLastError() != ERROR_IO_PENDING)
        return -1;
    if (WaitForSingleObject(c->ev, RTV_CLIENT_TIMEOUT_MS) != WAIT_OBJECT_0)
    {
        CancelIoEx(c->h, &ov);
        WaitForSingleObject(c->ev, INFINITE);
        fprintf(stderr, "rtv: timed out after %d ms waiting for service\n", RTV_CLIENT_TIMEOUT_MS);
        return -1;
    }
    DWORD done = 0;
    if (!GetOverlappedResult(c->h, &ov, &done, FALSE))
        return -1;
    return (int)done;
}
static int c_send(conn_t *c, const char *s)
{
    return c_io(c, (void *)s, (int)strlen(s), 1) < 0 ? -1 : 0;
}
static int c_raw(conn_t *c, char *b, int n)
{
    int r = c_io(c, b, n, 0);
    return r < 0 ? 0 : r;
}
static void c_close(conn_t *c)
{
    CloseHandle(c->h);
    CloseHandle(c->ev);
}

#else
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>

static int c_open(conn_t *c)
{
    memset(c, 0, sizeof *c);
    c->fd = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un a;
    memset(&a, 0, sizeof a);
    a.sun_family = AF_UNIX;
    strncpy(a.sun_path, RTV_SOCK_PATH, sizeof a.sun_path - 1);
    if (connect(c->fd, (struct sockaddr *)&a, sizeof a) < 0)
    {
        fprintf(stderr, "rtv: cannot reach service at %s. Is `retriever` running?\n",
                RTV_SOCK_PATH);
        close(c->fd);
        return -1;
    }
    struct timeval tv = {RTV_CLIENT_TIMEOUT_MS / 1000, 0};
    setsockopt(c->fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    setsockopt(c->fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
    return 0;
}
static int c_send(conn_t *c, const char *s)
{
    size_t n = strlen(s);
    return write(c->fd, s, n) == (ssize_t)n ? 0 : -1;
}
static int c_raw(conn_t *c, char *b, int n)
{
    ssize_t r = read(c->fd, b, (size_t)n);
    return r < 0 ? 0 : (int)r;
}
static void c_close(conn_t *c)
{
    close(c->fd);
}
#endif

static int c_line(conn_t *c, char *out, int cap)
{
    int o = 0;
    for (;;)
    {
        while (c->bo < c->bl)
        {
            char ch = c->buf[c->bo++];
            if (ch == '\n')
            {
                out[o] = 0;
                return o;
            }
            if (o < cap - 1)
                out[o++] = ch;
        }
        c->bl = c_raw(c, c->buf, (int)sizeof c->buf);
        c->bo = 0;
        if (c->bl <= 0)
        {
            out[o] = 0;
            return o ? o : -1;
        }
    }
}

static int run_request(conn_t *c, const char *line)
{
    char l[4096];
    snprintf(l, sizeof l, "%s\n", line);
    if (c_send(c, l))
        return -1;
    char b[RTV_MAX_LINE];
    for (;;)
    {
        int n = c_line(c, b, sizeof b);
        if (n < 0)
        {
            fprintf(stderr, "rtv: connection lost\n");
            return -1;
        }
        if (!strcmp(b, "."))
            return 0;
        if (b[0] == '*')
            fprintf(stderr, "%s\n", b + 2);
        else if (b[0] == '!')
            fprintf(stderr, "rtv: %s\n", b + 2);
        else
            printf("%s\n", b);
    }
}

int ipc_request(const char *line)
{
    conn_t c;
    if (c_open(&c))
        return 1;
    int r = run_request(&c, line);
    c_close(&c);
    return r ? 1 : 0;
}

int ipc_repl(const char *mode, long maxn)
{
    conn_t c;
    if (c_open(&c))
        return 1;
    if (maxn <= 0)
        maxn = 30;
    char in[2048], req[2300];
    fprintf(stderr, "rtv " RTV_VERSION " interactive (mode=%s, max=%ld). 'q' quits.\n", mode, maxn);
    for (;;)
    {
        fprintf(stderr, "rt> ");
        fflush(stderr);
        if (!fgets(in, sizeof in, stdin))
            break;
        size_t L = strlen(in);
        while (L && (in[L - 1] == '\n' || in[L - 1] == '\r'))
            in[--L] = 0;
        if (!strcmp(in, "q") || !strcmp(in, "quit"))
            break;
        snprintf(req, sizeof req, "S %s %ld %s", mode, maxn, in);
        if (run_request(&c, req))
            break;
    }
    c_close(&c);
    return 0;
}
