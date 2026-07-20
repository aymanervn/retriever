#ifndef RTV_PLAT_H
#define RTV_PLAT_H

#if defined(_WIN32)
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#ifndef __USE_MINGW_ANSI_STDIO
#define __USE_MINGW_ANSI_STDIO 1
#endif
#endif

#include <stdint.h>

void *plat_rwlock_new(void);
void plat_rdlock(void *l);
void plat_rdunlock(void *l);
void plat_wrlock(void *l);
void plat_wrunlock(void *l);
int plat_thread(void (*fn)(void *), void *arg);
void plat_sleep_ms(int ms);
int64_t plat_usec(void);

#endif
