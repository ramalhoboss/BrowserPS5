/* Copyright (C) 2026 BrowserPS5 contributors */
/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * The GPU broker's packet transport (packet_io.h in PS5_Vulkan's
 * tooling/gpu-broker) over TCP on 127.0.0.1, for an owner title and client
 * payloads. Named AF_UNIX sockets are refused to both (EOPNOTSUPP,
 * poc/title_ipc), so no descriptor can pass between them. A descriptor goes
 * as the name of a POSIX shared-memory object instead, which both can open
 * (poc/title_ipc): the owner names each object it makes
 * (ps5_gpu_packet_name), the receiver opens it by that name and removes the
 * name, and the sender removes what is left of its names at its next receive
 * on that socket (by then the peer has answered, so it has opened them).
 *
 * Each packet is a frame: a header (magic, payload bytes, how many names, the
 * names) and the payload. The broker's own packet_io.c is replaced by this
 * file, in the owner and in the client archive alike.
 */
#include "packet_io.h"
#include "packet_io_tcp.h"
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <unistd.h>

#define FRAME_MAGIC 0x35535042u /* "BPS5" */
#define NAME_BYTES 32
#define PAYLOAD_MAX 65536

struct frame {
    uint32_t magic, bytes, count, reserved;
    char names[2][NAME_BYTES];
};

/* Names by descriptor until sent; names sent, by socket, until removed. An
 * entry is free when its name is empty. */
#define NAMED_MAX 64
#define PENDING_MAX 64
static struct { int fd; char name[NAME_BYTES]; } named[NAMED_MAX];
static struct { int socket; char name[NAME_BYTES]; } pending[PENDING_MAX];
static int adopted = -1;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

/* Removes the names sent on socket (any socket when socket < 0). */
static void unlink_pending(int socket)
{
    char names[PENDING_MAX][NAME_BYTES];
    unsigned count = 0;
    pthread_mutex_lock(&lock);
    for (unsigned i = 0; i < PENDING_MAX; ++i)
        if (pending[i].name[0] && (socket < 0 || pending[i].socket == socket)) {
            memcpy(names[count++], pending[i].name, NAME_BYTES);
            pending[i].name[0] = 0;
        }
    pthread_mutex_unlock(&lock);
    for (unsigned i = 0; i < count; ++i)
        shm_unlink(names[i]);
}

void ps5_gpu_packet_adopt(int socket)
{
    pthread_mutex_lock(&lock);
    adopted = socket;
    pthread_mutex_unlock(&lock);
    /* A socket number seen again: the names sent on the closed one go. */
    unlink_pending(socket);
}

int ps5_gpu_packet_name(int fd, const char *name)
{
    if (fd < 0 || !name || !*name || strlen(name) >= NAME_BYTES) {
        errno = EINVAL;
        return -1;
    }
    char stale[NAME_BYTES] = "";
    int slot = -1;
    pthread_mutex_lock(&lock);
    for (int i = 0; i < NAMED_MAX; ++i)
        if (named[i].name[0] && named[i].fd == fd) {
            /* fd was closed unsent and its number given out again. */
            memcpy(stale, named[i].name, NAME_BYTES);
            slot = i;
            break;
        }
    for (int i = 0; slot < 0 && i < NAMED_MAX; ++i)
        if (!named[i].name[0])
            slot = i;
    if (slot >= 0) {
        named[slot].fd = fd;
        strcpy(named[slot].name, name);
    }
    pthread_mutex_unlock(&lock);
    if (stale[0])
        shm_unlink(stale);
    if (slot < 0) {
        errno = ENOSPC;
        return -1;
    }
    return 0;
}

static int socket_placeholder(void)
{
    return socket(AF_INET, SOCK_STREAM, 0);
}

int ps5_gpu_packet_socketpair(int pair[2], size_t packet_size)
{
    if (packet_size > PAYLOAD_MAX) {
        errno = E2BIG;
        return -1;
    }
    pthread_mutex_lock(&lock);
    const int socket = adopted;
    adopted = -1;
    pthread_mutex_unlock(&lock);
    if (socket < 0) {
        errno = ENOTSUP;
        return -1;
    }
    /* The client's end is in the client's process already: the owner gets a
     * socket of no use in its place, to close. Not a dup of this one: a
     * title may not duplicate descriptors (the first console run of
     * PPSA97010 failed here, which the broker reports as EAGAIN). */
    const int placeholder = socket_placeholder();
    if (placeholder < 0)
        return -1;
    pair[0] = socket;
    pair[1] = placeholder;
    return 0;
}

/* Takes fd's name for a packet on socket: it is pending there from now. */
static int take_name(int socket, int fd, char name[NAME_BYTES])
{
    int found = 0;
    pthread_mutex_lock(&lock);
    for (unsigned i = 0; i < NAMED_MAX && !found; ++i)
        if (named[i].name[0] && named[i].fd == fd) {
            memcpy(name, named[i].name, NAME_BYTES);
            named[i].name[0] = 0;
            found = 1;
        }
    if (found) {
        unsigned slot = 0;
        while (slot < PENDING_MAX && pending[slot].name[0])
            ++slot;
        if (slot < PENDING_MAX) {
            pending[slot].socket = socket;
            memcpy(pending[slot].name, name, NAME_BYTES);
        }
    }
    pthread_mutex_unlock(&lock);
    if (!found)
        errno = EBADF;
    return found ? 0 : -1;
}

int ps5_gpu_packet_send(int fd, const void *data, size_t size, const int *fds, unsigned count)
{
    if (count > 2 || size > PAYLOAD_MAX) {
        errno = EINVAL;
        return -1;
    }
    struct frame header;
    memset(&header, 0, sizeof(header));
    header.magic = FRAME_MAGIC;
    header.bytes = (uint32_t)size;
    header.count = count;
    for (unsigned i = 0; i < count; ++i)
        if (take_name(fd, fds[i], header.names[i]))
            return -1;
    struct iovec vec[2] = {{&header, sizeof(header)}, {(void *)data, size}};
    struct msghdr msg;
    memset(&msg, 0, sizeof(msg));
    msg.msg_iov = vec;
    msg.msg_iovlen = 2;
    size_t left = sizeof(header) + size;
    while (left) {
        const ssize_t sent = sendmsg(fd, &msg, MSG_NOSIGNAL);
        if (sent < 0 && errno == EINTR)
            continue;
        if (sent <= 0) {
            if (sent == 0)
                errno = EIO;
            return -1;
        }
        left -= (size_t)sent;
        size_t skip = (size_t)sent;
        while (msg.msg_iovlen && skip >= msg.msg_iov[0].iov_len) {
            skip -= msg.msg_iov[0].iov_len;
            ++msg.msg_iov;
            --msg.msg_iovlen;
        }
        if (msg.msg_iovlen) {
            msg.msg_iov[0].iov_base = (char *)msg.msg_iov[0].iov_base + skip;
            msg.msg_iov[0].iov_len -= skip;
        }
    }
    return 0;
}

/* bytes from fd, whole: their count, 0 at the end of the stream before the
 * first byte, or -1 (EIO when it ends inside them). */
static ssize_t read_all(int fd, void *data, size_t bytes)
{
    size_t got = 0;
    while (got < bytes) {
        const ssize_t n = recv(fd, (char *)data + got, bytes - got, 0);
        if (n < 0 && errno == EINTR)
            continue;
        if (n < 0)
            return -1;
        if (n == 0) {
            if (!got)
                return 0;
            errno = EIO;
            return -1;
        }
        got += (size_t)n;
    }
    return (ssize_t)got;
}

ssize_t ps5_gpu_packet_recv(int fd, void *data, size_t size, int fds[2], unsigned *count)
{
    *count = 0;
    fds[0] = fds[1] = -1;
    /* The peer answered whatever came before: the names it was sent are open. */
    unlink_pending(fd);
    struct frame header;
    const ssize_t got = read_all(fd, &header, sizeof(header));
    if (got <= 0)
        return got;
    if (header.magic != FRAME_MAGIC || header.bytes > PAYLOAD_MAX || header.count > 2 || header.bytes > size) {
        errno = EMSGSIZE;
        return -1;
    }
    if (header.bytes && read_all(fd, data, header.bytes) != (ssize_t)header.bytes) {
        if (!errno)
            errno = EIO;
        return -1;
    }
    for (unsigned i = 0; i < header.count; ++i) {
        char *name = header.names[i];
        name[NAME_BYTES - 1] = 0;
        const int object = name[0] == '/' ? shm_open(name, O_RDWR, 0) : -1;
        const int saved = errno;
        if (name[0] == '/')
            shm_unlink(name);
        if (object < 0) {
            for (unsigned n = 0; n < *count; ++n)
                close(fds[n]);
            *count = 0;
            fds[0] = fds[1] = -1;
            errno = name[0] == '/' ? saved : EINVAL;
            return -1;
        }
        fds[(*count)++] = object;
    }
    return (ssize_t)header.bytes;
}
