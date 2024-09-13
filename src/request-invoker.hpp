/*
Source Link
Copyright (C) 2024 OPENSPHERE Inc. ifo@opensphere.co.jp

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

#include <QObject>

#include "api-client.hpp"

// This class introduces sequencial invocation of requests
class RequestInvoker : public QObject {
    Q_OBJECT

    O2Requestor *requestor;
    SourceLinkApiClient *apiClient;
    int requestId = -1; // -1: no request, -2: refresh, other: Proper request ID from O2Requestor

signals:
    void finished(QNetworkReply::NetworkError error, QByteArray data);

public:
    explicit RequestInvoker(SourceLinkApiClient *apiClient, QObject *parent = nullptr);
    ~RequestInvoker();

    template<class Func> inline void queue(Func invoker)
    {
        if (apiClient->getRequestQueue().isEmpty()) {
            apiClient->getRequestQueue().append(this);
            invoker();
        } else {
            // Reserve next invocation
            connect(apiClient->getRequestQueue().last(), &RequestInvoker::finished, invoker);
            apiClient->getRequestQueue().append(this);
        }
        obs_log(LOG_DEBUG, "Queue request: size=%d", apiClient->getRequestQueue().size());
    }

    /// Do token refresh
    inline void refresh()
    {
        queue([this]() {
            obs_log(LOG_DEBUG, "Invoke refresh token");
            requestId = -2;
            apiClient->refresh();
        });
    }

    /// Make a GET request.
    inline void get(const QNetworkRequest &req, int timeout = DEFAULT_TIMEOUT_MSECS)
    {
        queue([this, req, timeout]() { requestId = requestor->get(req, timeout); });
    }

    /// Make a POST request.
    inline void post(const QNetworkRequest &req, const QByteArray &data, int timeout = DEFAULT_TIMEOUT_MSECS)
    {
        queue([this, req, data, timeout]() { requestor->post(req, data, timeout); });
    }

    inline void post(const QNetworkRequest &req, QHttpMultiPart *data, int timeout = DEFAULT_TIMEOUT_MSECS)
    {
        queue([this, req, data, timeout]() { requestId = requestor->post(req, data, timeout); });
    }

    /// Make a PUT request.
    inline void put(const QNetworkRequest &req, const QByteArray &data, int timeout = DEFAULT_TIMEOUT_MSECS)
    {
        queue([this, req, data, timeout]() { requestId = requestor->put(req, data, timeout); });
    }

    inline void put(const QNetworkRequest &req, QHttpMultiPart *data, int timeout = DEFAULT_TIMEOUT_MSECS)
    {
        queue([this, req, data, timeout]() { requestId = requestor->put(req, data, timeout); });
    }

    /// Make a DELETE request.
    inline void deleteResource(const QNetworkRequest &req, int timeout = DEFAULT_TIMEOUT_MSECS)
    {
        queue([this, req, timeout]() { requestId = requestor->deleteResource(req, timeout); });
    }

    /// Make a HEAD request.
    inline void head(const QNetworkRequest &req, int timeout = DEFAULT_TIMEOUT_MSECS)
    {
        queue([this, req, timeout]() { requestId = requestor->head(req, timeout); });
    }

    /// Make a custom request.
    inline void customRequest(
        const QNetworkRequest &req, const QByteArray &verb, const QByteArray &data, int timeout = DEFAULT_TIMEOUT_MSECS
    )
    {
        queue([this, req, verb, data, timeout]() { requestId = requestor->customRequest(req, verb, data, timeout); });
    }

private slots:
    void onRequestorFinished(int, QNetworkReply::NetworkError error, QByteArray data);
    void onO2RefreshFinished(QNetworkReply::NetworkError error);
};
