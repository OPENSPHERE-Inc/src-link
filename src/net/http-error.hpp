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

#include <QString>

enum class HttpError {
    NoError = 0,
    ConnectionRefused,
    TimeoutError,
    SslError,
    AuthenticationRequired, // HTTP 401
    AccessDenied,           // HTTP 403
    ContentNotFound,        // HTTP 404
    ContentConflict,        // HTTP 409
    UnknownContentError,    // Other 4xx
    UnknownServerError,     // 5xx
    NetworkError,           // Connection-level errors
    OperationCanceled,
};

/// Convert HTTP status code to HttpError
HttpError httpErrorFromStatusCode(int statusCode);

/// Convert curl error code to HttpError
HttpError httpErrorFromCurlCode(int curlCode);

/// Get human-readable description
QString httpErrorToString(HttpError error);
