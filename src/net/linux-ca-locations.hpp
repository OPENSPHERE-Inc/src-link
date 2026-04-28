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

#pragma once

#ifdef __linux__

struct LinuxCaLocation {
    const char *path;
    bool isDirectory; // true → CURLOPT_CAPATH, false → CURLOPT_CAINFO
};

// Resolve the configured CA path (SRC_LINK_LINUX_CA_PATH compile definition; see CMakeLists.txt)
// and report whether it is a rehashed CA directory or a single bundle file. Returns nullptr if
// the configured path is missing or refers to neither a directory nor a regular file. The returned
// pointer references function-local static storage and remains valid for the process lifetime.
//
// Single-threaded use only (UI / Qt event-loop thread). Returns a pointer
// to a function-static record whose isDirectory field is rewritten on each
// call; concurrent invocation produces benign tearing but should be avoided.
const LinuxCaLocation *findLinuxCaLocation();

#endif // __linux__
