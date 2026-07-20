#include "core.h"

#define TOMB ((uint64_t)-1)

static uint64_t hsh(uint64_t k)
{
    k ^= k >> 33;
    k *= 0xff51afd7ed558ccdULL;
    k ^= k >> 33;
    k *= 0xc4ceb9fe1a85ec53ULL;
    k ^= k >> 33;
    return k;
}

static void oom(void)
{
    fprintf(stderr, "retriever: out of memory\n");
    exit(1);
}

static void fmap_grow(fmap_t *m, size_t ncap)
{
    uint64_t *ok = m->keys;
    uint32_t *ov = m->vals;
    size_t oc = m->cap;
    m->keys = calloc(ncap, sizeof *m->keys);
    m->vals = malloc(ncap * sizeof *m->vals);
    if (!m->keys || !m->vals)
        oom();
    m->cap = ncap;
    m->used = 0;
    for (size_t i = 0; i < oc; i++)
    {
        if (ok[i] && ok[i] != TOMB)
        {
            size_t j = hsh(ok[i]) & (ncap - 1);
            while (m->keys[j])
                j = (j + 1) & (ncap - 1);
            m->keys[j] = ok[i];
            m->vals[j] = ov[i];
            m->used++;
        }
    }
    free(ok);
    free(ov);
}

static void fmap_put(fmap_t *m, uint64_t k, uint32_t v)
{
    if (!m->cap)
        fmap_grow(m, 1 << 16);
    if (m->used * 10 >= m->cap * 7)
        fmap_grow(m, m->cap << 1);
    size_t j = hsh(k) & (m->cap - 1), tomb = (size_t)-1;
    for (;;)
    {
        if (!m->keys[j])
            break;
        if (m->keys[j] == TOMB)
        {
            if (tomb == (size_t)-1)
                tomb = j;
        }
        else if (m->keys[j] == k)
        {
            m->vals[j] = v;
            return;
        }
        j = (j + 1) & (m->cap - 1);
    }
    if (tomb != (size_t)-1)
        j = tomb;
    else
        m->used++;
    m->keys[j] = k;
    m->vals[j] = v;
}

static bool fmap_get(const fmap_t *m, uint64_t k, uint32_t *v)
{
    if (!m->cap)
        return false;
    size_t j = hsh(k) & (m->cap - 1);
    while (m->keys[j])
    {
        if (m->keys[j] == k)
        {
            *v = m->vals[j];
            return true;
        }
        j = (j + 1) & (m->cap - 1);
    }
    return false;
}

static void fmap_del(fmap_t *m, uint64_t k)
{
    if (!m->cap)
        return;
    size_t j = hsh(k) & (m->cap - 1);
    while (m->keys[j])
    {
        if (m->keys[j] == k)
        {
            m->keys[j] = TOMB;
            return;
        }
        j = (j + 1) & (m->cap - 1);
    }
}

void idx_init(index_t *ix)
{
    memset(ix, 0, sizeof *ix);
    ix->lock = plat_rwlock_new();
}

int idx_add_vol(index_t *ix, const char *root, uint64_t root_frn)
{
    if (ix->nvols >= RTV_MAX_VOLS)
        return -1;
    int v = ix->nvols++;
    vol_t *vp = &ix->vols[v];
    memset(vp, 0, sizeof *vp);
    snprintf(vp->root, sizeof vp->root, "%s", root);
    vp->root_frn = root_frn;
    return v;
}

static uint32_t pool_add(index_t *ix, const char *s, size_t n)
{
    if (ix->npool + n + 1 > ix->cpool)
    {
        size_t nc = ix->cpool ? ix->cpool * 2 : (1 << 20);
        while (nc < ix->npool + n + 1)
            nc *= 2;
        ix->pool = realloc(ix->pool, nc);
        ix->lpool = realloc(ix->lpool, nc);
        if (!ix->pool || !ix->lpool)
            oom();
        ix->cpool = nc;
    }
    uint32_t off = (uint32_t)ix->npool;
    memcpy(ix->pool + off, s, n);
    ix->pool[off + n] = 0;
    for (size_t i = 0; i < n; i++)
    {
        char c = s[i];
        ix->lpool[off + i] = (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
    }
    ix->lpool[off + n] = 0;
    ix->npool += n + 1;
    return off;
}

uint32_t idx_add(index_t *ix, int vol, uint64_t frn, uint64_t parent, const char *name, size_t nlen,
                 bool isdir)
{
    if (ix->nrecs == ix->crecs)
    {
        size_t nc = ix->crecs ? ix->crecs * 2 : (1 << 18);
        ix->recs = realloc(ix->recs, nc * sizeof(rec_t));
        if (!ix->recs)
            oom();
        ix->crecs = nc;
    }
    if (nlen > 0xFFFF)
        nlen = 0xFFFF;
    rec_t *r = &ix->recs[ix->nrecs];
    r->frn = frn;
    r->parent = parent;
    r->name_off = pool_add(ix, name, nlen);
    r->name_len = (uint16_t)nlen;
    r->flags = isdir ? RF_DIR : 0;
    r->vol = (uint8_t)vol;
    uint32_t ri = (uint32_t)ix->nrecs++;
    fmap_put(&ix->map[vol], frn, ri);
    if (isdir)
        ix->ndirs++;
    else
        ix->nfiles++;
    return ri;
}

bool idx_del(index_t *ix, int vol, uint64_t frn)
{
    uint32_t ri;
    if (!fmap_get(&ix->map[vol], frn, &ri))
        return false;
    rec_t *r = &ix->recs[ri];
    if (r->flags & RF_DEAD)
        return false;
    r->flags |= RF_DEAD;
    fmap_del(&ix->map[vol], frn);
    if (r->flags & RF_DIR)
    {
        if (ix->ndirs)
            ix->ndirs--;
    }
    else
    {
        if (ix->nfiles)
            ix->nfiles--;
    }
    return true;
}

bool idx_move(index_t *ix, int vol, uint64_t frn, uint64_t newparent, const char *newname,
              size_t nlen)
{
    uint32_t ri;
    if (!fmap_get(&ix->map[vol], frn, &ri))
        return false;
    rec_t *r = &ix->recs[ri];
    r->parent = newparent;
    if (nlen > 0xFFFF)
        nlen = 0xFFFF;
    if (nlen == r->name_len && !memcmp(ix->pool + r->name_off, newname, nlen))
        return true;
    r->name_off = pool_add(ix, newname, nlen);
    r->name_len = (uint16_t)nlen;
    return true;
}

#define PATH_CHAIN_MAX 1024

size_t idx_path(const index_t *ix, uint32_t ri, char *out, size_t cap)
{
    uint32_t chain[PATH_CHAIN_MAX];
    int depth = 0;
    uint32_t cur = ri;
    bool truncated = false;
    for (;;)
    {
        if (depth == PATH_CHAIN_MAX)
        {
            truncated = true;
            break;
        }
        chain[depth++] = cur;
        const rec_t *r = &ix->recs[cur];
        if (r->frn == r->parent)
            break;
        uint32_t pi;
        if (!fmap_get(&ix->map[r->vol], r->parent, &pi))
            break;
        if (pi == cur)
            break;
        cur = pi;
    }
    const rec_t *top = &ix->recs[chain[depth - 1]];
    size_t o = (size_t)snprintf(out, cap, "%s", ix->vols[top->vol].root);
    if (truncated && o + 4 < cap)
    {
        memcpy(out + o, "\\...", 4);
        o += 4;
    }
    for (int i = depth - 1; i >= 0; i--)
    {
        const rec_t *r = &ix->recs[chain[i]];
        if (o + 1 + r->name_len + 1 >= cap)
            break;
        out[o++] = RTV_SEP;
        memcpy(out + o, ix->pool + r->name_off, r->name_len);
        o += r->name_len;
    }
    out[o] = 0;
    return o;
}

void idx_drop_vol(index_t *ix, int vol)
{
    for (size_t i = 0; i < ix->nrecs; i++)
    {
        rec_t *r = &ix->recs[i];
        if (r->vol != vol || (r->flags & RF_DEAD))
            continue;
        r->flags |= RF_DEAD;
        if (r->flags & RF_DIR)
        {
            if (ix->ndirs)
                ix->ndirs--;
        }
        else
        {
            if (ix->nfiles)
                ix->nfiles--;
        }
    }
    fmap_t *m = &ix->map[vol];
    free(m->keys);
    free(m->vals);
    memset(m, 0, sizeof *m);
}

void idx_compact(index_t *ix)
{
    char *pool2 = malloc(ix->cpool);
    char *lpool2 = malloc(ix->cpool);
    if (!pool2 || !lpool2)
        oom();
    size_t w = 0, np = 0;
    for (size_t i = 0; i < ix->nrecs; i++)
    {
        rec_t r = ix->recs[i];
        if (r.flags & RF_DEAD)
            continue;
        memcpy(pool2 + np, ix->pool + r.name_off, r.name_len + 1u);
        memcpy(lpool2 + np, ix->lpool + r.name_off, r.name_len + 1u);
        r.name_off = (uint32_t)np;
        np += r.name_len + 1u;
        ix->recs[w++] = r;
    }
    free(ix->pool);
    ix->pool = pool2;
    free(ix->lpool);
    ix->lpool = lpool2;
    ix->npool = np;
    ix->nrecs = w;
    for (int v = 0; v < ix->nvols; v++)
    {
        fmap_t *m = &ix->map[v];
        free(m->keys);
        free(m->vals);
        memset(m, 0, sizeof *m);
    }
    for (size_t i = 0; i < w; i++)
        fmap_put(&ix->map[ix->recs[i].vol], ix->recs[i].frn, (uint32_t)i);
}

size_t idx_mem(const index_t *ix)
{
    size_t m = ix->crecs * sizeof(rec_t) + ix->cpool * 2;
    for (int i = 0; i < ix->nvols; i++)
        m += ix->map[i].cap * (sizeof(uint64_t) + sizeof(uint32_t));
    return m;
}
