#include "sync.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <synchapi.h>

signal_event_handle signal_event_create(b8 signaled)
{
    HANDLE event = CreateEventA(NULL, TRUE, signaled, NULL);
    return (signal_event_handle)event;
}

void signal_event_set(signal_event_handle signal_event)
{
    HANDLE event = (HANDLE)signal_event;
    SetEvent(event);
}

void signal_event_reset(signal_event_handle signal_event)
{
    HANDLE event = (HANDLE)signal_event;
    ResetEvent(event);
}
