/* Copyright (C) 2026 BrowserPS5 contributors */
/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * packet_io_tcp.c on the PC (no PS5): a loopback TCP pair, packets with and
 * without a named shared-memory object, the object's bytes seen through the
 * received descriptor, its name gone afterwards, and the end of the stream.
 *
 *   cc -std=gnu11 -Wall -Wextra -Werror -fsanitize=address,undefined \
 *      -I<PS5_Vulkan>/tooling/gpu-broker -I. test_tcp.c packet_io_tcp.c && ./a.out
 */
#include "packet_io.h"
#include "packet_io_tcp.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <unistd.h>

static int failures;
#define CHECK(x) do { if (!(x)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #x); ++failures; } } while (0)

int main(void)
{
    const int listener = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in address = {.sin_family = AF_INET, .sin_addr.s_addr = htonl(INADDR_LOOPBACK)};
    socklen_t length = sizeof(address);
    CHECK(!bind(listener, (struct sockaddr *)&address, sizeof(address)) && !listen(listener, 1));
    getsockname(listener, (struct sockaddr *)&address, &length);
    const int client = socket(AF_INET, SOCK_STREAM, 0);
    CHECK(!connect(client, (struct sockaddr *)&address, sizeof(address)));
    const int accepted = accept(listener, NULL, NULL);

    int pair[2];
    CHECK(ps5_gpu_packet_socketpair(pair, 64) == -1 && errno == ENOTSUP);
    ps5_gpu_packet_adopt(accepted);
    CHECK(!ps5_gpu_packet_socketpair(pair, 64) && pair[0] == accepted && pair[1] >= 0);
    close(pair[1]);
    const int server = pair[0];

    char message[64], received[64];
    int fds[2];
    unsigned count;
    memset(message, 0x5a, sizeof(message));
    CHECK(!ps5_gpu_packet_send(client, message, sizeof(message), NULL, 0));
    CHECK(ps5_gpu_packet_recv(server, received, sizeof(received), fds, &count) == 64 && count == 0 &&
          !memcmp(message, received, 64));

    /* A reply carrying a named object. */
    const char *name = "/bps5gpu-test-1";
    shm_unlink(name);
    const int object = shm_open(name, O_RDWR | O_CREAT | O_EXCL, 0666);
    CHECK(object >= 0 && !ftruncate(object, 16384));
    char *mine = mmap(NULL, 16384, PROT_READ | PROT_WRITE, MAP_SHARED, object, 0);
    strcpy(mine, "written by the owner");
    CHECK(ps5_gpu_packet_send(server, message, 64, &object, 1) == -1 && errno == EBADF);
    CHECK(!ps5_gpu_packet_name(object, name));
    message[0] = 1;
    CHECK(!ps5_gpu_packet_send(server, message, 64, &object, 1));
    CHECK(ps5_gpu_packet_recv(client, received, sizeof(received), fds, &count) == 64 && count == 1 && received[0] == 1);
    const int opened = fds[0];
    char *theirs = mmap(NULL, 16384, PROT_READ | PROT_WRITE, MAP_SHARED, opened, 0);
    CHECK(theirs != MAP_FAILED && !strcmp(theirs, "written by the owner"));
    strcpy(theirs + 100, "seen by the client");
    CHECK(!strcmp(mine + 100, "seen by the client"));
    CHECK(shm_open(name, O_RDWR, 0) == -1 && errno == ENOENT);
    /* A name is sent once. */
    CHECK(ps5_gpu_packet_send(server, message, 64, &object, 1) == -1 && errno == EBADF);

    /* Large packets cross whole; a short buffer is refused. */
    static char big[65536], back[65536];
    memset(big, 7, sizeof(big));
    CHECK(!ps5_gpu_packet_send(client, big, sizeof(big), NULL, 0));
    CHECK(ps5_gpu_packet_recv(server, back, sizeof(back), fds, &count) == 65536 && !memcmp(big, back, sizeof(big)));
    CHECK(!ps5_gpu_packet_send(client, message, 64, NULL, 0));
    CHECK(ps5_gpu_packet_recv(server, received, 32, fds, &count) == -1 && errno == EMSGSIZE);

    close(client);
    char rest[64];
    while (recv(server, rest, sizeof(rest), MSG_DONTWAIT) > 0) {
    }
    CHECK(ps5_gpu_packet_recv(server, received, sizeof(received), fds, &count) == 0);
    munmap(mine, 16384);
    munmap(theirs, 16384);
    close(object);
    close(opened);
    close(server);
    close(listener);
    printf("%s packet_io_tcp: %d failures\n", failures ? "FAIL" : "PASS", failures);
    return failures != 0;
}
