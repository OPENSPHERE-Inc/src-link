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

#define CURL_POLL_INTERVAL_MSECS 10
#define CURL_DEFAULT_TIMEOUT_MS 10000
#define CURL_MAX_RESPONSE_SIZE (16 * 1024 * 1024) // 16 MB

class CurlHttpClient : public QObject {
    Q_OBJECT

public:
    using ResponseCallback = std::function<void(HttpResponse)>;

    explicit CurlHttpClient(QObject *parent = nullptr);
    ~CurlHttpClient();

    /// HTTP GET request
    void get(
        const QUrl &url, const QMap<QByteArray, QByteArray> &headers, ResponseCallback callback,
        int timeoutMs = CURL_DEFAULT_TIMEOUT_MS
    );

    /// HTTP POST request with body
    void post(
        const QUrl &url, const QMap<QByteArray, QByteArray> &headers, const QByteArray &body,
        ResponseCallback callback, int timeoutMs = CURL_DEFAULT_TIMEOUT_MS
    );

    /// HTTP PUT request with body
    void put(
        const QUrl &url, const QMap<QByteArray, QByteArray> &headers, const QByteArray &body,
        ResponseCallback callback, int timeoutMs = CURL_DEFAULT_TIMEOUT_MS
    );

    /// HTTP DELETE request
    void del(
        const QUrl &url, const QMap<QByteArray, QByteArray> &headers, ResponseCallback callback,
        int timeoutMs = CURL_DEFAULT_TIMEOUT_MS
    );

    /// HTTP HEAD request
    void head(
        const QUrl &url, const QMap<QByteArray, QByteArray> &headers, ResponseCallback callback,
        int timeoutMs = CURL_DEFAULT_TIMEOUT_MS
    );

    /// Cancel all pending requests.
    /// If invokeCallbacks is true (default), each request's callback is called with OperationCanceled.
    /// If false, requests are silently cleaned up without callbacks (safe for use in destructors).
    void cancelAll(bool invokeCallbacks = true);

private:
    struct RequestContext {
        CURL *easy;
        QByteArray responseData;
        QMap<QByteArray, QByteArray> responseHeaders;
        struct curl_slist *requestHeaders;
        QByteArray requestBody;
        ResponseCallback callback;
    };

    CURLM *multi;
    QTimer *pollTimer;
    QList<RequestContext *> activeRequests;

    CURL *createEasyHandle(
        const QUrl &url, const QMap<QByteArray, QByteArray> &headers, RequestContext *ctx, int timeoutMs
    );
    void startRequest(CURL *easy, RequestContext *ctx);
    void checkCompleted();
    void cleanupRequest(RequestContext *ctx);

    static size_t writeCallback(char *ptr, size_t size, size_t nmemb, void *userdata);
    static size_t headerCallback(char *ptr, size_t size, size_t nmemb, void *userdata);
};
