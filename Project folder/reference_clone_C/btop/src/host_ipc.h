#ifndef HOST_IPC_H
#define HOST_IPC_H

#include "wintop.h"

bool host_ipc_start(const char *agent_override);
void host_ipc_poll(void);
void host_ipc_stop(void);

#endif // HOST_IPC_H
