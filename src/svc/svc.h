#ifndef RTV_SVC_H
#define RTV_SVC_H

#include "../../include/retriever.h"
#include "../core/core.h"

typedef void (*emit_fn)(void *ctx, const char *buf, size_t len);
void ipc_handle_line(index_t *ix, char *line, emit_fn emit, void *ctx);
int ipc_serve(index_t *ix);
int ipc_endpoint_busy(void);
int svc_rescan_all(index_t *ix);
#ifdef _WIN32
int win_index_volume(index_t *ix, char letter);
int win_all_volumes(index_t *ix);
int tray_run(index_t *ix, const char *drives, int ndr);
#endif

#endif
