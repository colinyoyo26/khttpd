#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kthread.h>
#include <linux/sched/signal.h>
#include <linux/tcp.h>

#include "http_parser.h"
#include "http_server.h"

#define CRLF "\r\n"

#define HTTP_RESPONSE_200                                      \
    ""                                                         \
    "HTTP/1.1 200 OK" CRLF "Server: " KBUILD_MODNAME CRLF      \
    "Content-Type: text/plain" CRLF "Content-Length: %lu" CRLF \
    "Connection: Close" CRLF CRLF
#define HTTP_RESPONSE_200_KEEPALIVE                            \
    ""                                                         \
    "HTTP/1.1 200 OK" CRLF "Server: " KBUILD_MODNAME CRLF      \
    "Content-Type: text/plain" CRLF "Content-Length: %lu" CRLF \
    "Connection: Keep-Alive" CRLF CRLF
#define HTTP_RESPONSE_501                                              \
    ""                                                                 \
    "HTTP/1.1 501 Not Implemented" CRLF "Server: " KBUILD_MODNAME CRLF \
    "Content-Type: text/plain" CRLF "Content-Length: 21" CRLF          \
    "Connection: Close" CRLF CRLF "501 Not Implemented" CRLF
#define HTTP_RESPONSE_501_KEEPALIVE                                    \
    ""                                                                 \
    "HTTP/1.1 501 Not Implemented" CRLF "Server: " KBUILD_MODNAME CRLF \
    "Content-Type: text/plain" CRLF "Content-Length: 21" CRLF          \
    "Connection: KeepAlive" CRLF CRLF "501 Not Implemented" CRLF

#define HELLOW "Hello World!" CRLF


#define RECV_BUFFER_SIZE 4096

struct khttp_service daemon = {.is_stopped = false,
                               .lock = __SPIN_LOCK_UNLOCKED(daemon.lock)};
extern struct workqueue_struct *khttp_wq;

struct http_request {
    struct socket *socket;
    enum http_method method;
    char request_url[128];
    int complete;
};

static int http_server_recv(struct socket *sock, char *buf, size_t size)
{
    struct kvec iov = {.iov_base = (void *) buf, .iov_len = size};
    struct msghdr msg = {.msg_name = 0,
                         .msg_namelen = 0,
                         .msg_control = NULL,
                         .msg_controllen = 0,
                         .msg_flags = 0};
    return kernel_recvmsg(sock, &msg, &iov, 1, size, msg.msg_flags);
}

static int http_server_send(struct socket *sock, const char *buf, size_t size)
{
    struct msghdr msg = {.msg_name = NULL,
                         .msg_namelen = 0,
                         .msg_control = NULL,
                         .msg_controllen = 0,
                         .msg_flags = 0};
    int done = 0;
    while (done < size) {
        struct kvec iov = {
            .iov_base = (void *) ((char *) buf + done),
            .iov_len = size - done,
        };
        int length = kernel_sendmsg(sock, &msg, &iov, 1, iov.iov_len);
        if (length < 0) {
            pr_err("write error: %d\n", length);
            break;
        }
        done += length;
    }
    return done;
}

static int http_server_response(struct http_request *request, int keep_alive)
{
    char *response, *be_free = NULL;
    char buf[128];

    pr_info("requested_url = %s\n", request->request_url);
    if (request->method != HTTP_GET)
        response = keep_alive ? HTTP_RESPONSE_501_KEEPALIVE : HTTP_RESPONSE_501;
    else {
        char *url = request->request_url;
        if (!strncmp(url, "/fib/", 5)) {
            long long k;
            if (kstrtoll(url + 5, 10, &k)) {
                response = keep_alive ? HTTP_RESPONSE_501_KEEPALIVE
                                      : HTTP_RESPONSE_501;
                goto out;
            }
            response = fib_sequence(k);
            if (keep_alive)
                snprintf(buf, 128, HTTP_RESPONSE_200_KEEPALIVE,
                         strlen(response));
            else
                snprintf(buf, 128, HTTP_RESPONSE_200, strlen(response));
            int buf_len = strlen(buf);
            strncat(response, CRLF, strlen(CRLF));
            memmove(response + buf_len, response, strlen(response));
            memcpy(response, buf, buf_len);
            be_free = response;
        } else {
            if (keep_alive)
                snprintf(buf, 128, HTTP_RESPONSE_200_KEEPALIVE HELLOW, 12lu);
            else
                snprintf(buf, 128, HTTP_RESPONSE_200 HELLOW, 12lu);
            response = buf;
        }
    }

out:
    http_server_send(request->socket, response, strlen(response));
    kfree(be_free);
    return 0;
}

static int http_parser_callback_message_begin(http_parser *parser)
{
    struct http_request *request = parser->data;
    struct socket *socket = request->socket;
    memset(request, 0x00, sizeof(struct http_request));
    request->socket = socket;
    return 0;
}

static int http_parser_callback_request_url(http_parser *parser,
                                            const char *p,
                                            size_t len)
{
    struct http_request *request = parser->data;
    strncat(request->request_url, p, len);
    return 0;
}

static int http_parser_callback_header_field(http_parser *parser,
                                             const char *p,
                                             size_t len)
{
    return 0;
}

static int http_parser_callback_header_value(http_parser *parser,
                                             const char *p,
                                             size_t len)
{
    return 0;
}

static int http_parser_callback_headers_complete(http_parser *parser)
{
    struct http_request *request = parser->data;
    request->method = parser->method;
    return 0;
}

static int http_parser_callback_body(http_parser *parser,
                                     const char *p,
                                     size_t len)
{
    return 0;
}

static int http_parser_callback_message_complete(http_parser *parser)
{
    struct http_request *request = parser->data;
    http_server_response(request, http_should_keep_alive(parser));
    request->complete = 1;
    return 0;
}

static void free_work(struct khttp_worker *worker)
{
    kernel_sock_shutdown(worker->sock, SHUT_RDWR);
    sock_release(worker->sock);
    spin_lock(&daemon.lock);
    list_del(&worker->list);
    spin_unlock(&daemon.lock);
    kfree(worker);
}

static void http_server_worker(struct work_struct *work)
{
    char *buf;
    struct http_parser parser;
    struct http_parser_settings setting = {
        .on_message_begin = http_parser_callback_message_begin,
        .on_url = http_parser_callback_request_url,
        .on_header_field = http_parser_callback_header_field,
        .on_header_value = http_parser_callback_header_value,
        .on_headers_complete = http_parser_callback_headers_complete,
        .on_body = http_parser_callback_body,
        .on_message_complete = http_parser_callback_message_complete};
    struct http_request request;
    struct khttp_worker *worker =
        container_of(work, struct khttp_worker, khttp_work);
    struct socket *socket = (struct socket *) worker->sock;

    allow_signal(SIGKILL);
    allow_signal(SIGTERM);

    buf = kmalloc(RECV_BUFFER_SIZE, GFP_KERNEL);
    if (!buf) {
        pr_err("can't allocate memory!\n");
        return;
    }

    request.socket = socket;
    http_parser_init(&parser, HTTP_REQUEST);
    parser.data = &request;
    while (!daemon.is_stopped) {
        int ret = http_server_recv(socket, buf, RECV_BUFFER_SIZE - 1);
        if (ret <= 0) {
            if (ret)
                pr_err("recv error: %d\n", ret);
            break;
        }
        http_parser_execute(&parser, &setting, buf, ret);
        if (request.complete && !http_should_keep_alive(&parser))
            break;
    }

    kfree(buf);
    free_work(worker);
}

static struct work_struct *create_work(struct socket *sock)
{
    struct khttp_worker *worker;

    if (!(worker = kmalloc(sizeof(struct khttp_worker), GFP_KERNEL)))
        return NULL;

    worker->sock = sock;
    INIT_WORK(&worker->khttp_work, http_server_worker);
    spin_lock(&daemon.lock);
    list_add(&worker->list, &daemon.worker);
    spin_unlock(&daemon.lock);
    return &worker->khttp_work;
}

static void flush_works(void)
{
    struct khttp_worker *l = NULL, *tar = NULL;
    list_for_each_entry_safe (tar, l, &daemon.worker, list) {
        kernel_sock_shutdown(tar->sock, SHUT_RDWR);
        flush_work(&tar->khttp_work);
    }
}

int http_server_daemon(void *arg)
{
    struct socket *socket;
    struct work_struct *work;
    struct http_server_param *param = (struct http_server_param *) arg;

    allow_signal(SIGKILL);
    allow_signal(SIGTERM);

    INIT_LIST_HEAD(&daemon.worker);

    while (!kthread_should_stop()) {
        int err = kernel_accept(param->listen_socket, &socket, 0);
        if (err < 0) {
            if (signal_pending(current))
                break;
            pr_err("kernel_accept() error: %d\n", err);
            continue;
        }

        if (unlikely(!(work = create_work(socket)))) {
            pr_err("create_work() error\n");
            kernel_sock_shutdown(socket, SHUT_RDWR);
            sock_release(socket);
            break;
        }

        queue_work(khttp_wq, work);

        if (IS_ERR(work)) {
            pr_err("can't create more worker process\n");
            continue;
        }
    }

    daemon.is_stopped = true;
    flush_works();

    return 0;
}
