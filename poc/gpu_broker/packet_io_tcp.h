/* Copyright (C) 2026 BrowserPS5 contributors */
/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * What packet_io_tcp.c adds to the GPU broker's packet_io.h (PS5_Vulkan,
 * tooling/gpu-broker): a TCP connection for the broker to serve, and names
 * for the shared memory it hands out.
 */
#ifndef BROWSERPS5_PACKET_IO_TCP_H
#define BROWSERPS5_PACKET_IO_TCP_H
#ifdef __cplusplus
extern "C" {
#endif
/* The next ps5_gpu_packet_socketpair gives this connected socket as pair[0]
 * (the broker's end) and an unconnected socket as pair[1] (which the owner
 * closes). Without one, socketpair fails with ENOTSUP. */
void ps5_gpu_packet_adopt(int socket);
/* fd is a POSIX shared-memory object opened by name: when it is sent, the
 * name goes in its place. 0, or -1 with errno. */
int ps5_gpu_packet_name(int fd, const char *name);
#ifdef __cplusplus
}
#endif
#endif
