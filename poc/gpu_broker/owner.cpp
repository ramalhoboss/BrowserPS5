/* Copyright (C) 2026 BrowserPS5 contributors
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 3, or (at your option) any
 * later version.
 *
 * The GPU's owner (title PPSA97010): PS5_Vulkan's GPU broker (tooling/
 * gpu-broker at baaf3ad, LGPL-2.1-or-later) on the title's GPU and display,
 * for clients in payload processes. A title is the only process the system
 * gives the GPU; a payload that wants it (WebKit's processes, later) asks the
 * broker. Clients connect over TCP on 127.0.0.1:9040 and get their memory as
 * named POSIX shared memory (packet_io_tcp.c; why: docs/02, "GPU com o
 * PS5_Vulkan").
 *
 * One client at a time: a new connection closes the previous one. The title
 * never exits (the broker's rule: the system closes it from the PS menu).
 * Its report goes to /app0/owner.txt and to klog.
 */
#include "server.h"
#include "packet_io_tcp.h"

#include <ps5platform/kernel.h>
#include <ps5platform/klog.h>

#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

extern "C" int sceKernelUsleep(unsigned microseconds);

#define OWNER_PORT 9040
#define REPORT_PATH "/app0/owner.txt"
#define PICTURE_PATH "/app0/owner-frame.ppm"

static FILE *report_file;
static pthread_mutex_t report_lock = PTHREAD_MUTEX_INITIALIZER;

static void report(const char *format, ...) __attribute__((format(printf, 1, 2)));
static void report(const char *format, ...)
{
    char line[512];
    va_list arguments;
    va_start(arguments, format);
    int n = snprintf(line, sizeof(line), "[gpu_owner] ");
    vsnprintf(line + n, sizeof(line) - n - 1, format, arguments);
    va_end(arguments);
    strcat(line, "\n");
    pthread_mutex_lock(&report_lock);
    if (report_file)
        fputs(line, report_file);
    sceKernelDebugOutText(0, line);
    pthread_mutex_unlock(&report_lock);
}

[[noreturn]] static void park(void)
{
    for (;;)
        sceKernelUsleep(1000000);
}

/* The client's processes are payloads, not the broker's children: there is
 * no process to wait for, so a close is never held up by one. */
static bool absent(int) { return true; }

/* Each buffer a client maps: a POSIX shared-memory object with a name, which
 * the transport sends in place of the descriptor. Names from an earlier run
 * that was closed with some still there are taken over. */
static int named_alloc(size_t bytes)
{
    static unsigned next;
    static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_lock(&lock);
    const unsigned number = next++;
    pthread_mutex_unlock(&lock);
    char name[32];
    snprintf(name, sizeof(name), "/bps5gpu-%u", number);
    int fd = shm_open(name, O_RDWR | O_CREAT | O_EXCL, 0666);
    if (fd < 0 && errno == EEXIST) {
        shm_unlink(name);
        fd = shm_open(name, O_RDWR | O_CREAT | O_EXCL, 0666);
    }
    if (fd < 0) {
        report("shm_open %s: %s", name, strerror(errno));
        return -1;
    }
    if (ftruncate(fd, (off_t)bytes) || ps5_gpu_packet_name(fd, name)) {
        report("shared memory %s of %zu bytes: %s", name, bytes, strerror(errno));
        close(fd);
        shm_unlink(name);
        return -1;
    }
    return fd;
}

/* A picture of the frame the display showed, read back: a few of its pixels,
 * for the report (the TV shows the rest). */
static void describe_picture(void)
{
    struct stat st;
    const off_t expected = 15 + 1920 * 1080 * 3; /* "P6\n1920 1080\n255\n" */
    for (unsigned n = 0; n < 50; ++n) {
        if (!stat(PICTURE_PATH, &st) && st.st_size >= expected)
            break;
        sceKernelUsleep(100000);
    }
    FILE *f = fopen(PICTURE_PATH, "rb");
    if (!f) {
        report("picture: %s: %s", PICTURE_PATH, strerror(errno));
        return;
    }
    unsigned width = 0, height = 0, depth = 0;
    if (fscanf(f, "P6 %u %u %u", &width, &height, &depth) != 3 || fgetc(f) == EOF || !width || !height) {
        report("picture: not a P6 picture");
        fclose(f);
        return;
    }
    const long data = ftell(f);
    const struct { const char *where; unsigned x, y; } points[] = {
        {"top-left", 8, 8}, {"centre", width / 2, height / 2}, {"bottom-right", width - 8, height - 8},
        {"left of centre", width / 2 - height / 4, height / 2},
    };
    for (const auto &p : points) {
        unsigned char rgb[3] = {0, 0, 0};
        fseek(f, data + ((long)p.y * width + p.x) * 3, SEEK_SET);
        if (fread(rgb, 1, 3, f) == 3)
            report("picture %ux%u: %s (%u,%u) rgb %u %u %u", width, height, p.where, p.x, p.y, rgb[0], rgb[1], rgb[2]);
    }
    fclose(f);
    unlink(PICTURE_PATH);
}

/* Every 5 s, the frames the display has shown; once a client has shown 120,
 * a picture of the next one. */
static void *watch(void *)
{
    unsigned long long last = 0, base = 0;
    bool pictured = false;
    for (;;) {
        sceKernelUsleep(5000000);
        const unsigned long long flips = ps5_gpu_broker_flips();
        if (flips != last)
            report("display: %llu frames shown (+%llu in 5 s)", flips, flips - last);
        if (!base && flips)
            base = flips;
        last = flips;
        if (!pictured && base && flips - base >= 120 && ps5_gpu_broker_capture_next(PICTURE_PATH)) {
            pictured = true;
            describe_picture();
        }
    }
    return nullptr;
}

int main()
{
    report_file = fopen(REPORT_PATH, "w");
    if (report_file)
        setvbuf(report_file, nullptr, _IOLBF, 0);
    report("owner pid %d: the GPU broker for payload clients on 127.0.0.1:%d", getpid(), OWNER_PORT);

    ps5_gpu_broker_set_shared_memory(named_alloc, nullptr);
    if (!ps5_gpu_broker_start("/app0", false, true, absent)) {
        report("FAIL ps5_gpu_broker_start (its log: /app0/gpu-broker-results.txt)");
        park();
    }
    report("broker started");

    const int listener = socket(AF_INET, SOCK_STREAM, 0);
    const int yes = 1;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(OWNER_PORT);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (listener < 0 || bind(listener, (struct sockaddr *)&address, sizeof(address)) || listen(listener, 4)) {
        report("FAIL listen on 127.0.0.1:%d: %s", OWNER_PORT, strerror(errno));
        park();
    }
    report("listening on 127.0.0.1:%d", OWNER_PORT);
    {
        /* What the transport avoids (packet_io_tcp.c): its first console run
         * duplicated the client's socket and the broker refused the client. */
        const int copy = dup(listener);
        report("dup of a socket in this title: %s", copy >= 0 ? "allowed" : strerror(errno));
        if (copy >= 0)
            close(copy);
    }

    pthread_t watcher;
    pthread_create(&watcher, nullptr, watch, nullptr);

    int cookie = 0;
    for (;;) {
        const int client = accept(listener, nullptr, nullptr);
        if (client < 0) {
            if (errno != EINTR)
                report("accept: %s", strerror(errno));
            sceKernelUsleep(100000);
            continue;
        }
        setsockopt(client, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes));
        if (cookie) {
            /* Its buffers go once its last submissions have. */
            ps5_gpu_broker_close(cookie, 0);
            report("client %d closed", cookie);
            cookie = 0;
        }
        ps5_gpu_packet_adopt(client);
        const int ours = ps5_gpu_broker_open(&cookie);
        if (ours < 0) {
            report("FAIL ps5_gpu_broker_open: %s", strerror(errno));
            close(client);
            cookie = 0;
            continue;
        }
        close(ours);
        report("client %d connected", cookie);
    }
}
