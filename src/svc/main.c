#include "svc.h"
#include <ctype.h>

static void usage(void)
{
    fprintf(stderr,
            "retriever " RTV_VERSION " -- instant filename search service\n"
            "\n"
            "  retriever [C: D: ...]        index NTFS volumes + serve (needs "
            "admin;\n"
            "                                no drives given = all fixed NTFS/ReFS)\n"
            "  retriever --version | --help\n"
            "\n"
            "Clients connect over the pipe/socket line protocol (see "
            "retriever.h);\n"
            "the bundled `rtv` CLI is the reference client. Search modes: %s\n",
            search_modes());
}

int main(int argc, char **argv)
{
    index_t ix;
    idx_init(&ix);
    char drives[26];
    int ndr = 0;

    for (int i = 1; i < argc; i++)
    {
        if (!strcmp(argv[i], "--version"))
        {
            puts(RTV_VERSION);
            return 0;
        }
        else if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h"))
        {
            usage();
            return 0;
        }
        else if (isalpha((unsigned char)argv[i][0]) &&
                 (argv[i][1] == 0 || (argv[i][1] == ':' && argv[i][2] == 0)))
        {
            char d = (char)toupper((unsigned char)argv[i][0]);
            bool dup = false;
            for (int k = 0; k < ndr; k++)
                if (drives[k] == d)
                    dup = true;
            if (!dup && ndr < 26)
                drives[ndr++] = d;
        }
        else
        {
            fprintf(stderr, "retriever: bad arg '%s' (see --help)\n", argv[i]);
            return 2;
        }
    }

    if (ipc_endpoint_busy())
    {
        fprintf(stderr, "retriever: another service instance is already running\n");
        return 1;
    }

#ifdef _WIN32
    if (!win_is_admin())
        fprintf(stderr, "retriever: warning: not elevated; raw volume access "
                        "will likely fail\n");
    int ok = 0;
    if (ndr)
    {
        for (int i = 0; i < ndr; i++)
            if (win_index_volume(&ix, drives[i]) == 0)
                ok++;
    }
    else
        ok = win_all_volumes(&ix);
    if (!ok)
    {
        fprintf(stderr, "retriever: no volumes indexed\n");
        return 1;
    }
#else
    if (ndr)
        fprintf(stderr, "retriever: note: drive '%c:' ignored off Windows\n", drives[0]);
    fprintf(stderr, "retriever: NTFS indexing needs Windows\n");
    return 1;
#endif
    return ipc_serve(&ix);
}
