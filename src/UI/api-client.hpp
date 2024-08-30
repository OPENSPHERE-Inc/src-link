/*
Source Link
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

#include <obs-module.h>

#include <QNetworkAccessManager>
#include <QByteArray>
#include <QException>

#include <o2.h>
#include <o2requestor.h>

#include "objects.hpp"

class SourceLinkApiClientSettingsStore : public O0AbstractStore {
    Q_OBJECT

    obs_data_t *settingsData;

public:
    explicit SourceLinkApiClientSettingsStore(QObject *parent = nullptr);
    ~SourceLinkApiClientSettingsStore();

    QString value(const QString &key, const QString &defaultValue = QString());
    void setValue(const QString &key, const QString &value);
};

class SourceLinkApiClient : public QObject {
    Q_OBJECT

    friend class RequestHandler;

    SourceLinkApiClientSettingsStore settings;
    O2 client;
    QNetworkAccessManager networkManager;

protected:
    O2Requestor requestor;
    QList<RequestHandler *> requestQueue;

signals:
    void extraTokensReady(const QVariantMap &extraTokens);
    void linkingFailed();
    void linkingSucceeded();
    void accountInfoReady(AccountInfo *accountInfo);
    void accountInfoFailed();
    void partyEventsReady(QList<PartyEvent *> partyEvents);
    void partyEventsFailed();

public:
    explicit SourceLinkApiClient(QObject *parent = nullptr);

    void login(QWidget *parent);
    void logout();
    void getAccountInfo();
    void getPartyEvents();

private slots:
    void onLinkedChanged();
    void onLinkingSucceeded();
    void onOpenBrowser(const QUrl &url);
    void onCloseBrowser();
};

class RequestHandler : public QObject {
    Q_OBJECT

    SourceLinkApiClient *apiClient;
    int requestId = -1;

signals:
    void finished(QNetworkReply::NetworkError error, QByteArray data);

public:
    explicit RequestHandler(SourceLinkApiClient *apiClient);
    ~RequestHandler();

    template<class Func> inline void queue(Func invoker)
    {
        if (apiClient->requestQueue.isEmpty()) {
            invoker();
        } else {
            // Reserve next invocation
            connect(apiClient->requestQueue.last(), &RequestHandler::finished, invoker);
        }
        apiClient->requestQueue.append(this);
    }

    /// Make a GET request.
    inline void get(const QNetworkRequest &req, int timeout = 60 * 1000)
    {
        queue([this, req, timeout]() { requestId = apiClient->requestor.get(req, timeout); });
    }

    /// Make a POST request.
    inline void post(const QNetworkRequest &req, const QByteArray &data, int timeout = 60 * 1000)
    {
        queue([this, req, data, timeout]() { apiClient->requestor.post(req, data, timeout); });
    }

    inline void post(const QNetworkRequest &req, QHttpMultiPart *data, int timeout = 60 * 1000)
    {
        queue([this, req, data, timeout]() { requestId = apiClient->requestor.post(req, data, timeout); });
    }

    /// Make a PUT request.
    inline void put(const QNetworkRequest &req, const QByteArray &data, int timeout = 60 * 1000)
    {
        queue([this, req, data, timeout]() { requestId = apiClient->requestor.put(req, data, timeout); });
    }

    inline void put(const QNetworkRequest &req, QHttpMultiPart *data, int timeout = 60 * 1000)
    {
        queue([this, req, data, timeout]() { requestId = apiClient->requestor.put(req, data, timeout); });
    }

    /// Make a DELETE request.
    inline void deleteResource(const QNetworkRequest &req, int timeout = 60 * 1000)
    {
        queue([this, req, timeout]() { requestId = apiClient->requestor.deleteResource(req, timeout); });
    }

    /// Make a HEAD request.
    inline void head(const QNetworkRequest &req, int timeout = 60 * 1000)
    {
        queue([this, req, timeout]() { requestId = apiClient->requestor.head(req, timeout); });
    }

    /// Make a custom request.
    inline void
    customRequest(const QNetworkRequest &req, const QByteArray &verb, const QByteArray &data, int timeout = 60 * 1000)
    {
        queue([this, req, verb, data, timeout]() {
            requestId = apiClient->requestor.customRequest(req, verb, data, timeout);
        });
    }

private slots:
    void onFinished(int, QNetworkReply::NetworkError error, QByteArray data);
};
