#include "server.h"

#include <errno.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>

#include "poll_thread.h"
#include "io_utils.h"
#include "buffer.h"


struct server_socket_handler_context {
    struct poll_thread * poll_thread;
    struct tpool * thread_pool;
    int server_socket;
};

/*
static int socket_handler(
        void * context,
        char * line
) {
    printf("First line of request: \"%s\"\n", line);
    free(line);

    return 0;
}
*/

/* TODO handle connection closing */
static int handle_client_socket(
        struct server_socket_handler_context context,
        int socket,
        struct sockaddr_in6 addr
) {
    int socket_flags, ret = 0;
    struct buffer * buffer;

    socket_flags = fcntl(socket, F_GETFL, 0);
    if (socket_flags < 0) {
        ret = socket_flags;
        goto end;
    }

    if ((ret = fcntl(socket, F_SETFL, socket_flags | O_NONBLOCK))) {
        goto end;
    }

    buffer = buffer_new(BUFFER_DEFAULT_CAPACITY);
    if (!buffer) {
        ret = -ENOMEM;
        goto end;
    }

    /* TODO */

    printf("[%d] Connection from: %02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x\n",
            socket,
            addr.sin6_addr.s6_addr[0], addr.sin6_addr.s6_addr[1],
            addr.sin6_addr.s6_addr[2], addr.sin6_addr.s6_addr[3],
            addr.sin6_addr.s6_addr[4], addr.sin6_addr.s6_addr[5],
            addr.sin6_addr.s6_addr[6], addr.sin6_addr.s6_addr[7],
            addr.sin6_addr.s6_addr[8], addr.sin6_addr.s6_addr[9],
            addr.sin6_addr.s6_addr[10], addr.sin6_addr.s6_addr[11],
            addr.sin6_addr.s6_addr[12], addr.sin6_addr.s6_addr[13],
            addr.sin6_addr.s6_addr[14], addr.sin6_addr.s6_addr[15]);

    goto end;

/* free_buffer: */
    buffer_delete(buffer);

end:
    return ret;
}

static int server_socket_handler(
        struct server_socket_handler_context * context,
        struct poll_thread_event event,
        int descriptor
) {
    struct sockaddr_in6 addr;
    socklen_t addrlen = sizeof(addr);
    int socket, ret = 0;

    socket = accept(context->server_socket, (struct sockaddr *) &addr, &addrlen);
    if (socket < 0) {
        ret = -errno;
        goto free_context;
    }

    if (sizeof(addr) != addrlen) {
        ret = -EINVAL;
        goto free_context;
    }

    ret = poll_thread_continue(context->poll_thread, descriptor);
    if (ret) {
        goto free_socket;
    }

    ret = handle_client_socket(*context, socket, addr);
    if (ret) {
        goto free_socket;
    }

    goto end;

free_socket:
    close(socket);

free_context:
    free(context);

end:
    return ret;
}

int server_main(struct server_config config) {
    struct server_socket_handler_context * server_socket_handler_context;
    struct poll_thread * poll_thread;
    int server_socket, ret = 0;
    tpool_t * thread_pool;

    struct sockaddr_in6 addr = { AF_INET6 };
    addr.sin6_port = htons(config.server_port);
    addr.sin6_addr = config.server_addr;

    server_socket = socket(AF_INET6, SOCK_STREAM | SOCK_NONBLOCK, /* TCP */ 6);
    if (server_socket < 0) {
        ret = -errno;
        goto end;
    }

    ret = bind(server_socket, (struct sockaddr *) &addr, sizeof(addr));
    if (ret < 0) {
        goto free_server_socket;
    }

    ret = listen(server_socket, 255);
    if (ret < 0) {
        goto free_server_socket;
    }

    thread_pool = tpool_create(8);

    poll_thread = poll_thread_new(thread_pool);
    if (!poll_thread) {
        ret = -errno;
        goto free_thread_pool;
    }

    server_socket_handler_context = malloc(sizeof(struct server_socket_handler_context));
    if (!server_socket_handler_context) {
        ret = -ENOMEM;
        goto free_poll_thread;
    }

    server_socket_handler_context->poll_thread = poll_thread;
    server_socket_handler_context->thread_pool = thread_pool;
    server_socket_handler_context->server_socket = server_socket;

    ret = poll_thread_register(poll_thread, server_socket, POLLIN,
            poll_thread_handler(server_socket_handler_context, server_socket_handler));
    if (ret) {
        goto free_server_socket_handler_context;
    }

    ret = poll_thread_run(poll_thread);

free_server_socket_handler_context:
    free(server_socket_handler_context);

free_poll_thread:
    poll_thread_delete(poll_thread);

free_thread_pool:
    tpool_destroy(thread_pool);

free_server_socket:
    close(server_socket);

end:
    return ret;
}
