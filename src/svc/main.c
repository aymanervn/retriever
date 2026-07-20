#include "svc.h"
#include <ctype.h>

static void usage(void)
{
    fprintf(stderr,
            "retriever " RTV_VERSION " -- instant filename search service\n"
            "\n"
            "  retriever [C: D: ...]        index NTFS volumes + serve from the "
            "tray\n"
            "                                (elevates via UAC; no drives given "
            "= all fixed NTFS/ReFS)\n"
            "  retriever --version | --help\n"
            "\n"
            "Clients connect over the pipe/socket line protocol (see "
            "src/common/proto.h);\n"
            "the bundled `rtv` CLI is the reference client. Search modes: %s\n",
            search_modes());
}

#ifdef _WIN32
#include <windows.h>
/* GUI-subsystem exe: no console of its own, so best-effort attach to the
 * launching shell's console for --help/--version and error output. */
static void attach_parent_console(void)
{
    if (AttachConsole(ATTACH_PARENT_PROCESS))
    {
        freopen("CONOUT$", "w", stdout);
        freopen("CONOUT$", "w", stderr);
    }
}
int main(int argc, char **argv);
int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show)
{
    (void)inst;
    (void)prev;
    (void)cmd;
    (void)show;
    attach_parent_console();
    return main(__argc, __argv);
}
#endif

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
    return tray_run(&ix, drives, ndr);
#else
    if (ndr)
        fprintf(stderr, "retriever: note: drive '%c:' ignored off Windows\n", drives[0]);
    fprintf(stderr, "retriever: NTFS indexing needs Windows\n");
    return 1;
#endif
}
