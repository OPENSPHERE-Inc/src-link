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
#include <QMutex>
#include <QNetworkAccessManager>

#include <o2.h>
#include <o2requestor.h>

#define DEFAULT_TIMEOUT_MSECS (10 * 1000)


// This class introduces sequencial invocation of requests
class RequestSequencer : public QObject {
    Q_OBJECT

    friend class RequestInvoker;

    QNetworkAccessManager *networkManager;
    O2 *client;
    QList<RequestInvoker *> requestQueue;
    QMutex mutex;

public:
    explicit RequestSequencer(QNetworkAccessManager *networkManager, O2 *client, QObject *parent = nullptr);
    ~RequestSequencer();
};

// This class contains O2Requestor individually
class RequestInvoker : public QObject {
    Q_OBJECT

    RequestSequencer *sequencer;
    O2Requestor *createRequestor();

signals:
    void finished(QNetworkReply::NetworkError error, QByteArray data);

public:
    // Sequential invocation
    explicit RequestInvoker(RequestSequencer *sequencer, QObject *parent = nullptr);
    
    // Parallel invocation
    explicit RequestInvoker(QNetworkAccessManager *networkManager, O2 *client, QObject *parent = nullptr);

    ~RequestInvoker();

    template<class Func> void queue(Func invoker);

    /// Do token refresh
    void refresh();

    /// Make a GET request.
    void get(const QNetworkRequest &req, int timeout = DEFAULT_TIMEOUT_MSECS);

    /// Make a POST request.
    void post(const QNetworkRequest &req, const QByteArray &data, int timeout = DEFAULT_TIMEOUT_MSECS);
    void post(const QNetworkRequest &req, QHttpMultiPart *data, int timeout = DEFAULT_TIMEOUT_MSECS);

    /// Make a PUT request.
    void put(const QNetworkRequest &req, const QByteArray &data, int timeout = DEFAULT_TIMEOUT_MSECS);
    void put(const QNetworkRequest &req, QHttpMultiPart *data, int timeout = DEFAULT_TIMEOUT_MSECS);

    /// Make a DELETE request.
    void deleteResource(const QNetworkRequest &req, int timeout = DEFAULT_TIMEOUT_MSECS);

    /// Make a HEAD request.
    void head(const QNetworkRequest &req, int timeout = DEFAULT_TIMEOUT_MSECS);

    /// Make a custom request.
    void customRequest(
        const QNetworkRequest &req, const QByteArray &verb, const QByteArray &data, int timeout = DEFAULT_TIMEOUT_MSECS
    );

private slots:
    void onRequestorFinished(int, QNetworkReply::NetworkError error, QByteArray data);
    void onO2RefreshFinished(QNetworkReply::NetworkError error);
};
