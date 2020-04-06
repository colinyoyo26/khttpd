#ifndef KHTTPD_HTTP_SERVER_H
#define KHTTPD_HTTP_SERVER_H

#include <net/sock.h>

struct http_server_param {
    struct socket *listen_socket;
};

struct khttp_service {
    bool is_stopped;
    struct list_head worker;
};

struct khttp_worker {
    struct socket *sock;
    struct list_head list;
    struct work_struct khttp_work;
};

extern int http_server_daemon(void *arg);
char *fib_sequence(long long k);

#endif
