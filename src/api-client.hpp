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
#include <QTimer>

#include <o2.h>
#include <o2requestor.h>

#include "plugin-support.h"
#include "schema.hpp"

#define QENUM_NAME(o, e, v) (o::staticMetaObject.enumerator(o::staticMetaObject.indexOfEnumerator(#e)).valueToKey((v)))
#define GRANTFLOW_STR(v) QString(QENUM_NAME(O2, GrantFlow, v))

//#define LOCAL_DEBUG

#ifdef LOCAL_DEBUG
#define API_SERVER "http://localhost:3000"
#else
#define API_SERVER "https://source-link-test.opensphere.co.jp"
#endif

#define CLIENT_ID "testClientId"
#define CLIENT_SECRET "testClientSecret"
#define AUTHORIZE_URL (API_SERVER "/oauth2/authorize")
#define TOKEN_URL (API_SERVER "/oauth2/token")
#define ACCOUNT_INFO_URL (API_SERVER "/api/v1/accounts/me")
#define PARTIES_URL (API_SERVER "/api/v1/parties/my")
#define PARTY_EVENTS_URL (API_SERVER "/api/v1/events/my")
#define STAGES_URL (API_SERVER "/api/v1/stages")
#define STAGES_CONNECTIONS_URL (API_SERVER "/api/v1/stages/connections")
#define STAGE_SEAT_ALLOCATIONS_URL (API_SERVER "/api/v1/stages/seat-allocations")
#define ACCOUNT_SCREENSHOTS_URL (API_SERVER "/api/v1/accounts/screenshots")
#define PING_URL (API_SERVER "/api/v1/ping")
#define SCOPE "read write"
#define SETTINGS_JSON_NAME "settings.json"
#define POLLING_INTERVAL_MSECS 30000
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
    QTimer *pollingTimer;

    // Online rsources
    AccountInfo *accountInfo;
    QList<Party *> parties;
    QList<PartyEvent *> partyEvents;  // Contains all events of all parties
    QList<Stage *> stages;
    StageSeatInfo *seat;
    int seatAllocationRefs;

protected:
    inline O2 *getO2Client() { return client; }
    inline O2Requestor *getRequestor() { return requestor; }
    inline QList<RequestInvoker *> &getRequestQueue() { return requestQueue; }

signals:
    void linkingFailed();
    void linkingSucceeded();
    void accountInfoReady(const AccountInfo *accountInfo);
    void accountInfoFailed();
    void partiesReady(const QList<Party *> &parties);
    void partiesFailed();
    void partyEventsReady(const QList<PartyEvent *> &partyEvents);
    void partyEventsFailed();
    void stagesReady(const QList<Stage *> &stages);
    void stagesFailed();
    void seatAllocationReady(const StageSeatInfo *setPartyEventId);
    void seatAllocationFailed();
    void connectionPutSucceeded(const StageConnection *connection);
    void connectionPutFailed();
    void connectionDeleteSucceeded(const QString &uuid);
    void connectionDeleteFailed();
    void seatAllocationPutSucceeded(const StageSeatInfo *seat);
    void seatAllocationPutFailed();
    void seatAllocationDeleteSucceeded(const QString &uuid);
    void seatAllocationDeleteFailed();
    void pingSucceeded();
    void pingFailed();
    void screenshotPutSucceeded(const QString &sourceName);
    void screenshotPutFailed();

public:
    explicit SourceLinkApiClient(QObject *parent = nullptr);
    ~SourceLinkApiClient();

public slots:
    void login();
    void logout();
    bool isLoggedIn();
    void refresh();
    bool ping();
    bool requestOnlineResources();
    bool requestAccountInfo();
    bool requestParties();
    bool requestPartyEvents();
    bool requestStages();
    bool requestSeatAllocation();
    bool putConnection(
        const QString &sourceUuid, const QString &stageId, const QString &seatName, const QString &sourceName,
        const QString &protocol, const int port, const QString &parameters, const int maxBitrate, const int minBitrate,
        const int width, const int height
    );
    bool deleteConnection(const QString &sourceUuid);
    bool putSeatAllocation(const bool force = false);
    bool deleteSeatAllocation();
    bool putScreenshot(const QString &sourceName, const QImage& image);

    const int getFreePort();
    void releasePort(const int port);

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
    inline void setForceConnection(const bool forceConnection)
    {
        settings->setValue("forceConnection", forceConnection ? "true" : "false");
    }
    inline const bool getForceConnection() { return settings->value("forceConnection", "false") == "true"; }

    inline const AccountInfo *getAccountInfo() const { return accountInfo; }
    inline const QList<Party *> &getParties() const { return parties; }
    inline const QList<PartyEvent *> &getPartyEvents() const { return partyEvents; }
    inline const QList<Stage *> &getStages() const { return stages; }
    inline const StageSeatInfo *getSeat() const { return seat; }

private slots:
    void onO2LinkedChanged();
    void onO2LinkingSucceeded();
    void onO2OpenBrowser(const QUrl &url);
    void onO2RefreshFinished(QNetworkReply::NetworkError);
    void onPollingTimerTimeout();
};
