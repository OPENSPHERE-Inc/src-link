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

#include <curl/curl.h>
#include <obs-module.h>

#include "http-error.hpp"

HttpError httpErrorFromStatusCode(int statusCode)
{
    // 1xx informational and 3xx redirect — treat as NoError. CURLOPT_FOLLOWLOCATION is now
    // disabled (see curl-http-client.cpp) to avoid leaking the Authorization header across
    // hosts, so 30x responses surface here directly as NoError with the redirect Location
    // available via response headers.
    // FIXME: 304 Not Modified arrives as NoError with an empty body. Callers that issue
    // conditional GETs (If-None-Match / If-Modified-Since) must distinguish 304 from 200
    // explicitly via HttpResponse::statusCode before treating an empty body as an error.
    if (statusCode >= 100 && statusCode <= 399) {
        return HttpError::NoError;
    }
    switch (statusCode) {
    case 401:
        return HttpError::AuthenticationRequired;
    case 403:
        return HttpError::AccessDenied;
    case 404:
        return HttpError::ContentNotFound;
    case 409:
        return HttpError::ContentConflict;
    default:
        if (statusCode >= 400 && statusCode <= 499) {
            return HttpError::UnknownContentError;
        }
        if (statusCode >= 500 && statusCode <= 599) {
            return HttpError::UnknownServerError;
        }
        return HttpError::NetworkError;
    }
}

// CURLE_PEER_FAILED_VERIFICATION historically aliases CURLE_SSL_CACERT (both value 60).
// If a future libcurl release ever splits them, the switch below would have a duplicate
// case label removed silently — catch that at compile time so we revisit the mapping.
static_assert(
    static_cast<int>(CURLE_PEER_FAILED_VERIFICATION) == static_cast<int>(CURLE_SSL_CACERT),
    "CURLE_PEER_FAILED_VERIFICATION is expected to alias CURLE_SSL_CACERT; "
    "update httpErrorFromCurlCode() to handle them separately."
);

HttpError httpErrorFromCurlCode(int curlCode)
{
    switch (static_cast<CURLcode>(curlCode)) {
    case CURLE_OK:
        return HttpError::NoError;
    case CURLE_COULDNT_CONNECT:
    case CURLE_COULDNT_RESOLVE_HOST:
    case CURLE_COULDNT_RESOLVE_PROXY:
        return HttpError::ConnectionRefused;
    case CURLE_OPERATION_TIMEDOUT:
        return HttpError::TimeoutError;
    case CURLE_SSL_CONNECT_ERROR:
    case CURLE_SSL_CERTPROBLEM:
    case CURLE_SSL_CIPHER:
    case CURLE_SSL_CACERT:
    case CURLE_SSL_CACERT_BADFILE:
    case CURLE_SSL_CRL_BADFILE:
    case CURLE_SSL_ISSUER_ERROR:
        // Note: CURLE_PEER_FAILED_VERIFICATION is an alias for CURLE_SSL_CACERT (value 60)
        return HttpError::SslError;
    case CURLE_SEND_ERROR:
    case CURLE_RECV_ERROR:
    case CURLE_GOT_NOTHING:
        return HttpError::NetworkError;
    case CURLE_TOO_MANY_REDIRECTS:
        return HttpError::TooManyRedirects;
    case CURLE_ABORTED_BY_CALLBACK:
        return HttpError::OperationCanceled;
    default:
        return HttpError::NetworkError;
    }
}

QString httpErrorToString(HttpError error)
{
    switch (error) {
    case HttpError::NoError:
        return QStringLiteral("No error");
    case HttpError::ConnectionRefused:
        return QStringLiteral("Connection refused");
    case HttpError::TimeoutError:
        return QStringLiteral("Request timed out");
    case HttpError::SslError:
        return QStringLiteral("SSL/TLS error");
    case HttpError::AuthenticationRequired:
        return QStringLiteral("Authentication required (HTTP 401)");
    case HttpError::AccessDenied:
        return QStringLiteral("Access denied (HTTP 403)");
    case HttpError::ContentNotFound:
        return QStringLiteral("Content not found (HTTP 404)");
    case HttpError::ContentConflict:
        return QStringLiteral("Content conflict (HTTP 409)");
    case HttpError::UnknownContentError:
        return QStringLiteral("Client error (HTTP 4xx)");
    case HttpError::UnknownServerError:
        return QStringLiteral("Server error (HTTP 5xx)");
    case HttpError::NetworkError:
        return QStringLiteral("Network error");
    case HttpError::OperationCanceled:
        return QStringLiteral("Operation canceled");
    case HttpError::ResponseTooLarge:
        return QStringLiteral("Response exceeded size limit");
    case HttpError::TooManyRedirects:
        return QStringLiteral("Too many redirects");
    }
    return QStringLiteral("Unknown error");
}
