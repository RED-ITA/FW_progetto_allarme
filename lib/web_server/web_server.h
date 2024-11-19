#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#ifdef __cplusplus
extern "C" {
#endif

void web_server_start(void);
void web_server_stop(void);

// Dichiara la funzione url_decode
void url_decode(char *str);

#ifdef __cplusplus
}
#endif

#endif // WEB_SERVER_H
