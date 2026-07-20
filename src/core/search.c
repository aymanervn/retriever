#include "core.h"

void *rtv_memmem(const void *hay, size_t hl, const void *nee, size_t nl)
{
    if (!nl)
        return (void *)hay;
    if (hl < nl)
        return NULL;
    const unsigned char *h = hay, *n = nee;
    const unsigned char *end = h + hl - nl + 1;
    unsigned char c0 = n[0];
    while (h < end && (h = memchr(h, c0, (size_t)(end - h))) != NULL)
    {
        if (!memcmp(h, n, nl))
            return (void *)h;
        h++;
    }
    return NULL;
}

enum
{
    M_NAME,
    M_PREFIX,
    M_SUFFIX,
    M_EXACT,
    M_PATH,
    M_REGEX
};

static const struct
{
    const char *name;
    int id;
} MODES[] = {
    {"name", M_NAME},   {"prefix", M_PREFIX}, {"suffix", M_SUFFIX},
    {"exact", M_EXACT}, {"path", M_PATH},     {"regex", M_REGEX},
};

const char *search_modes(void)
{
    return "name prefix suffix exact path regex";
}

static int mode_id(const char *s)
{
    for (size_t i = 0; i < sizeof MODES / sizeof MODES[0]; i++)
        if (!strcmp(s, MODES[i].name))
            return MODES[i].id;
    return -1;
}

static void hpush(hits_t *h, uint32_t v)
{
    if (h->n == h->cap)
    {
        h->cap = h->cap ? h->cap * 2 : 1024;
        h->v = realloc(h->v, h->cap * sizeof *h->v);
        if (!h->v)
        {
            fprintf(stderr, "retriever: out of memory\n");
            exit(1);
        }
    }
    h->v[h->n++] = v;
}

int search_run(index_t *ix, const char *mode, const char *query, size_t maxn, hits_t *out)
{
    int m = mode_id(mode);
    if (m < 0)
        return -1;

    char q[2048];
    size_t ql = strlen(query);
    if (ql >= sizeof q)
        ql = sizeof q - 1;
    for (size_t i = 0; i < ql; i++)
    {
        char c = query[i];
        q[i] = (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
    }
    q[ql] = 0;

    if (m == M_REGEX && re_valid(q))
        return -2;

    for (size_t i = 0; i < ix->nrecs && out->n < maxn; i++)
    {
        const rec_t *r = &ix->recs[i];
        if (r->flags & RF_DEAD)
            continue;
        const char *ln = ix->lpool + r->name_off;
        size_t nl = r->name_len;
        bool hit = false;
        switch (m)
        {
        case M_NAME:
            hit = rtv_memmem(ln, nl, q, ql) != NULL;
            break;
        case M_PREFIX:
            hit = nl >= ql && !memcmp(ln, q, ql);
            break;
        case M_SUFFIX:
            hit = nl >= ql && !memcmp(ln + nl - ql, q, ql);
            break;
        case M_EXACT:
            hit = nl == ql && !memcmp(ln, q, ql);
            break;
        case M_PATH:
        {
            char pb[RTV_MAX_PATH];
            size_t pl = idx_path(ix, (uint32_t)i, pb, sizeof pb);
            for (size_t k = 0; k < pl; k++)
            {
                char c = pb[k];
                if (c >= 'A' && c <= 'Z')
                    pb[k] = (char)(c + 32);
            }
            hit = rtv_memmem(pb, pl, q, ql) != NULL;
            break;
        }
        case M_REGEX:
            hit = re_match(q, ln, nl) > 0;
            break;
        }
        if (hit)
            hpush(out, (uint32_t)i);
    }
    return 0;
}
