// update_net.c (BrowserPS5 RN, 2026-11-17): the two things the browser's own updates ask of the network (update.h
// says what an update is): GitHub's list of the project's releases, and a release's installer, written to a file
// as it comes and checked against the size and the SHA-256 the list gave. libcurl with the console's certificates,
// https only, redirects to https only. Both block: called on a thread of their own.
#pragma once
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif

// The text at url (1 MiB at most), NUL-terminated, in *text (free() it) and its length in *len; 0 when the
// server answered 200. Otherwise -1: *status the HTTP code (0: no answer, *curl_err says why).
int update_net_feed(const char* url, const char* agent, char** text, size_t* len, int* status, int* curl_err);

// The file at url into path. 0 when all of it came, of exactly size bytes and with that SHA-256; otherwise the
// file is removed: -1 (it did not come: *status, *curl_err) or -2 (it came and is not that file). *got follows
// the bytes written; *stop set to 1 ends it.
int update_net_file(const char* url, const char* agent, const char* path, unsigned long long size,
                    const unsigned char sha256[32], volatile unsigned long long* got, volatile int* stop, int* status,
                    int* curl_err);

#ifdef __cplusplus
}
#endif
