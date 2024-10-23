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
#include "api-websocket.hpp"

class SourceLinkApiClient : public QObject {
    Q_OBJECT

    friend class SourceLinkWebSocketClient;

    QString uuid;
    SourceLinkSettingsStore *settings;
    O2 *client;
    QNetworkAccessManager *networkManager;
    QMap<int, bool> usedPorts;
    QTimer *pollingTimer;
    RequestSequencer *sequencer;
    int activeOutputs;
    int standByOutputs;
    SourceLinkWebSocketClient *websocket;

    // Online rsources
    AccountInfo accountInfo;
    PartyArray parties;
    PartyEventArray partyEvents; // Contains all events of all parties
    StageArray stages;
    UplinkInfo uplink;
    QMap<QString, DownlinkInfo> downlinks;

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
    void uplinkReady(const UplinkInfo &uplink);
    void uplinkFailed();
    void downlinkReady(const DownlinkInfo &downlink);
    void downlinkFailed();
    void putDownlinkSucceeded(const DownlinkInfo &downlink);
    void putDownlinkFailed();
    void deleteDownlinkSucceeded(const QString &uuid);
    void deleteDownlinkFailed();
    void putUplinkSucceeded(const UplinkInfo &uplink);
    void putUplinkFailed();
    void putUplinkStatusSucceeded(const UplinkInfo &uplink);
    void putUplinkStatusFailed();
    void deleteUplinkSucceeded(const QString &uuid);
    void deleteUplinkFailed();
    void putScreenshotSucceeded(const QString &sourceName);
    void putScreenshotFailed();
    void getPictureSucceeded(const QString &pictureId, const QImage &picture);
    void getPictureFailed(const QString &pictureId);
    void ingressRestartNeeded();

private slots:
    void onO2LinkedChanged();
    void onO2LinkingSucceeded();
    void onO2LinkingFailed();
    void onO2OpenBrowser(const QUrl &url);
    void onO2RefreshFinished(QNetworkReply::NetworkError);
    void onPollingTimerTimeout();
    void onWebSocketReady();
    void onWebSocketDataChanged(const QString &subId, const QString &name, const QString &id, const QJsonObject &payload);
    void onWebSocketDataRemoved(const QString &subId, const QString &name, const QString &id);

public:
    explicit SourceLinkApiClient(QObject *parent = nullptr);
    ~SourceLinkApiClient();

    inline const QString &getUuid() const { return uuid; }
    inline const AccountInfo getAccountInfo() const { return accountInfo; }
    inline const PartyArray &getParties() const { return parties; }
    inline const PartyEventArray &getPartyEvents() const { return partyEvents; }
    inline const StageArray &getStages() const { return stages; }
    inline const UplinkInfo getUplink() const { return uplink; }
    inline SourceLinkSettingsStore *getSettings() const { return settings; }

public slots:
    void login();
    void logout();
    bool isLoggedIn();
    const RequestInvoker *refresh();
    void requestOnlineResources();
    const RequestInvoker *requestAccountInfo();
    const RequestInvoker *requestParties();
    const RequestInvoker *requestPartyEvents();
    const RequestInvoker *requestStages();
    const RequestInvoker *requestUplink();
    const RequestInvoker *requestDownlink(const QString &sourceUuid);
    const RequestInvoker *putDownlink(
        const QString &sourceUuid, const QString &stageId, const QString &seatName, const QString &sourceName,
        const QString &protocol, const int port, const QString &parameters, const int maxBitrate, const int minBitrate,
        const int width, const int height, const int revision = 0
    );
    const RequestInvoker *deleteDownlink(const QString &sourceUuid, const bool parallel = false);
    const RequestInvoker *putUplink(const bool force = false);
    const RequestInvoker *putUplinkStatus();
    const RequestInvoker *deleteUplink(const bool parallel = false);
    const RequestInvoker *putScreenshot(const QString &sourceName, const QImage &image);
    void getPicture(const QString &pitureId);
    void restartIngress() { emit ingressRestartNeeded(); }
    void terminate();
    void openStagesManagementPage(); // Just open web browser

    const int getFreePort();
    void releasePort(const int port);
    inline void incrementActiveOutputs() { activeOutputs++; }
    inline void decrementActiveOutputs() { activeOutputs--; }
    inline void incrementStandByOutputs() { standByOutputs++; }
    inline void decrementStandByOutputs() { standByOutputs--; }
};
