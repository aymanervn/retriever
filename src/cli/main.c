#include "cli.h"
#ifdef _WIN32
#include <windows.h>
#endif

static void usage(void)
{
    fprintf(stderr, "rtv " RTV_VERSION " -- retriever search client\n"
                    "\n"
                    "  rtv [-m MODE] [-n MAX] QUERY...   search (modes: name prefix suffix\n"
                    "                                   exact path regex; default: name)\n"
                    "  rtv [-m MODE] [-n MAX]            interactive prompt\n"
                    "  rtv stats | ping | rescan | version | help\n");
}

int main(int argc, char **argv)
{
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    if (argc == 2)
    {
        if (!strcmp(argv[1], "stats"))
            return ipc_request("STATS");
        if (!strcmp(argv[1], "ping"))
            return ipc_request("PING");
        if (!strcmp(argv[1], "rescan"))
            return ipc_request("RESCAN");
        if (!strcmp(argv[1], "version"))
        {
            puts(RTV_VERSION);
            return 0;
        }
        if (!strcmp(argv[1], "help") || !strcmp(argv[1], "-h") || !strcmp(argv[1], "--help"))
        {
            usage();
            return 0;
        }
    }

    const char *mode = "name";
    long maxn = 0;
    int i = 1;
    for (; i < argc; i++)
    {
        if (!strcmp(argv[i], "-m") && i + 1 < argc)
            mode = argv[++i];
        else if (!strcmp(argv[i], "-n") && i + 1 < argc)
            maxn = atol(argv[++i]);
        else
            break;
    }
    char q[2048] = {0};
    size_t o = 0;
    for (; i < argc; i++)
    {
        int w = snprintf(q + o, sizeof q - o, "%s%s", o ? " " : "", argv[i]);
        if (w < 0 || (o += (size_t)w) >= sizeof q - 1)
            break;
    }
    if (!*q)
        return ipc_repl(mode, maxn);
    char line[2400];
    snprintf(line, sizeof line, "S %s %ld %s", mode, maxn, q);
    return ipc_request(line);
}
