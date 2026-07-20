#include "core.h"

enum
{
    T_LIT,
    T_NUM,
    T_ALPHA,
    T_SPECIAL
};

typedef struct
{
    unsigned char kind;
    char lit;
} tok_t;

#define RE_MAX_TOK 256

static int fold(int c)
{
    return (c >= 'A' && c <= 'Z') ? c + 32 : c;
}

static int lex(const char *pat, tok_t *out)
{
    int n = 0;
    for (const char *p = pat; *p; p++)
    {
        if (n == RE_MAX_TOK)
            return -1;
        tok_t *t = &out[n++];
        if (*p != '\\')
        {
            t->kind = T_LIT;
            t->lit = (char)fold((unsigned char)*p);
            continue;
        }
        p++;
        switch (fold((unsigned char)*p))
        {
        case 'd':
            t->kind = T_NUM;
            break;
        case 'a':
            t->kind = T_ALPHA;
            break;
        case 'p':
            t->kind = T_SPECIAL;
            break;
        case '\\':
            t->kind = T_LIT;
            t->lit = '\\';
            break;
        default:
            return -1;
        }
    }
    return n;
}

static bool in_class(unsigned char kind, int c)
{
    bool alnum = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z');
    if (kind == T_NUM)
        return c >= '0' && c <= '9';
    if (kind == T_ALPHA)
        return c >= 'a' && c <= 'z';
    return !alnum;
}

int re_match(const char *pat, const char *s, size_t n)
{
    tok_t tok[RE_MAX_TOK];
    int nt = lex(pat, tok);
    if (nt < 0)
        return 0;
    bool cur[RE_MAX_TOK + 1] = {0}, nxt[RE_MAX_TOK + 1];
    cur[0] = true;
    for (size_t k = 0; k < n; k++)
    {
        int c = fold((unsigned char)s[k]);
        memset(nxt, 0, (size_t)(nt + 1) * sizeof *nxt);
        bool any = false;
        for (int i = 0; i < nt; i++)
        {
            if (!cur[i])
                continue;
            if (tok[i].kind == T_LIT)
            {
                if (c == tok[i].lit)
                    nxt[i + 1] = any = true;
            }
            else if (in_class(tok[i].kind, c))
            {
                nxt[i] = nxt[i + 1] = any = true;
            }
        }
        if (!any)
            return 0;
        memcpy(cur, nxt, (size_t)(nt + 1) * sizeof *cur);
    }
    return cur[nt] ? 1 : 0;
}

const char *re_valid(const char *pat)
{
    int n = 0;
    for (const char *p = pat; *p; p++, n++)
    {
        if (n == RE_MAX_TOK)
            return "pattern too long";
        if (*p != '\\')
            continue;
        p++;
        if (!*p)
            return "trailing backslash";
        switch (fold((unsigned char)*p))
        {
        case 'd':
        case 'a':
        case 'p':
        case '\\':
            break;
        default:
            return "unknown escape (have \\d \\a \\p \\\\)";
        }
    }
    return NULL;
}
