/*
SRC-Link
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

#include <obs-module.h>

#include "request-invoker.hpp"
#include "plugin-support.h"

//#define LOG_REQUEST_QUEUE_TRACE

#ifdef LOG_REQUEST_QUEUE_TRACE
#define TRACE(...) obs_log(LOG_DEBUG, __VA_ARGS__)
#else
#define TRACE(...)
#endif

//--- RequestSequencer class ---//

RequestSequencer::RequestSequencer(QNetworkAccessManager *_networkManager, O2 *_client, QObject *parent)
    : QObject(parent),
      networkManager(_networkManager),
      client(_client)
{
    TRACE("RequestSequencer created");
}

RequestSequencer::~RequestSequencer()
{
    if (!requestQueue.isEmpty()) {
        obs_log(LOG_WARNING, "Remaining %d requests in queue.", requestQueue.size());
    }

    TRACE("RequestSequencer destroyed");
}

//--- RequestInvoker class ---//

RequestInvoker::RequestInvoker(RequestSequencer *_sequencer, QObject *parent) : QObject(parent), sequencer(_sequencer)
{
    TRACE("RequestInvoker created (Sequential)");
}

RequestInvoker::RequestInvoker(QNetworkAccessManager *networkManager, O2 *client, QObject *parent)
    : QObject(parent),
      sequencer(nullptr)
{
    sequencer = new RequestSequencer(networkManager, client, this);
    TRACE("RequestInvoker created (Parallel)");
}

RequestInvoker::~RequestInvoker()
{
    disconnect(this);
    TRACE("RequestInvoker destroyed");
}

template<class Func> void RequestInvoker::queue(Func invoker)
{
    QMutexLocker locker(&sequencer->mutex);
    {
        if (sequencer->requestQueue.isEmpty()) {
            sequencer->requestQueue.append(this);
            locker.unlock(); // Must unlock before invoking
            invoker();
            return;
        } else {
            // Reserve next invocation
            connect(sequencer->requestQueue.last(), &RequestInvoker::finished, invoker);
            sequencer->requestQueue.append(this);
        }
        TRACE("Queue request: size=%d", sequencer->requestQueue.size());
    }
    locker.unlock();
}

void RequestInvoker::refresh()
{
    connect(
        sequencer->client, SIGNAL(refreshFinished(QNetworkReply::NetworkError)), this,
        SLOT(onO2RefreshFinished(QNetworkReply::NetworkError))
    );
    queue([this]() {
        TRACE("Invoke refresh token");
        sequencer->client->refresh();
    });
}

O2Requestor *RequestInvoker::createRequestor()
{
    auto requestor = new O2Requestor(sequencer->networkManager, sequencer->client, this);
    requestor->setAddAccessTokenInQuery(false);
    requestor->setAccessTokenInAuthenticationHTTPHeaderFormat("Bearer %1");

    connect(
        requestor, SIGNAL(finished(int, QNetworkReply::NetworkError, QByteArray)), this,
        SLOT(onRequestorFinished(int, QNetworkReply::NetworkError, QByteArray))
    );

    return requestor;
}

void RequestInvoker::get(const QNetworkRequest &req, int timeout)
{
    queue([this, req, timeout]() { createRequestor()->get(req, timeout); });
}

void RequestInvoker::post(const QNetworkRequest &req, const QByteArray &data, int timeout)
{
    queue([this, req, data, timeout]() { createRequestor()->post(req, data, timeout); });
}

void RequestInvoker::post(const QNetworkRequest &req, QHttpMultiPart *data, int timeout)
{
    queue([this, req, data, timeout]() { createRequestor()->post(req, data, timeout); });
}

void RequestInvoker::put(const QNetworkRequest &req, const QByteArray &data, int timeout)
{
    queue([this, req, data, timeout]() { createRequestor()->put(req, data, timeout); });
}

void RequestInvoker::put(const QNetworkRequest &req, QHttpMultiPart *data, int timeout)
{
    queue([this, req, data, timeout]() { createRequestor()->put(req, data, timeout); });
}

void RequestInvoker::deleteResource(const QNetworkRequest &req, int timeout)
{
    queue([this, req, timeout]() { createRequestor()->deleteResource(req, timeout); });
}

void RequestInvoker::head(const QNetworkRequest &req, int timeout)
{
    queue([this, req, timeout]() { createRequestor()->head(req, timeout); });
}

void RequestInvoker::customRequest(
    const QNetworkRequest &req, const QByteArray &verb, const QByteArray &data, int timeout
)
{
    queue([this, req, verb, data, timeout]() { createRequestor()->customRequest(req, verb, data, timeout); });
}

void RequestInvoker::onRequestorFinished(int _requestId, QNetworkReply::NetworkError error, QByteArray data)
{
    TRACE("Request finished: %d", _requestId);

    QMutexLocker locker(&sequencer->mutex);
    {
        sequencer->requestQueue.removeOne(this);
    }
    locker.unlock();

    emit finished(error, data);
    deleteLater();
}

void RequestInvoker::onO2RefreshFinished(QNetworkReply::NetworkError error)
{
    if (error != QNetworkReply::NoError) {
        obs_log(LOG_ERROR, "Refresh failed: %d", error);
    } else {
        TRACE("Refresh finished");
    }

    QMutexLocker locker(&sequencer->mutex);
    {
        sequencer->requestQueue.removeOne(this);
    }
    locker.unlock();

    emit finished(error, nullptr);
    deleteLater();
}
