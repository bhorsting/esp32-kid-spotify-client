#include "spotify/cspot_bridge.h"

#include <stdio.h>

bool cspot_bridge_start(const char *username, const char *credentials_blob)
{
    (void)username;
    (void)credentials_blob;
    printf("[cspot] bridge stub — link feelfreelinux/cspot next\n");
    return false;
}

void cspot_bridge_stop(void) {}

bool cspot_bridge_is_running(void)
{
    return false;
}
