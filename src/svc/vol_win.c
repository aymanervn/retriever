#ifdef _WIN32
#include "svc.h"
#include <windows.h>
#include <winioctl.h>

typedef struct
{
    DWORD RecordLength;
    WORD MajorVersion, MinorVersion;
    ULONGLONG FileReferenceNumber, ParentFileReferenceNumber;
    LONGLONG Usn;
    LARGE_INTEGER TimeStamp;
    DWORD Reason, SourceInfo, SecurityId, FileAttributes;
    WORD FileNameLength, FileNameOffset;
    WCHAR FileName[1];
} usnrec_t;

typedef struct
{
    ULONGLONG Start;
    LONGLONG LowUsn, HighUsn;
} mftenum_t;
typedef struct
{
    ULONGLONG Start;
    LONGLONG LowUsn, HighUsn;
    WORD MinMajorVersion, MaxMajorVersion;
} mftenum1_t;
typedef struct
{
    ULONGLONG MaximumSize, AllocationDelta;
} createjd_t;
typedef struct
{
    ULONGLONG UsnJournalID;
    LONGLONG FirstUsn, NextUsn, LowestValidUsn, MaxUsn;
    ULONGLONG MaximumSize, AllocationDelta;
} qjd_t;
typedef struct
{
    LONGLONG StartUsn;
    DWORD ReasonMask, ReturnOnlyOnClose;
    ULONGLONG Timeout, BytesToWaitFor, UsnJournalID;
} readjd_t;

#ifndef FSCTL_ENUM_USN_DATA
#define FSCTL_ENUM_USN_DATA CTL_CODE(FILE_DEVICE_FILE_SYSTEM, 44, METHOD_NEITHER, FILE_ANY_ACCESS)
#endif
#ifndef FSCTL_READ_USN_JOURNAL
#define FSCTL_READ_USN_JOURNAL                                                                     \
    CTL_CODE(FILE_DEVICE_FILE_SYSTEM, 46, METHOD_NEITHER, FILE_ANY_ACCESS)
#endif
#ifndef FSCTL_CREATE_USN_JOURNAL
#define FSCTL_CREATE_USN_JOURNAL                                                                   \
    CTL_CODE(FILE_DEVICE_FILE_SYSTEM, 57, METHOD_NEITHER, FILE_ANY_ACCESS)
#endif
#ifndef FSCTL_QUERY_USN_JOURNAL
#define FSCTL_QUERY_USN_JOURNAL                                                                    \
    CTL_CODE(FILE_DEVICE_FILE_SYSTEM, 61, METHOD_BUFFERED, FILE_ANY_ACCESS)
#endif
#ifndef USN_REASON_FILE_CREATE
#define USN_REASON_FILE_CREATE 0x00000100
#endif
#ifndef USN_REASON_FILE_DELETE
#define USN_REASON_FILE_DELETE 0x00000200
#endif
#ifndef USN_REASON_RENAME_NEW_NAME
#define USN_REASON_RENAME_NEW_NAME 0x00002000
#endif
#ifndef ERROR_JOURNAL_DELETE_IN_PROGRESS
#define ERROR_JOURNAL_DELETE_IN_PROGRESS 1178L
#endif
#ifndef ERROR_JOURNAL_NOT_ACTIVE
#define ERROR_JOURNAL_NOT_ACTIVE 1179L
#endif
#ifndef ERROR_JOURNAL_ENTRY_DELETED
#define ERROR_JOURNAL_ENTRY_DELETED 1181L
#endif

static size_t u16to8(const WCHAR *w, size_t wn, char *out, size_t cap)
{
    size_t o = 0;
    for (size_t i = 0; i < wn; i++)
    {
        uint32_t c = w[i];
        if (c >= 0xD800 && c < 0xDC00 && i + 1 < wn && w[i + 1] >= 0xDC00 && w[i + 1] < 0xE000)
        {
            c = 0x10000 + ((c - 0xD800) << 10) + (w[i + 1] - 0xDC00);
            i++;
        }
        if (c < 0x80)
        {
            if (o + 1 > cap)
                break;
            out[o++] = (char)c;
        }
        else if (c < 0x800)
        {
            if (o + 2 > cap)
                break;
            out[o++] = (char)(0xC0 | (c >> 6));
            out[o++] = (char)(0x80 | (c & 63));
        }
        else if (c < 0x10000)
        {
            if (o + 3 > cap)
                break;
            out[o++] = (char)(0xE0 | (c >> 12));
            out[o++] = (char)(0x80 | ((c >> 6) & 63));
            out[o++] = (char)(0x80 | (c & 63));
        }
        else
        {
            if (o + 4 > cap)
                break;
            out[o++] = (char)(0xF0 | (c >> 18));
            out[o++] = (char)(0x80 | ((c >> 12) & 63));
            out[o++] = (char)(0x80 | ((c >> 6) & 63));
            out[o++] = (char)(0x80 | (c & 63));
        }
    }
    return o;
}

static uint64_t root_frn_of(char letter)
{
    char rp[8] = {letter, ':', '\\', 0};
    HANDLE h = CreateFileA(rp, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                           OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return 5;
    BY_HANDLE_FILE_INFORMATION bi;
    uint64_t f = 5;
    if (GetFileInformationByHandle(h, &bi))
        f = ((uint64_t)bi.nFileIndexHigh << 32) | bi.nFileIndexLow;
    CloseHandle(h);
    return f;
}

int win_is_admin(void)
{
    HANDLE t;
    TOKEN_ELEVATION e;
    DWORD n;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &t))
        return 0;
    int r = GetTokenInformation(t, TokenElevation, &e, sizeof e, &n) && e.TokenIsElevated;
    CloseHandle(t);
    return r;
}

static int enum_mft(index_t *ix, int vol, HANDLE h)
{
    vol_t *v = &ix->vols[vol];
    int64_t t0 = plat_usec();
    size_t before = ix->nrecs;
    DWORD bufsz = 1 << 20, br;
    BYTE *buf = malloc(bufsz);
    if (!buf)
        return -1;
    char nm[1024];
    mftenum1_t med = {0, 0, 0x7FFFFFFFFFFFFFFFLL, 2, 2};
    DWORD insz = sizeof med;
    for (;;)
    {
        if (!DeviceIoControl(h, FSCTL_ENUM_USN_DATA, &med, insz, buf, bufsz, &br, NULL))
        {
            if (insz == sizeof med && med.Start == 0 && GetLastError() == ERROR_INVALID_PARAMETER)
            {
                insz = sizeof(mftenum_t);
                continue;
            }
            break;
        }
        if (br < sizeof(ULONGLONG))
            break;
        med.Start = *(ULONGLONG *)buf;
        DWORD off = sizeof(ULONGLONG);
        while (off + sizeof(DWORD) <= br)
        {
            usnrec_t *u = (usnrec_t *)(buf + off);
            if (!u->RecordLength || off + u->RecordLength > br)
                break;
            size_t nl = u16to8((WCHAR *)((BYTE *)u + u->FileNameOffset), u->FileNameLength / 2, nm,
                               sizeof nm);
            if (u->MajorVersion == 2 && u->FileReferenceNumber != v->root_frn &&
                !(nl > 0 && nm[0] == '$'))
            {
                if (!idx_move(ix, vol, u->FileReferenceNumber, u->ParentFileReferenceNumber, nm,
                              nl))
                    idx_add(ix, vol, u->FileReferenceNumber, u->ParentFileReferenceNumber, nm, nl,
                            (u->FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0);
            }
            off += u->RecordLength;
        }
    }
    free(buf);
    fprintf(stderr, "retriever: %s indexed %zu entries in %.2fs\n", v->root, ix->nrecs - before,
            (double)(plat_usec() - t0) / 1e6);
    return 0;
}

static int win_rescan_volume(index_t *ix, int vol)
{
    vol_t *v = &ix->vols[vol];
    if (!v->handle)
        return -1;
    char vp[16];
    snprintf(vp, sizeof vp, "\\\\.\\%c:", v->root[0]);
    HANDLE h = CreateFileA(vp, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return -1;
    DWORD br;
    createjd_t cj = {0x2000000, 0x400000};
    DeviceIoControl(h, FSCTL_CREATE_USN_JOURNAL, &cj, sizeof cj, NULL, 0, &br, NULL);
    qjd_t q;
    if (DeviceIoControl(h, FSCTL_QUERY_USN_JOURNAL, NULL, 0, &q, sizeof q, &br, NULL))
    {
        v->journal_id = q.UsnJournalID;
        v->next_usn = q.NextUsn;
    }
    plat_wrlock(ix->lock);
    idx_drop_vol(ix, vol);
    idx_compact(ix);
    int rc = enum_mft(ix, vol, h);
    plat_wrunlock(ix->lock);
    CloseHandle(h);
    return rc;
}

int svc_rescan_all(index_t *ix)
{
    int n = 0;
    for (int v = 0; v < ix->nvols; v++)
        if (win_rescan_volume(ix, v) == 0)
            n++;
    return n;
}

typedef struct
{
    index_t *ix;
    int vol;
    HANDLE h;
} mon_t;

static void monitor_fn(void *arg)
{
    mon_t *m = arg;
    index_t *ix = m->ix;
    vol_t *v = &ix->vols[m->vol];
    DWORD bufsz = 1 << 16, br;
    BYTE *buf = malloc(bufsz);
    char nm[1024];
    int64_t last_rescan = 0;
    for (;;)
    {
        readjd_t rj = {v->next_usn, 0xFFFFFFFF, 0, 0, 1, v->journal_id};
        if (!DeviceIoControl(m->h, FSCTL_READ_USN_JOURNAL, &rj, sizeof rj, buf, bufsz, &br, NULL))
        {
            DWORD e = GetLastError();
            if ((e == ERROR_JOURNAL_ENTRY_DELETED || e == ERROR_JOURNAL_NOT_ACTIVE ||
                 e == ERROR_JOURNAL_DELETE_IN_PROGRESS || e == ERROR_INVALID_PARAMETER) &&
                plat_usec() - last_rescan > 60 * 1000000LL)
            {
                last_rescan = plat_usec();
                fprintf(stderr,
                        "retriever: %s journal wrapped/reset (err %lu); "
                        "rescanning volume\n",
                        v->root, e);
                win_rescan_volume(ix, m->vol);
            }
            plat_sleep_ms(1000);
            continue;
        }
        if (br < sizeof(LONGLONG))
            continue;
        v->next_usn = *(LONGLONG *)buf;
        DWORD off = sizeof(LONGLONG);
        plat_wrlock(ix->lock);
        while (off + sizeof(DWORD) <= br)
        {
            usnrec_t *u = (usnrec_t *)(buf + off);
            if (!u->RecordLength || off + u->RecordLength > br)
                break;
            if (u->MajorVersion != 2 || u->FileReferenceNumber == v->root_frn)
            {
                off += u->RecordLength;
                continue;
            }
            size_t nl = u16to8((WCHAR *)((BYTE *)u + u->FileNameOffset), u->FileNameLength / 2, nm,
                               sizeof nm);
            bool isdir = (u->FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
            if (u->Reason & USN_REASON_FILE_DELETE)
            {
                idx_del(ix, m->vol, u->FileReferenceNumber);
            }
            else if (u->Reason & (USN_REASON_RENAME_NEW_NAME | USN_REASON_FILE_CREATE))
            {
                if (!idx_move(ix, m->vol, u->FileReferenceNumber, u->ParentFileReferenceNumber, nm,
                              nl))
                    idx_add(ix, m->vol, u->FileReferenceNumber, u->ParentFileReferenceNumber, nm,
                            nl, isdir);
            }
            off += u->RecordLength;
        }
        plat_wrunlock(ix->lock);
    }
}

int win_index_volume(index_t *ix, char letter)
{
    char vp[16];
    snprintf(vp, sizeof vp, "\\\\.\\%c:", letter);
    HANDLE h = CreateFileA(vp, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE)
    {
        fprintf(stderr, "retriever: cannot open %c: (err %lu) -- run elevated?\n", letter,
                GetLastError());
        return -1;
    }
    DWORD br;
    createjd_t cj = {0x2000000, 0x400000};
    DeviceIoControl(h, FSCTL_CREATE_USN_JOURNAL, &cj, sizeof cj, NULL, 0, &br, NULL);
    qjd_t q;
    if (!DeviceIoControl(h, FSCTL_QUERY_USN_JOURNAL, NULL, 0, &q, sizeof q, &br, NULL))
    {
        fprintf(stderr, "retriever: %c: no USN journal (err %lu)\n", letter, GetLastError());
        CloseHandle(h);
        return -1;
    }
    char root[8] = {letter, ':', 0};
    int vol = idx_add_vol(ix, root, root_frn_of(letter));
    if (vol < 0)
    {
        CloseHandle(h);
        return -1;
    }
    vol_t *v = &ix->vols[vol];
    v->handle = h;
    v->journal_id = q.UsnJournalID;
    v->next_usn = q.NextUsn;

    plat_wrlock(ix->lock);
    enum_mft(ix, vol, h);
    plat_wrunlock(ix->lock);

    mon_t *m = malloc(sizeof *m);
    m->ix = ix;
    m->vol = vol;
    m->h = h;
    plat_thread(monitor_fn, m);
    return 0;
}

int win_all_volumes(index_t *ix)
{
    DWORD mask = GetLogicalDrives();
    int n = 0;
    for (int i = 0; i < 26; i++)
    {
        if (!(mask & (1u << i)))
            continue;
        char root[8] = {(char)('A' + i), ':', '\\', 0};
        char fs[16] = {0};
        if (GetDriveTypeA(root) != DRIVE_FIXED)
            continue;
        if (GetVolumeInformationA(root, NULL, 0, NULL, NULL, NULL, fs, sizeof fs) &&
            (!strcmp(fs, "NTFS") || !strcmp(fs, "ReFS")))
        {
            if (win_index_volume(ix, (char)('A' + i)) == 0)
                n++;
        }
    }
    return n;
}
#else
typedef int rtv_vol_win_not_built;
#endif
