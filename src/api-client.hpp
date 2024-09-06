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

#include "plugin-support.h"
#include "objects.hpp"

#define DEFAULT_TIMEOUT_MSECS (10 * 1000)

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

    friend class RequestInvoker;

    QString uuid;
    SourceLinkApiClientSettingsStore *settings;
    O2 *client;
    QNetworkAccessManager *networkManager;
    O2Requestor *requestor;
    QList<RequestInvoker *> requestQueue;
    QMap<int, bool> usedPorts;

    // Online rsources
    AccountInfo *accountInfo;
    QList<Party *> parties;
    QList<PartyEvent *> partyEvents;
    QList<Stage *> stages;
    StageSeatAllocation *seatAllocation;

protected:
    inline O2 *getO2Client() { return client; }
    inline O2Requestor *getRequestor() { return requestor; }
    inline QList<RequestInvoker *> &getRequestQueue() { return requestQueue; }

signals:
    void linkingFailed();
    void linkingSucceeded();
    void accountInfoReady(const AccountInfo *accountInfo);
    void accountInfoFailed();
    void partiesReady(QList<Party *> parties);
    void partiesFailed();
    void partyEventsReady(QList<PartyEvent *> partyEvents);
    void partyEventsFailed();
    void stagesReady(QList<Stage *> stages);
    void stagesFailed();
    void seatAllocationReady(StageSeatAllocation *seatAllocation);
    void seatAllocationFailed();
    void connectionPutSucceeded(StageConnection *connection);
    void connectionPutFailed();
    void connectionDeleteSucceeded(const QString uuid);
    void connectionDeleteFailed();
    void seatAllocationPutSucceeded(StageSeatAllocation *seatAllocation);
    void seatAllocationPutFailed();
    void seatAllocationDeleteSucceeded(const QString uuid);
    void seatAllocationDeleteFailed();

public:
    explicit SourceLinkApiClient(QObject *parent = nullptr);
    ~SourceLinkApiClient();

public slots:
    void login();
    void logout();
    bool isLoggedIn();
    void refresh();
    void requestOnlineResources();
    void requestAccountInfo();
    void requestParties();
    void requestPartyEvents(const QString &partyId);
    void requestStages();
    void requestSeatAllocation();
    void putConnection(
        const QString &sourceUuid, const QString &stageId, const QString &seatName, const QString &sourceName,
        const QString &protocol, const int port, const QString &parameters, const int maxBitrate, const int minBitrate,
        const int width, const int height
    );
    void deleteConnection(const QString &sourceUuid);
    const int getFreePort();
    void releasePort(const int port);
    void putSeatAllocation();
    void deleteSeatAllocation();

public:
    inline const QString getUuid() const { return uuid; }
    inline void setPartyId(const QString &partyId) { settings->setValue("partyId", partyId); }
    inline const QString getPartyId() const { return settings->value("partyId"); }
    inline void setPartyEventId(const QString &partyEventId) { settings->setValue("partyEventId", partyEventId); }
    inline const QString getPartyEventId() const { return settings->value("partyEventId"); }
    inline void setPortMin(const int portMin) { settings->setValue("portRange.min", QString::number(portMin)); }
    inline const int getPortMin() { return settings->value("portRange.min", "10000").toInt(); }
    inline void setPortMax(const int portMax) { settings->setValue("portRange.max", QString::number(portMax)); }
    inline const int getPortMax() { return settings->value("portRange.max", "10099").toInt(); }

    inline const AccountInfo *getAccountInfo() const { return accountInfo; }
    inline const QList<Party *> &getParties() const { return parties; }
    inline const QList<PartyEvent *> &getPartyEvents() const { return partyEvents; }
    inline const QList<Stage *> &getStages() const { return stages; }
    inline const StageSeatAllocation *getSeatAllocation() const { return seatAllocation; }

private slots:
    void onO2LinkedChanged();
    void onO2LinkingSucceeded();
    void onO2OpenBrowser(const QUrl &url);
    void onO2RefreshFinished(QNetworkReply::NetworkError);
};

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
            invoker();
        } else {
            // Reserve next invocation
            obs_log(LOG_DEBUG, "Queue next request: pos=%d", apiClient->getRequestQueue().size());
            connect(apiClient->getRequestQueue().last(), &RequestInvoker::finished, invoker);
        }
        apiClient->getRequestQueue().append(this);
    }

    /// Do token refresh
    inline void refresh()
    {
        queue([this]() {
            obs_log(LOG_DEBUG, "Invoke refresh token");
            apiClient->refresh();
            requestId = -2;
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
