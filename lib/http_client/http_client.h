#ifndef HTTP_CLIENT_H
#define HTTP_CLIENT_H

#ifdef __cplusplus
extern "C" {
#endif

void http_client_init(void *param);
void http_client_task(void *param);

#ifdef __cplusplus
}
#endif

#endif // HTTP_CLIENT_H
