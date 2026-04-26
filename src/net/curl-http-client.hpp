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

#include <functional>

#include <QObject>
#include <QByteArray>
#include <QTimer>
#include <QMap>
#include <QUrl>

#include <curl/curl.h>

#include "http-response.hpp"

/// Asynchronous HTTP client backed by libcurl's multi interface.
///
/// All ResponseCallback invocations are delivered on the thread that owns this QObject
/// (typically the UI / main thread), driven by pollTimer's QTimer::timeout signal.
/// Callbacks therefore must NOT assume they run on a worker thread.
class CurlHttpClient : public QObject {
    Q_OBJECT

public:
    static constexpr int POLL_INTERVAL_MSECS = 10;
    static constexpr int DEFAULT_TIMEOUT_MS = 10000;
    static constexpr int MAX_RESPONSE_SIZE = 16 * 1024 * 1024; // 16 MB

    using ResponseCallback = std::function<void(HttpResponse)>;

    explicit CurlHttpClient(QObject *parent = nullptr);
    ~CurlHttpClient();

    /// @warning The callback may capture `this` of the caller. If the caller can be destroyed
    /// before the request completes, it must either: (a) call cancelAllSilently() in its destructor
    /// to prevent dangling callbacks, or (b) use QPointer guards in the callback lambda.
    /// See OAuth2Client for pattern (b).

    /// HTTP GET request
    void
    get(const QUrl &url, const QMap<QByteArray, QByteArray> &headers, ResponseCallback callback,
        int timeoutMs = DEFAULT_TIMEOUT_MS);

    /// HTTP POST request with body
    void post(
        const QUrl &url, const QMap<QByteArray, QByteArray> &headers, const QByteArray &body, ResponseCallback callback,
        int timeoutMs = DEFAULT_TIMEOUT_MS
    );

    /// HTTP PUT request with body
    void
    put(const QUrl &url, const QMap<QByteArray, QByteArray> &headers, const QByteArray &body, ResponseCallback callback,
        int timeoutMs = DEFAULT_TIMEOUT_MS);

    /// HTTP DELETE request
    void
    del(const QUrl &url, const QMap<QByteArray, QByteArray> &headers, ResponseCallback callback,
        int timeoutMs = DEFAULT_TIMEOUT_MS);

    /// HTTP HEAD request
    void head(
        const QUrl &url, const QMap<QByteArray, QByteArray> &headers, ResponseCallback callback,
        int timeoutMs = DEFAULT_TIMEOUT_MS
    );

    /// Cancel all pending requests and invoke each callback with OperationCanceled.
    /// @warning Do NOT call from a destructor where callers may already be partially destroyed —
    /// use cancelAllSilently() in that case.
    void cancelAll();

    /// Cancel all pending requests without invoking callbacks. Safe for use in destructors.
    /// @warning Downstream consumers will not be notified; only call when callers are being torn down.
    void cancelAllSilently();

private:
    struct RequestContext {
        CURL *easy = nullptr;
        QByteArray responseData;
        QMap<QByteArray, QByteArray> responseHeaders;
        struct curl_slist *requestHeaders = nullptr;
        QByteArray requestBody;
        ResponseCallback callback;
        bool responseTooLarge = false;
    };

    CURLM *multi;
    QTimer *pollTimer;
    QList<RequestContext *> activeRequests;

    CURL *
    createEasyHandle(const QUrl &url, const QMap<QByteArray, QByteArray> &headers, RequestContext *ctx, int timeoutMs);
    void startRequest(CURL *easy, RequestContext *ctx);
    void checkCompleted();
    void cleanupRequest(RequestContext *ctx);

    static size_t writeCallback(char *ptr, size_t size, size_t nmemb, void *userdata);
    static size_t headerCallback(char *ptr, size_t size, size_t nmemb, void *userdata);
};
