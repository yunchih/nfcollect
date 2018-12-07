
#ifndef _COLLECT_H
#define _COLLECT_H

#include "main.h"
void collect_open_netlink(Netlink *nl, uint16_t group_id);
void collect_close_netlink(Netlink *nl);
void *collect_worker(void *targs);
void state_init(State **s, Netlink *nl, Global *g);
void state_free(State *s);

#endif // _COLLECT_H
