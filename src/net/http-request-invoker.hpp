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
#include <QMutex>
#include <QUrl>
#include <QByteArray>
#include <QMap>

#include "http-error.hpp"

#define HTTP_DEFAULT_TIMEOUT_MSECS (10 * 1000)

class CurlHttpClient;
class OAuth2Client;
class HttpRequestInvoker;

/// Sequential request queue (replaces RequestSequencer)
class HttpRequestSequencer : public QObject {
    Q_OBJECT

    friend class HttpRequestInvoker;

    CurlHttpClient *httpClient;
    OAuth2Client *oauth2Client;
    QList<HttpRequestInvoker *> requestQueue;
    QMutex mutex;

public:
    explicit HttpRequestSequencer(CurlHttpClient *httpClient, OAuth2Client *oauth2Client, QObject *parent = nullptr);
    ~HttpRequestSequencer();
};

/// One-shot HTTP request invoker with automatic Bearer token injection and 401 retry (replaces RequestInvoker)
class HttpRequestInvoker : public QObject {
    Q_OBJECT

    HttpRequestSequencer *sequencer;
    bool retried;

    QMap<QByteArray, QByteArray> mergeAuthHeaders(const QMap<QByteArray, QByteArray> &headers);
    void handleResponse(int statusCode, HttpError error, const QByteArray &data, std::function<void()> retryFunc);

signals:
    void finished(HttpError error, QByteArray data);

public:
    /// Sequential invocation (uses shared sequencer)
    explicit HttpRequestInvoker(HttpRequestSequencer *sequencer, QObject *parent = nullptr);

    /// Parallel invocation (creates own sequencer as child)
    explicit HttpRequestInvoker(CurlHttpClient *httpClient, OAuth2Client *oauth2Client, QObject *parent = nullptr);

    ~HttpRequestInvoker();

    template<class Func> void queue(Func invoker);

    /// Refresh OAuth2 token
    void refresh();

    /// HTTP GET request
    void get(
        const QUrl &url, const QMap<QByteArray, QByteArray> &headers = {},
        int timeout = HTTP_DEFAULT_TIMEOUT_MSECS
    );

    /// HTTP POST request with body
    void post(
        const QUrl &url, const QMap<QByteArray, QByteArray> &headers, const QByteArray &data,
        int timeout = HTTP_DEFAULT_TIMEOUT_MSECS
    );

    /// HTTP PUT request with body
    void put(
        const QUrl &url, const QMap<QByteArray, QByteArray> &headers, const QByteArray &data,
        int timeout = HTTP_DEFAULT_TIMEOUT_MSECS
    );

    /// HTTP DELETE request
    void deleteResource(
        const QUrl &url, const QMap<QByteArray, QByteArray> &headers = {},
        int timeout = HTTP_DEFAULT_TIMEOUT_MSECS
    );

    /// HTTP HEAD request
    void head(
        const QUrl &url, const QMap<QByteArray, QByteArray> &headers = {},
        int timeout = HTTP_DEFAULT_TIMEOUT_MSECS
    );
};
