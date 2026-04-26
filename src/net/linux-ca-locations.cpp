/*
SRC-Link
Copyright (C) 2024 OPENSPHERE Inc. info@opensphere.co.jp

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

#include "linux-ca-locations.hpp"

#ifdef __linux__

#include <sys/stat.h>

// Default targets the layout used by Debian / Ubuntu / Arch. Override via the CMake cache
// variable (e.g. -DSRC_LINK_LINUX_CA_PATH=/etc/pki/tls/certs/ca-bundle.crt for RHEL/Fedora,
// -DSRC_LINK_LINUX_CA_PATH=/etc/ssl/cert.pem for Alpine, or any other absolute path).
#ifndef SRC_LINK_LINUX_CA_PATH
#define SRC_LINK_LINUX_CA_PATH "/etc/ssl/certs"
#endif

const LinuxCaLocation *findLinuxCaLocation()
{
    static LinuxCaLocation result = {SRC_LINK_LINUX_CA_PATH, false};
    struct stat st;
    if (stat(result.path, &st) != 0) {
        return nullptr;
    }
    if (S_ISDIR(st.st_mode)) {
        result.isDirectory = true;
        return &result;
    }
    if (S_ISREG(st.st_mode)) {
        result.isDirectory = false;
        return &result;
    }
    return nullptr;
}

#endif // __linux__
