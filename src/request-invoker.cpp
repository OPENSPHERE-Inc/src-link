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

#include "request-invoker.hpp"

//--- RequestInvoker class ---//

RequestInvoker::RequestInvoker(SourceLinkApiClient *_apiClient, QObject *parent)
    : QObject(parent),
      apiClient(_apiClient),
      requestor(_apiClient->getRequestor())
{
    connect(
        requestor, SIGNAL(finished(int, QNetworkReply::NetworkError, QByteArray)), this,
        SLOT(onRequestorFinished(int, QNetworkReply::NetworkError, QByteArray))
    );
    connect(
        apiClient->getO2Client(), SIGNAL(refreshFinished(QNetworkReply::NetworkError)), this,
        SLOT(onO2RefreshFinished(QNetworkReply::NetworkError))
    );
    obs_log(LOG_DEBUG, "RequestInvoker created");
}

RequestInvoker::~RequestInvoker()
{
    disconnect(
        requestor, SIGNAL(finished(int, QNetworkReply::NetworkError, QByteArray)), this,
        SLOT(onRequestorFinished(int, QNetworkReply::NetworkError, QByteArray))
    );
    obs_log(LOG_DEBUG, "RequestInvoker destroyed");
}

void RequestInvoker::onRequestorFinished(int _requestId, QNetworkReply::NetworkError error, QByteArray data)
{
    if (requestId != _requestId) {
        return;
    }
    obs_log(LOG_DEBUG, "Request finished: %d", _requestId);

    apiClient->requestQueue.removeOne(this);
    emit finished(error, data);
    deleteLater();
}

void RequestInvoker::onO2RefreshFinished(QNetworkReply::NetworkError error)
{
    if (requestId != -2) {
        return;
    }
    obs_log(LOG_DEBUG, "Refresh finished");

    apiClient->requestQueue.removeOne(this);
    emit finished(error, nullptr);
    deleteLater();
}
