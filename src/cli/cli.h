#ifndef RTV_CLI_H
#define RTV_CLI_H

#include "../../include/retriever.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int ipc_request(const char *line);
int ipc_repl(const char *mode, long maxn);

#endif
