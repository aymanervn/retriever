#ifndef RTV_CORE_H
#define RTV_CORE_H

#include "../common/plat.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#define RTV_SEP '\\'
#else
#define RTV_SEP '/'
#endif

#define RTV_MAX_VOLS 32
#define RTV_MAX_PATH 4096

enum
{
    RF_DIR = 1u << 0,
    RF_DEAD = 1u << 1
};

typedef struct
{
    uint64_t frn;
    uint64_t parent;
    uint32_t name_off;
    uint16_t name_len;
    uint8_t flags;
    uint8_t vol;
} rec_t;

typedef struct
{
    uint64_t *keys;
    uint32_t *vals;
    size_t cap;
    size_t used;
} fmap_t;

typedef struct
{
    char root[16];
    uint64_t root_frn;
    uint64_t journal_id;
    int64_t next_usn;
    void *handle;
} vol_t;

typedef struct
{
    rec_t *recs;
    size_t nrecs, crecs;
    char *pool;
    size_t npool, cpool;
    char *lpool;
    fmap_t map[RTV_MAX_VOLS];
    vol_t vols[RTV_MAX_VOLS];
    int nvols;
    size_t ndirs, nfiles;
    void *lock;
} index_t;

void idx_init(index_t *ix);
int idx_add_vol(index_t *ix, const char *root, uint64_t root_frn);
uint32_t idx_add(index_t *ix, int vol, uint64_t frn, uint64_t parent, const char *name, size_t nlen,
                 bool isdir);
bool idx_del(index_t *ix, int vol, uint64_t frn);
bool idx_move(index_t *ix, int vol, uint64_t frn, uint64_t newparent, const char *newname,
              size_t nlen);
size_t idx_path(const index_t *ix, uint32_t ri, char *out, size_t cap);
size_t idx_mem(const index_t *ix);
void idx_drop_vol(index_t *ix, int vol);
void idx_compact(index_t *ix);

typedef struct
{
    uint32_t *v;
    size_t n, cap;
} hits_t;
int search_run(index_t *ix, const char *mode, const char *query, size_t maxn, hits_t *out);
const char *search_modes(void);
void *rtv_memmem(const void *hay, size_t hl, const void *nee, size_t nl);

const char *re_valid(const char *pat);
int re_match(const char *pat, const char *s, size_t n);

#endif
