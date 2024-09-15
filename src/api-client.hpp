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
#include <obs.hpp>

#include <QNetworkAccessManager>
#include <QByteArray>
#include <QException>
#include <QTimer>

#include <o2.h>
#include <o2requestor.h>

#include "plugin-support.h"
#include "schema.hpp"

class SourceLinkApiClientSettingsStore : public O0AbstractStore {
    Q_OBJECT

    OBSDataAutoRelease settingsData;

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
    AccountInfo accountInfo;
    QList<Party> parties;
    QList<PartyEvent> partyEvents;  // Contains all events of all parties
    QList<Stage> stages;
    StageSeatInfo seat;

protected:
    inline O2 *getO2Client() { return client; }
    inline O2Requestor *getRequestor() { return requestor; }
    inline QList<RequestInvoker *> &getRequestQueue() { return requestQueue; }

signals:
    void loginSucceeded();
    void loginFailed();
    void logoutSucceeded();
    void accountInfoReady(const AccountInfo &accountInfo);
    void accountInfoFailed();
    void partiesReady(const QList<Party> &parties);
    void partiesFailed();
    void partyEventsReady(const QList<PartyEvent> &partyEvents);
    void partyEventsFailed();
    void stagesReady(const QList<Stage> &stages);
    void stagesFailed();
    void seatAllocationReady(const StageSeatInfo &seat);
    void seatAllocationFailed();
    void connectionPutSucceeded(const StageConnection &connection);
    void connectionPutFailed();
    void connectionDeleteSucceeded(const QString &uuid);
    void connectionDeleteFailed();
    void seatAllocationPutSucceeded(const StageSeatInfo &seat);
    void seatAllocationPutFailed();
    void seatAllocationDeleteSucceeded(const QString &uuid);
    void seatAllocationDeleteFailed();
    void pingSucceeded();
    void pingFailed();
    void screenshotPutSucceeded(const QString &sourceName);
    void screenshotPutFailed();
    void pictureGetSucceeded(const QString &pictureId, const QImage &picture);
    void pictureGetFailed(const QString &pictureId);

private slots:
    void onO2LinkedChanged();
    void onO2LinkingSucceeded();
    void onO2LinkingFailed();
    void onO2OpenBrowser(const QUrl &url);
    void onO2RefreshFinished(QNetworkReply::NetworkError);
    void onPollingTimerTimeout();

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
    bool deleteConnection(const QString &sourceUuid, const bool noSignal = false);
    bool putSeatAllocation(const bool force = false);
    bool deleteSeatAllocation(const bool noSignal = false);
    bool putScreenshot(const QString &sourceName, const QImage& image);
    bool getPicture(const QString &pitureId);

    const int getFreePort();
    void releasePort(const int port);

public:
    inline const QString &getUuid() const { return uuid; }
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

    inline const AccountInfo getAccountInfo() const { return accountInfo; }
    inline const QList<Party> &getParties() const { return parties; }
    inline const QList<PartyEvent> &getPartyEvents() const { return partyEvents; }
    inline const QList<Stage> &getStages() const { return stages; }
    inline const StageSeatInfo getSeat() const { return seat; }
};
