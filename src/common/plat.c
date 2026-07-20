#define _POSIX_C_SOURCE 200809L
#include "plat.h"
#include <stdlib.h>

#ifdef _WIN32
#include <windows.h>

void *plat_rwlock_new(void)
{
    SRWLOCK *l = malloc(sizeof *l);
    InitializeSRWLock(l);
    return l;
}
void plat_rdlock(void *l)
{
    AcquireSRWLockShared((SRWLOCK *)l);
}
void plat_rdunlock(void *l)
{
    ReleaseSRWLockShared((SRWLOCK *)l);
}
void plat_wrlock(void *l)
{
    AcquireSRWLockExclusive((SRWLOCK *)l);
}
void plat_wrunlock(void *l)
{
    ReleaseSRWLockExclusive((SRWLOCK *)l);
}

typedef struct
{
    void (*fn)(void *);
    void *a;
} thw_t;
static DWORD WINAPI tramp(LPVOID p)
{
    thw_t t = *(thw_t *)p;
    free(p);
    t.fn(t.a);
    return 0;
}
int plat_thread(void (*fn)(void *), void *arg)
{
    thw_t *w = malloc(sizeof *w);
    if (!w)
        return -1;
    w->fn = fn;
    w->a = arg;
    HANDLE h = CreateThread(NULL, 0, tramp, w, 0, NULL);
    if (!h)
    {
        free(w);
        return -1;
    }
    CloseHandle(h);
    return 0;
}
void plat_sleep_ms(int ms)
{
    Sleep((DWORD)ms);
}
int64_t plat_usec(void)
{
    LARGE_INTEGER f, c;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return (int64_t)(c.QuadPart * 1000000 / f.QuadPart);
}

#else
#include <pthread.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>

void *plat_rwlock_new(void)
{
    pthread_rwlock_t *l = malloc(sizeof *l);
    pthread_rwlock_init(l, NULL);
    return l;
}
void plat_rdlock(void *l)
{
    pthread_rwlock_rdlock((pthread_rwlock_t *)l);
}
void plat_rdunlock(void *l)
{
    pthread_rwlock_unlock((pthread_rwlock_t *)l);
}
void plat_wrlock(void *l)
{
    pthread_rwlock_wrlock((pthread_rwlock_t *)l);
}
void plat_wrunlock(void *l)
{
    pthread_rwlock_unlock((pthread_rwlock_t *)l);
}

typedef struct
{
    void (*fn)(void *);
    void *a;
} thw_t;
static void *tramp(void *p)
{
    thw_t t = *(thw_t *)p;
    free(p);
    t.fn(t.a);
    return NULL;
}
int plat_thread(void (*fn)(void *), void *arg)
{
    thw_t *w = malloc(sizeof *w);
    if (!w)
        return -1;
    w->fn = fn;
    w->a = arg;
    pthread_t t;
    if (pthread_create(&t, NULL, tramp, w))
    {
        free(w);
        return -1;
    }
    pthread_detach(t);
    return 0;
}
void plat_sleep_ms(int ms)
{
    struct timespec ts = {ms / 1000, (long)(ms % 1000) * 1000000L};
    nanosleep(&ts, NULL);
}
int64_t plat_usec(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (int64_t)tv.tv_sec * 1000000 + tv.tv_usec;
}
#endif
