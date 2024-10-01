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

#include "plugin-support.h"
#include "schema.hpp"
#include "settings.hpp"
#include "request-invoker.hpp"

class SourceLinkApiClient : public QObject {
    Q_OBJECT

    QString uuid;
    SourceLinkSettingsStore *settings;
    O2 *client;
    QNetworkAccessManager *networkManager;
    QMap<int, bool> usedPorts;
    QTimer *pollingTimer;
    RequestSequencer *sequencer;

    // Online rsources
    AccountInfo accountInfo;
    PartyArray parties;
    PartyEventArray partyEvents; // Contains all events of all parties
    StageArray stages;
    StageSeatInfo seat;

signals:
    void loginSucceeded();
    void loginFailed();
    void logoutSucceeded();
    void accountInfoReady(const AccountInfo &accountInfo);
    void accountInfoFailed();
    void partiesReady(const PartyArray &parties);
    void partiesFailed();
    void partyEventsReady(const PartyEventArray &partyEvents);
    void partyEventsFailed();
    void stagesReady(const StageArray &stages);
    void stagesFailed();
    void seatAllocationReady(const StageSeatInfo &seat);
    void seatAllocationFailed();
    void stageConnectionReady(const StageConnectionInfo &connection);
    void stageConnectionFailed();
    void connectionPutSucceeded(const StageConnectionInfo &connection);
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
    void ingressRestartNeeded();

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

    inline const QString &getUuid() const { return uuid; }
    inline const AccountInfo getAccountInfo() const { return accountInfo; }
    inline const PartyArray &getParties() const { return parties; }
    inline const PartyEventArray &getPartyEvents() const { return partyEvents; }
    inline const StageArray &getStages() const { return stages; }
    inline const StageSeatInfo getSeat() const { return seat; }
    inline SourceLinkSettingsStore *getSettings() const { return settings; }

public slots:
    void login();
    void logout();
    bool isLoggedIn();
    const RequestInvoker *refresh();
    const RequestInvoker *ping();
    void requestOnlineResources();
    const RequestInvoker *requestAccountInfo();
    const RequestInvoker *requestParties();
    const RequestInvoker *requestPartyEvents();
    const RequestInvoker *requestStages();
    const RequestInvoker *requestSeatAllocation();
    const RequestInvoker *requestStageConnection(const QString &sourceUuid);
    const RequestInvoker *putConnection(
        const QString &sourceUuid, const QString &stageId, const QString &seatName, const QString &sourceName,
        const QString &protocol, const int port, const QString &parameters, const int maxBitrate, const int minBitrate,
        const int width, const int height, const int revision = 0
    );
    const RequestInvoker *deleteConnection(const QString &sourceUuid, const bool parallel = false);
    const RequestInvoker *putSeatAllocation(const bool force = false);
    const RequestInvoker *deleteSeatAllocation(const bool parallel = false);
    const RequestInvoker *putScreenshot(const QString &sourceName, const QImage &image);
    void getPicture(const QString &pitureId);
    void restartIngress() { emit ingressRestartNeeded(); }
    void terminate();
    void openStagesManagementPage(); // Just open web browser

    const int getFreePort();
    void releasePort(const int port);
};
