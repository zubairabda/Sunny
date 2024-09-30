#ifndef SYNC_H
#define SYNC_H

#include "common.h"

typedef struct signal_event *signal_event_handle;

signal_event_handle signal_event_create(b8 signaled);
void signal_event_set(signal_event_handle signal_event);
void signal_event_reset(signal_event_handle signal_event);

#endif /* SYNC_H */
