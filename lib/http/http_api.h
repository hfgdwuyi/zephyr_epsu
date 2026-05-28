#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* Start/register HTTP service and resources (must be called after network is ready). */
void http_api_start(void);

#ifdef __cplusplus
}
#endif