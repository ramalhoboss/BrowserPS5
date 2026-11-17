/* Copyright (C) 2026 BrowserPS5 contributors

This program is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation; either version 3, or (at your option) any
later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; see the file COPYING. If not, see
<http://www.gnu.org/licenses/>.  */

// libresolv.a for GLib's GIO (build-media.sh): libc functions GIO uses that
// the SDK's libc declares but does not have.
//  - res_query() and dn_expand(): GIO only needs them for DNS records other
//    than addresses (SRV, MX, TXT), which neither GStreamer nor WebKit asks
//    for (WebKit resolves names in its NetworkProcess, with curl). Every
//    query finds nothing.
//  - setfsent(), getfsent() and endfsent(): GIO's list of mount points from
//    /etc/fstab. There is no fstab: the list is empty.

#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/nameser.h>
#include <fstab.h>
#include <resolv.h>
#include <netdb.h>

int
res_query(const char* name, int class, int type, u_char* answer, int size) {
  (void)name;
  (void)class;
  (void)type;
  (void)answer;
  (void)size;
  h_errno = HOST_NOT_FOUND;
  return -1;
}


int
dn_expand(const u_char* message, const u_char* end, const u_char* compressed,
          char* name, int size) {
  (void)message;
  (void)end;
  (void)compressed;
  if(size > 0) {
    name[0] = 0;
  }
  return -1;
}


int
setfsent(void) {
  return 0;
}


struct fstab*
getfsent(void) {
  return 0;
}


void
endfsent(void) {
}
