/*
SRC-Link
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

#include <obs-module.h>
#include <util/platform.h>

#include <algorithm>
#include <climits>

#include <QMessageBox>
#include <QDesktopServices>
#include <QString>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPromise>
#include <QTimer>
#include <QByteArray>
#include <QBuffer>
#include <QFile>
#include <QPointer>

#include "plugin-support.h"
#include "api-client.hpp"
#include "api-websocket.hpp"
#include "utils.hpp"

#define SCOPE "read write"
#define SCREENSHOT_QUALITY 75
#define REPLY_HTML_NAME "oauth-reply.html"

//#define API_DEBUG

// REST Endpoints
#ifndef API_SERVER
#define API_SERVER "http://localhost:3000"
#endif
#ifndef API_WS_SERVER
#define API_WS_SERVER "ws://localhost:3000"
#endif
#ifndef FRONTEND_SERVER
#define FRONTEND_SERVER "http://localhost:3001"
#endif
#define TOKEN_URL (API_SERVER "/oauth2/token")
#define ACCOUNT_INFO_URL (API_SERVER "/api/v1/accounts/me")
#define PARTIES_URL (API_SERVER "/api/v1/parties/my")
#define PARTY_EVENTS_URL (API_SERVER "/api/v1/events/my")
#define PARTICIPANTS_URL (API_SERVER "/api/v1/participants/my")
#define STAGES_URL (API_SERVER "/api/v1/stages")
#define DOWNLINK_URL (API_SERVER "/api/v1/downlink/%1")
#define DOWNLINK_STATUS_URL (API_SERVER "/api/v1/downlink/%1/status")
#define UPLINK_URL (API_SERVER "/api/v1/uplink/%1")
#define UPLINK_STATUS_URL (API_SERVER "/api/v1/uplink/%1/status")
#define SCREENSHOTS_URL (API_SERVER "/api/v1/screenshots/%1/%2")
#define PICTURES_URL (API_SERVER "/pictures/%1")
#define WEBSOCKET_URL (API_WS_SERVER "/api/v1/websocket")
#define MEMBER_ACTIVATE_URL (API_SERVER "/api/v1/members/activate")
// Control Panel Pages
#define AUTHORIZE_URL (FRONTEND_SERVER "/oauth2/authorize")
#define STAGES_PAGE_URL (FRONTEND_SERVER "/receivers")
#define CONTROL_PANEL_PAGE_URL (FRONTEND_SERVER "/dashboard")
#define MEMBERSHIPS_PAGE (FRONTEND_SERVER "/memberships")
#define SIGNUP_PAGE (FRONTEND_SERVER "/accounts/register")
#define WS_PORTALS_PAGE (FRONTEND_SERVER "/ws-portals")
#define GUEST_CODES_PAGE (FRONTEND_SERVER "/accounts/guest-codes")
// OAuth2 Client info
#ifndef CLIENT_ID
#define CLIENT_ID "testClientId"
#endif
#ifndef CLIENT_SECRET
#define CLIENT_SECRET "testClientSecret"
#endif

//--- Macros ---//

#ifdef API_DEBUG
#define API_LOG(...) obs_log(LOG_DEBUG, "client: " __VA_ARGS__)
#else
#define API_LOG(...)
#endif
#define INFO_LOG(...) obs_log(LOG_INFO, "client: " __VA_ARGS__)
#define ERROR_LOG(...) obs_log(LOG_ERROR, "client: " __VA_ARGS__)
#define WARNING_LOG(...) obs_log(LOG_WARNING, "client: " __VA_ARGS__)

#define CHECK_CLIENT_TOKEN(...)                             \
    do {                                                    \
        if (oauth2Client->refreshTokenValue().isEmpty()) {  \
            ERROR_LOG("client: Client not authenticated."); \
            return __VA_ARGS__;                             \
        }                                                   \
    } while (0)

#define CHECK_RESPONSE_NOERROR(signal, message, ...) \
    do {                                             \
        if (error != HttpError::NoError) {           \
            ERROR_LOG(message, __VA_ARGS__);         \
            emit signal();                           \
            return;                                  \
        }                                            \
    } while (0)

#define HTTP_ERR(e) static_cast<int>(e)

//--- SRCLinkApiClient class ---//

SRCLinkApiClient::SRCLinkApiClient(QObject *parent)
    : QObject(parent),
      // Create store object for writing the received tokens
      settings(new SRCLinkSettingsStore(this)),
      httpClient(nullptr),
      oauth2Client(nullptr),
      activeOutputs(0),
      standByOutputs(0),
      uplinkStatus(UPLINK_STATUS_INACTIVE),
      terminating(false)
{
    API_LOG("SRCLinkApiClient creating with %s,%s,%s", API_SERVER, API_WS_SERVER, FRONTEND_SERVER);

    httpClient = new CurlHttpClient(this);
    oauth2Client = new OAuth2Client(httpClient, this);
    sequencer = new HttpRequestSequencer(httpClient, oauth2Client, this);
    websocket = new SRCLinkWebSocketClient(QUrl(WEBSOCKET_URL), this, this);

    uuid = settings->value("uuid");
    if (uuid.isEmpty()) {
        // Generate new UUID for the client
        OBSString defaultUuid(os_generate_uuid());
        uuid = QString(defaultUuid);
        settings->setValue("uuid", uuid);
    }

    // Retrieve private IP addresses
    retrievePrivateIp();

    oauth2Client->setRequestUrl(AUTHORIZE_URL);
    oauth2Client->setTokenUrl(TOKEN_URL);
    oauth2Client->setRefreshTokenUrl(TOKEN_URL);
    oauth2Client->setClientId(CLIENT_ID);
    oauth2Client->setClientSecret(CLIENT_SECRET);
    oauth2Client->setLocalPort(0);
    oauth2Client->setScope(QString::fromLatin1(SCOPE));
    oauth2Client->setStore(settings);

    // Read reply content html
    QString replyHtmlFile =
        QString("%1/%2").arg(obs_get_module_data_path(obs_current_module())).arg(QTStr(REPLY_HTML_NAME));
    QFile replyContent(replyHtmlFile);
    if (!replyContent.open(QIODevice::ReadOnly)) {
        ERROR_LOG("Failed to read reply content html: %s", qUtf8Printable(replyHtmlFile));
    } else {
        oauth2Client->setReplyContent(replyContent.readAll());
    }

    connect(oauth2Client, SIGNAL(linkedChanged()), this, SLOT(onOAuth2LinkedChanged()));
    connect(oauth2Client, SIGNAL(linkingSucceeded()), this, SLOT(onOAuth2LinkingSucceeded()));
    connect(oauth2Client, SIGNAL(linkingFailed()), this, SLOT(onOAuth2LinkingFailed()));
    connect(oauth2Client, SIGNAL(openBrowser(QUrl)), this, SLOT(onOAuth2OpenBrowser(QUrl)));
    connect(oauth2Client, SIGNAL(refreshFinished(HttpError)), this, SLOT(onOAuth2RefreshFinished(HttpError)));

    connect(websocket, SIGNAL(ready(bool)), this, SLOT(onWebSocketReady(bool)));
    connect(websocket, SIGNAL(aborted(const QString &)), this, SLOT(onWebSocketAborted(const QString &)));
    connect(websocket, SIGNAL(disconnected()), this, SLOT(onWebSocketDisconnected()));
    connect(
        websocket, SIGNAL(added(const WebSocketMessage &)), this, SLOT(onWebSocketDataChanged(const WebSocketMessage &))
    );
    connect(
        websocket, SIGNAL(changed(const WebSocketMessage &)), this,
        SLOT(onWebSocketDataChanged(const WebSocketMessage &))
    );
    connect(
        websocket, SIGNAL(removed(const WebSocketMessage &)), this,
        SLOT(onWebSocketDataRemoved(const WebSocketMessage &))
    );
    connect(
        websocket, &SRCLinkWebSocketClient::subscribed, this,
        [this](const QString &name, const QJsonObject &payload) { emit webSocketSubscribeSucceeded(name, payload); }
    );
    connect(
        websocket, &SRCLinkWebSocketClient::unsubscribed, this,
        [this](const QString &name, const QJsonObject &payload) { emit webSocketUnsubscribeSucceeded(name, payload); }
    );
    connect(
        websocket, &SRCLinkWebSocketClient::subscribeFailed, this,
        [this](const QString &name, const QJsonObject &payload) { emit webSocketSubscribeFailed(name, payload); }
    );
    connect(
        websocket, &SRCLinkWebSocketClient::unsubscribeFailed, this,
        [this](const QString &name, const QJsonObject &payload) { emit webSocketUnsubscribeFailed(name, payload); }
    );
    connect(websocket, &SRCLinkWebSocketClient::invoked, this, [this](const QString &name, const QJsonObject &payload) {
        emit webSocketInvokeSucceeded(name, payload);
    });
    connect(
        websocket, &SRCLinkWebSocketClient::invokeFailed, this,
        [this](const QString &name, const QJsonObject &payload) { emit webSocketInvokeFailed(name, payload); }
    );

    connect(this, &SRCLinkApiClient::licenseChanged, this, [this](const SubscriptionLicense &license) {
        if (license.getLicenseValid()) {
            putUplink();
        }
    });

    tokenRefreshTimer = new QTimer(this);
    tokenRefreshTimer->setSingleShot(true);
    connect(tokenRefreshTimer, &QTimer::timeout, this, [this]() { refresh(); });

    if (oauth2Client->expires() - 60 <= QDateTime::currentSecsSinceEpoch()) {
        // Refresh token now
        refresh();
    } else {
        if (auto *invoker = requestAccountInfo()) {
            connect(invoker, &HttpRequestInvoker::finished, this, [this](HttpError error) {
                if (error != HttpError::NoError) {
                    return;
                }

                //syncOnlineResources(); // Now received by WebSocket
                if (auto *uplinkInvoker = putUplink()) {
                    connect(uplinkInvoker, &HttpRequestInvoker::finished, this, [this](HttpError) {
                        // WebSocket will be started even if uplink upload failed.
                        websocket->start();
                    });
                }
            });
        }

        // Schedule next refresh (guard against negative/overflow values)
        qint64 refreshInterval = oauth2Client->expires() * 1000 - 60000 - QDateTime::currentMSecsSinceEpoch();
        tokenRefreshTimer->start(
            static_cast<int>(std::clamp(refreshInterval, static_cast<qint64>(1000), static_cast<qint64>(INT_MAX)))
        );
    }

    API_LOG("SRCLinkApiClient created");
}

SRCLinkApiClient::~SRCLinkApiClient()
{
    API_LOG("SRCLinkApiClient destroyed");
}

void SRCLinkApiClient::login()
{
    API_LOG("Starting OAuth 2 Authorization Code flow");
    oauth2Client->link();
}

void SRCLinkApiClient::logout()
{
    if (auto *invoker = deleteUplink(true)) {
        connect(invoker, &HttpRequestInvoker::finished, this, [this](HttpError) { oauth2Client->unlink(); });
    } else {
        oauth2Client->unlink();
    }
}

bool SRCLinkApiClient::isLoggedIn()
{
    return oauth2Client->linked();
}

const HttpRequestInvoker *SRCLinkApiClient::refresh()
{
    auto invoker = new HttpRequestInvoker(sequencer, this);
    invoker->refresh();
    return invoker;
}

int SRCLinkApiClient::getFreePort()
{
    auto min = settings->getIngressPortMin();
    auto max = settings->getIngressPortMax();

    for (auto i = min; i <= max; i++) {
        if (!usedPorts[i]) {
            usedPorts[i] = true;
            return i;
        }
    }
    return 0;
}

void SRCLinkApiClient::releasePort(const int port)
{
    usedPorts[port] = false;
}

void SRCLinkApiClient::syncOnlineResources()
{
    CHECK_CLIENT_TOKEN();

    requestParties();
    requestPartyEvents();
    requestParticipants();
    requestStages();
}

void SRCLinkApiClient::clearOnlineResources()
{
    accountInfo = AccountInfo();
    parties = PartyArray();
    partyEvents = PartyEventArray();
    participants = PartyEventParticipantArray();
    stages = StageArray();
    uplink = UplinkInfo();
    wsPortals = WsPortalArray();
    downlinks.clear();
    settings->setParticipantId("");
    settings->setPartyId("");
}

void SRCLinkApiClient::terminate()
{
    API_LOG("Terminating API client.");
    terminating = true;
    uplink = QJsonObject();
    // FIXME: deleteUplink(true) here is fire-and-forget — the in-flight DELETE is
    // canceled by ~CurlHttpClient on plugin teardown, leaving the server uplink
    // entry until its TTL. Replace with a bounded QEventLoop pump (or a sync
    // curl_easy_perform call) in a separate PR.
    deleteUplink(true);
}

// Call putUplinkStatus() when uplinkStatus is changed
void SRCLinkApiClient::syncUplinkStatus(bool force)
{
    if (uplink.isEmpty()) {
        return;
    }

    auto nextUplinkStatus = activeOutputs > 0    ? UPLINK_STATUS_ACTIVE
                            : standByOutputs > 0 ? UPLINK_STATUS_STANDBY
                                                 : UPLINK_STATUS_INACTIVE;
    if (force || nextUplinkStatus != uplinkStatus) {
        uplinkStatus = nextUplinkStatus;
        putUplinkStatus();
    }
}

QString SRCLinkApiClient::retrievePrivateIp()
{
    auto privateAddresses = getPrivateIPv4Addresses();
    if (privateAddresses.isEmpty()) {
        settings->setIngressPrivateIpValue("");
        settings->setIngressPrivateIpIndex(0);
        return "";
    }
    auto index = privateAddresses.indexOf(settings->getIngressPrivateIpValue());
    if (index < 0) {
        // IP had been changed -> fallback to index
        index = std::min<qsizetype>(settings->getIngressPrivateIpIndex(), privateAddresses.size() - 1);
        settings->setIngressPrivateIpValue(privateAddresses[index]);
    }
    settings->setIngressPrivateIpIndex((int)index);

    return settings->getIngressPrivateIpValue();
}

const HttpRequestInvoker *SRCLinkApiClient::requestAccountInfo()
{
    CHECK_CLIENT_TOKEN(nullptr);

    API_LOG("Requesting account info.");
    auto invoker = new HttpRequestInvoker(sequencer, this);
    connect(invoker, &HttpRequestInvoker::finished, this, [this](HttpError error, QByteArray replyData) {
        CHECK_RESPONSE_NOERROR(accountInfoFailed, "Requesting account info failed: %d", HTTP_ERR(error));

        AccountInfo newAccountInfo = QJsonDocument::fromJson(replyData).object();
        if (!newAccountInfo.isValid()) {
            ERROR_LOG("Received malformed account info data.");
            API_LOG("dump=%s", replyData.toStdString().c_str());
            emit accountInfoFailed();
            return;
        }

        auto emitLicenseChanged = !accountInfo.isEmpty() &&
                                  accountInfo.getSubscriptionLicense().getLicenseValid() !=
                                      newAccountInfo.getSubscriptionLicense().getLicenseValid();

        accountInfo = newAccountInfo;
        API_LOG("Received account: %s", qUtf8Printable(accountInfo.getAccount().getDisplayName()));
        emit accountInfoReady(accountInfo);

        if (emitLicenseChanged) {
            emit licenseChanged(newAccountInfo.getSubscriptionLicense());
        }
    });
    invoker->get(QUrl(ACCOUNT_INFO_URL));

    return invoker;
}

const HttpRequestInvoker *SRCLinkApiClient::requestParties()
{
    CHECK_CLIENT_TOKEN(nullptr);

    API_LOG("Requesting parties.");
    auto invoker = new HttpRequestInvoker(sequencer, this);
    connect(invoker, &HttpRequestInvoker::finished, this, [this](HttpError error, QByteArray replyData) {
        CHECK_RESPONSE_NOERROR(partiesFailed, "Requesting parties failed: %d", HTTP_ERR(error));

        PartyArray newParties = QJsonDocument::fromJson(replyData).array();
        if (!newParties.every([](const Party &party) { return party.isValid(); })) {
            ERROR_LOG("Received malformed parties data.");
            API_LOG("dump=%s", replyData.toStdString().c_str());
            emit partiesFailed();
            return;
        }

        parties = newParties;
        API_LOG("Received %d parties", parties.size());

        if (settings->getPartyId().isEmpty() && parties.size() > 0) {
            settings->setPartyId(parties[0].getId());
        }

        emit partiesReady(parties);
    });
    invoker->get(QUrl(PARTIES_URL));

    return invoker;
}

const HttpRequestInvoker *SRCLinkApiClient::requestPartyEvents()
{
    CHECK_CLIENT_TOKEN(nullptr);

    API_LOG("Requesting party events");
    auto invoker = new HttpRequestInvoker(sequencer, this);
    connect(invoker, &HttpRequestInvoker::finished, this, [this](HttpError error, QByteArray replyData) {
        CHECK_RESPONSE_NOERROR(partyEventsFailed, "Requesting party events failed: %d", HTTP_ERR(error));

        PartyEventArray newPartyEvents = QJsonDocument::fromJson(replyData).array();
        if (!newPartyEvents.every([](const PartyEvent &event) { return event.isValid(); })) {
            ERROR_LOG("Received malformed party events data.");
            API_LOG("dump=%s", replyData.toStdString().c_str());
            emit partyEventsFailed();
            return;
        }

        partyEvents = newPartyEvents;
        API_LOG("Received %d party events", partyEvents.size());

        emit partyEventsReady(partyEvents);
    });
    invoker->get(QUrl(PARTY_EVENTS_URL));

    return invoker;
}

const HttpRequestInvoker *SRCLinkApiClient::requestParticipants()
{
    CHECK_CLIENT_TOKEN(nullptr);

    API_LOG("Requesting participants");
    auto invoker = new HttpRequestInvoker(sequencer, this);
    connect(invoker, &HttpRequestInvoker::finished, this, [this](HttpError error, QByteArray replyData) {
        CHECK_RESPONSE_NOERROR(participantsFailed, "Requesting participants failed: %d", HTTP_ERR(error));

        PartyEventParticipantArray newParticipants = QJsonDocument::fromJson(replyData).array();
        if (!newParticipants.every([](const PartyEventParticipant &participant) { return participant.isValid(); })) {
            ERROR_LOG("Received malformed participants data.");
            API_LOG("dump=%s", replyData.toStdString().c_str());
            emit participantsFailed();
            return;
        }

        participants = newParticipants;
        API_LOG("Received %d participants", participants.size());

        emit participantsReady(participants);
    });
    invoker->get(QUrl(PARTICIPANTS_URL));

    return invoker;
}

const HttpRequestInvoker *SRCLinkApiClient::requestStages()
{
    CHECK_CLIENT_TOKEN(nullptr);

    API_LOG("Requesting receivers.");
    auto invoker = new HttpRequestInvoker(sequencer, this);
    connect(invoker, &HttpRequestInvoker::finished, this, [this](HttpError error, QByteArray replyData) {
        CHECK_RESPONSE_NOERROR(stagesFailed, "Requesting receivers failed: %d", HTTP_ERR(error));

        StageArray newStages = QJsonDocument::fromJson(replyData).array();
        if (!newStages.every([](const Stage &stage) { return stage.isValid(); })) {
            ERROR_LOG("Received malformed receivers data.");
            API_LOG("dump=%s", replyData.toStdString().c_str());
            emit stagesFailed();
            return;
        }

        stages = newStages;
        API_LOG("Received %d receivers", stages.size());

        emit stagesReady(stages);
    });
    invoker->get(QUrl(STAGES_URL));

    return invoker;
}

const HttpRequestInvoker *SRCLinkApiClient::requestUplink()
{
    CHECK_CLIENT_TOKEN(nullptr);

    API_LOG("Requesting uplink for %s", qUtf8Printable(uuid));
    auto invoker = new HttpRequestInvoker(sequencer, this);
    connect(invoker, &HttpRequestInvoker::finished, this, [this](HttpError error, QByteArray replyData) {
        if (terminating) {
            WARNING_LOG("Ignore the response during terminating");
            return;
        }
        if (error != HttpError::NoError) {
            ERROR_LOG("Requesting uplink for %s failed: %d", qUtf8Printable(uuid), HTTP_ERR(error));
            emit uplinkFailed(uuid);
            return;
        }
        API_LOG("Received uplink for %s", qUtf8Printable(uuid));

        UplinkInfo newUplink = QJsonDocument::fromJson(replyData).object();
        if (!newUplink.isValid()) {
            ERROR_LOG("Received malformed uplink data.");
            API_LOG("dump=%s", replyData.toStdString().c_str());
            emit uplinkFailed(uuid);
            return;
        }

        uplink = newUplink;

        emit uplinkReady(uplink);
    });
    invoker->get(QUrl(QString(UPLINK_URL).arg(uuid)));

    return invoker;
}

const HttpRequestInvoker *SRCLinkApiClient::requestDownlink(const QString &sourceUuid)
{
    CHECK_CLIENT_TOKEN(nullptr);

    API_LOG("Requesting downlink for %s", qUtf8Printable(sourceUuid));
    auto invoker = new HttpRequestInvoker(sequencer, this);
    connect(invoker, &HttpRequestInvoker::finished, this, [this, sourceUuid](HttpError error, QByteArray replyData) {
        if (error != HttpError::NoError) {
            ERROR_LOG("Requesting downlink for %s failed: %d", qUtf8Printable(sourceUuid), HTTP_ERR(error));
            emit downlinkFailed(sourceUuid);
            return;
        }
        API_LOG("Received downlink for %s", qUtf8Printable(sourceUuid));

        DownlinkInfo newDownlink = QJsonDocument::fromJson(replyData).object();
        if (!newDownlink.isValid()) {
            ERROR_LOG("Received malformed downlink data.");
            API_LOG("dump=%s", replyData.toStdString().c_str());
            emit downlinkFailed(sourceUuid);
            return;
        }

        downlinks[sourceUuid] = newDownlink;

        emit downlinkReady(downlinks[sourceUuid]);
    });
    invoker->get(QUrl(QString(DOWNLINK_URL).arg(sourceUuid)));

    return invoker;
}

const HttpRequestInvoker *SRCLinkApiClient::putDownlink(const QString &sourceUuid, const DownlinkRequestBody &params)
{
    CHECK_CLIENT_TOKEN(nullptr);

    auto url = QUrl(QString(DOWNLINK_URL).arg(sourceUuid));
    QMap<QByteArray, QByteArray> headers = {{"Content-Type", "application/json"}};

    API_LOG("Putting downlink: %s rev.%d", qUtf8Printable(sourceUuid), params.getRevision());
    auto invoker = new HttpRequestInvoker(sequencer, this);
    connect(
        invoker, &HttpRequestInvoker::finished, this,
        [this, sourceUuid, params](HttpError error, QByteArray replyData) {
            if (error != HttpError::NoError) {
                ERROR_LOG(
                    "Putting downlink %s rev.%d failed: %d", qUtf8Printable(sourceUuid), params.getRevision(),
                    HTTP_ERR(error)
                );
                emit putDownlinkFailed(sourceUuid);
                emit downlinkFailed(sourceUuid);
                return;
            }

            DownlinkInfo newDownlink = QJsonDocument::fromJson(replyData).object();
            if (!newDownlink.isValid()) {
                ERROR_LOG("Received malformed downlink data.");
                API_LOG("dump=%s", replyData.toStdString().c_str());
                emit putDownlinkFailed(sourceUuid);
                emit downlinkFailed(sourceUuid);
                return;
            }

            downlinks[sourceUuid] = newDownlink;
            obs_log(
                LOG_DEBUG, "Put downlink %s rev.%d succeeded",
                qUtf8Printable(downlinks[sourceUuid].getConnection().getId()), params.getRevision()
            );

            websocket->subscribe("downlink", {{"uuid", sourceUuid}});

            emit putDownlinkSucceeded(downlinks[sourceUuid]);
            emit downlinkReady(downlinks[sourceUuid]);
        }
    );
    invoker->put(url, headers, QJsonDocument(params).toJson(QJsonDocument::Compact));

    return invoker;
}

const HttpRequestInvoker *SRCLinkApiClient::putDownlinkStatus(const QString &sourceUuid)
{
    CHECK_CLIENT_TOKEN(nullptr);

    auto url = QUrl(QString(DOWNLINK_STATUS_URL).arg(sourceUuid));
    QMap<QByteArray, QByteArray> headers = {{"Content-Type", "application/json"}};

    API_LOG("Putting downlink status: %s", qUtf8Printable(sourceUuid));
    auto invoker = new HttpRequestInvoker(sequencer, this);
    connect(invoker, &HttpRequestInvoker::finished, this, [this, sourceUuid](HttpError error, QByteArray replyData) {
        if (error != HttpError::NoError) {
            ERROR_LOG("Putting downlink status %s failed: %d", qUtf8Printable(sourceUuid), HTTP_ERR(error));
            emit putDownlinkStatusFailed(sourceUuid);
            emit downlinkFailed(sourceUuid);
            return;
        }

        DownlinkInfo newDownlink = QJsonDocument::fromJson(replyData).object();
        if (!newDownlink.isValid()) {
            ERROR_LOG("Received malformed downlink data.");
            API_LOG("dump=%s", replyData.toStdString().c_str());
            emit putDownlinkStatusFailed(sourceUuid);
            emit downlinkFailed(sourceUuid);
            return;
        }

        downlinks[sourceUuid] = newDownlink;
        obs_log(
            LOG_DEBUG, "Put downlink status %s succeeded", qUtf8Printable(downlinks[sourceUuid].getConnection().getId())
        );

        emit putDownlinkStatusSucceeded(downlinks[sourceUuid]);
        emit downlinkReady(downlinks[sourceUuid]);
    });
    invoker->put(url, headers, QJsonDocument().toJson(QJsonDocument::Compact));

    return invoker;
}

const HttpRequestInvoker *SRCLinkApiClient::deleteDownlink(const QString &sourceUuid, const bool parallel)
{
    CHECK_CLIENT_TOKEN(nullptr);

    auto url = QUrl(QString(DOWNLINK_URL).arg(sourceUuid));

    API_LOG("Deleting downlink of %s", qUtf8Printable(sourceUuid));
    auto invoker = !parallel ? new HttpRequestInvoker(sequencer, this)
                             : new HttpRequestInvoker(httpClient, oauth2Client, this);
    connect(invoker, &HttpRequestInvoker::finished, this, [this, sourceUuid](HttpError error, QByteArray) {
        if (error != HttpError::NoError) {
            ERROR_LOG("Deleting downlink of %s failed: %d", qUtf8Printable(sourceUuid), HTTP_ERR(error));
            emit deleteDownlinkFailed(sourceUuid);
            return;
        }
        API_LOG("Delete downlink of %s succeeded", qUtf8Printable(sourceUuid));

        websocket->unsubscribe("downlink", {{"uuid", sourceUuid}});

        if (downlinks.contains(sourceUuid)) {
            downlinks.remove(sourceUuid);
            emit deleteDownlinkSucceeded(sourceUuid);
        }
    });
    invoker->deleteResource(url);

    return invoker;
}

const HttpRequestInvoker *SRCLinkApiClient::putUplink()
{
    CHECK_CLIENT_TOKEN(nullptr);

    auto url = QUrl(QString(UPLINK_URL).arg(uuid));
    QMap<QByteArray, QByteArray> headers = {{"Content-Type", "application/json"}};

    QJsonObject body;
    auto participantId = settings->getParticipantId();
    body["participant_id"] = participantId != PARTICIPANT_SEELCTION_NONE ? participantId : "";
    body["uplink_status"] = uplinkStatus;
    body["protocols"] = QJsonArray({"srt", "rtmp"});
    body["relay_apps"] = QJsonArray({RELAY_APP_SRTRELAY, RELAY_APP_MEDIAMTX});
    body["force"] = "true";

    API_LOG(
        "Putting uplink of %s (participant=%s, force=%s)", qUtf8Printable(uuid),
        qUtf8Printable(body["participant_id"].toString()), qUtf8Printable(body["force"].toString())
    );
    auto invoker = new HttpRequestInvoker(sequencer, this);
    connect(invoker, &HttpRequestInvoker::finished, this, [this](HttpError error, QByteArray replyData) {
        if (terminating) {
            WARNING_LOG("Ignore the response during terminating");
            return;
        }
        if (error != HttpError::NoError) {
            if (error == HttpError::ContentConflict) {
                ERROR_LOG("%s", obs_module_text("UuidConflictErrorDueToSecurity"));
            } else {
                ERROR_LOG("Putting uplink of %s failed: %d", qUtf8Printable(uuid), HTTP_ERR(error));
            }
            emit putUplinkFailed(uuid, error);
            emit uplinkFailed(uuid);
            return;
        }
        API_LOG("Put uplink of %s succeeded", qUtf8Printable(uuid));

        UplinkInfo newUplink = QJsonDocument::fromJson(replyData).object();
        if (!newUplink.isValid()) {
            ERROR_LOG("Received malformed uplink data.");
            API_LOG("dump=%s", replyData.toStdString().c_str());
            emit putUplinkFailed(uuid, HttpError::UnknownContentError);
            emit uplinkFailed(uuid);
            return;
        }

        uplink = newUplink;

        websocket->subscribe("uplink", {{"uuid", uuid}});

        emit putUplinkSucceeded(uplink);
        emit uplinkReady(uplink);
    });
    invoker->put(url, headers, QJsonDocument(body).toJson(QJsonDocument::Compact));

    return invoker;
}

const HttpRequestInvoker *SRCLinkApiClient::putUplinkStatus()
{
    CHECK_CLIENT_TOKEN(nullptr);

    auto url = QUrl(QString(UPLINK_STATUS_URL).arg(uuid));
    QMap<QByteArray, QByteArray> headers = {{"Content-Type", "application/json"}};

    QJsonObject body;
    body["uplink_status"] = uplinkStatus;

    API_LOG("Putting uplink status of %s", qUtf8Printable(uuid));
    auto invoker = new HttpRequestInvoker(sequencer, this);
    connect(invoker, &HttpRequestInvoker::finished, this, [this](HttpError error, QByteArray replyData) {
        if (terminating) {
            WARNING_LOG("Ignore the response during terminating");
            return;
        }
        if (error != HttpError::NoError) {
            ERROR_LOG("Putting uplink status of %s failed: %d", qUtf8Printable(uuid), HTTP_ERR(error));
            emit putUplinkStatusFailed(uuid);
            emit uplinkFailed(uuid);
            return;
        }
        API_LOG("Put uplink status of %s succeeded", qUtf8Printable(uuid));

        UplinkInfo newUplink = QJsonDocument::fromJson(replyData).object();
        if (!newUplink.isValid()) {
            ERROR_LOG("Received malformed uplink data.");
            API_LOG("dump=%s", replyData.toStdString().c_str());
            emit putUplinkStatusFailed(uuid);
            emit uplinkFailed(uuid);
            return;
        }

        uplink = newUplink;

        emit putUplinkStatusSucceeded(uplink);
        emit uplinkReady(uplink);
    });
    invoker->put(url, headers, QJsonDocument(body).toJson(QJsonDocument::Compact));

    return invoker;
}

const HttpRequestInvoker *SRCLinkApiClient::deleteUplink(const bool parallel)
{
    CHECK_CLIENT_TOKEN(nullptr);

    auto url = QUrl(QString(UPLINK_URL).arg(uuid));

    API_LOG("Deleting uplink of %s", qUtf8Printable(uuid));
    auto invoker = !parallel ? new HttpRequestInvoker(sequencer, this)
                             : new HttpRequestInvoker(httpClient, oauth2Client, this);
    connect(invoker, &HttpRequestInvoker::finished, this, [this](HttpError error, QByteArray) {
        if (error != HttpError::NoError) {
            ERROR_LOG("Deleting uplink of %s failed: %d", qUtf8Printable(uuid), HTTP_ERR(error));
            emit deleteUplinkFailed(uuid);
            return;
        }
        API_LOG("Delete uplink %s succeeded", qUtf8Printable(uuid));

        websocket->unsubscribe("uplink", {{"uuid", uuid}});

        uplink = QJsonObject();

        emit deleteUplinkSucceeded(uuid);
    });
    invoker->deleteResource(url);

    return invoker;
}

const HttpRequestInvoker *SRCLinkApiClient::redeemInviteCode(const QString &inviteCode)
{
    CHECK_CLIENT_TOKEN(nullptr);

    auto url = QUrl(QString(MEMBER_ACTIVATE_URL));
    QMap<QByteArray, QByteArray> headers = {{"Content-Type", "application/json"}};

    QJsonObject body;
    body["invite_code"] = inviteCode;

    API_LOG("Activating member for %s", qUtf8Printable(inviteCode));
    auto invoker = new HttpRequestInvoker(sequencer, this);
    connect(invoker, &HttpRequestInvoker::finished, this, [this, inviteCode](HttpError error, QByteArray replyData) {
        if (error != HttpError::NoError) {
            ERROR_LOG("Activating membership for %s failed: %d", qUtf8Printable(inviteCode), HTTP_ERR(error));
            emit redeemInviteCodeFailed(inviteCode);
            return;
        }
        API_LOG("Activating member for %s succeeded", qUtf8Printable(inviteCode));

        MemberActivationResult result = QJsonDocument::fromJson(replyData).object();

        // Marge participants
        for (const auto &participant : result.getParticipants().values()) {
            auto index = participants.findIndex([participant](const PartyEventParticipant &p) {
                return p.getId() == participant.getId();
            });
            if (index >= 0) {
                participants.replace(index, participant);
            } else {
                participants.append(participant);
            }
        }

        if (result.getParticipants().size() > 0) {
            // Change current participant to activated one
            settings->setParticipantId(result.getParticipants().values()[0].getId());
            // Put uplink now
            putUplink();
        }

        emit redeemInviteCodeSucceeded(result);
        emit participantsReady(participants);
    });
    invoker->post(url, headers, QJsonDocument(body).toJson(QJsonDocument::Compact));

    return invoker;
}

// Upload statistics via wewbsocket
void SRCLinkApiClient::putStatistics(
    const QString &sourceName, const QString &status, bool recording, const OutputMetric &metric
)
{
    CHECK_CLIENT_TOKEN();

    json payload;

    payload["uuid"] = qUtf8Printable(uuid);
    payload["source_name"] = qUtf8Printable(sourceName);
    payload["status"] = qUtf8Printable(status);
    payload["recording"] = recording;
    payload["metric"] = {
        {"bitrate", metric.getBitrate()},
        {"total_frames", metric.getTotalFrames()},
        {"dropped_frames", metric.getDroppedFrames()},
        {"total_size", metric.getTotalSize()}
    };

    websocket->invokeBin("statistics.put", payload);
}

// Upload screenshot via websocket
void SRCLinkApiClient::putScreenshot(const QString &sourceName, const QImage &image)
{
    CHECK_CLIENT_TOKEN();

    QByteArray imageBytes;
    QBuffer imageBuffer(&imageBytes);
    imageBuffer.open(QIODevice::WriteOnly);
    image.save(&imageBuffer, "JPG", SCREENSHOT_QUALITY);

    json payload;

    payload["uuid"] = qUtf8Printable(uuid);
    payload["source_name"] = qUtf8Printable(sourceName);
    payload["mime_type"] = "image/jpeg";

    auto bufferPtr = imageBuffer.data().constData();
    payload["body"] = json::binary_t(std::vector<uint8_t>(bufferPtr, bufferPtr + imageBuffer.data().size()));

    websocket->invokeBin("screenshots.put", payload);
}

void SRCLinkApiClient::getPicture(const QString &pictureId)
{
    Q_ASSERT(httpClient && httpClient->parent() == this);
    // Uses CurlHttpClient directly (bypasses HttpRequestInvoker) because this is a
    // public endpoint that does not require authentication or 401 retry logic.
    // FIXME: If the server auth model ever requires Bearer tokens for picture URLs,
    // switch this back to HttpRequestInvoker (or inject the Authorization header here).
    QPointer<SRCLinkApiClient> self = this;
    httpClient->get(QUrl(QString(PICTURES_URL).arg(pictureId)), {}, [self, pictureId](HttpResponse response) {
        if (!self) {
            return;
        }
        if (response.error != HttpError::NoError) {
            ERROR_LOG("Getting picture of %s failed: %d", qUtf8Printable(pictureId), response.statusCode);
            emit self->getPictureFailed(pictureId);
            return;
        }
        API_LOG("Get picture of %s succeeded", qUtf8Printable(pictureId));

        auto picture = QImage::fromData(response.data);

        emit self->getPictureSucceeded(pictureId, picture);
    });
}

void SRCLinkApiClient::openStagesPage()
{
    QDesktopServices::openUrl(QUrl(STAGES_PAGE_URL));
}

void SRCLinkApiClient::openControlPanelPage()
{
    QDesktopServices::openUrl(QUrl(CONTROL_PANEL_PAGE_URL));
}

void SRCLinkApiClient::openMembershipsPage()
{
    QDesktopServices::openUrl(QUrl(MEMBERSHIPS_PAGE));
}

void SRCLinkApiClient::openSignupPage()
{
    QDesktopServices::openUrl(QUrl(SIGNUP_PAGE));
}

void SRCLinkApiClient::openWsPortalsPage()
{
    QDesktopServices::openUrl(QUrl(WS_PORTALS_PAGE));
}

void SRCLinkApiClient::openGuestCodesPage()
{
    QDesktopServices::openUrl(QUrl(GUEST_CODES_PAGE));
}

void SRCLinkApiClient::onOAuth2OpenBrowser(const QUrl &url)
{
    QDesktopServices::openUrl(url);
}

void SRCLinkApiClient::onOAuth2LinkedChanged()
{
    CHECK_CLIENT_TOKEN();

    API_LOG("The API client link has been changed.");
}

// This is called when link, unlink or refresh token succeeded
void SRCLinkApiClient::onOAuth2LinkingSucceeded()
{
    if (oauth2Client->linked()) {
        CHECK_CLIENT_TOKEN();
        INFO_LOG("The API client has linked up.");

        if (accountInfo.isEmpty()) {
            // Called only the first time after logging in
            if (auto *invoker = requestAccountInfo()) {
                connect(invoker, &HttpRequestInvoker::finished, this, [this](HttpError error) {
                    if (error != HttpError::NoError) {
                        return;
                    }

                    //syncOnlineResources(); // Now received by WebSocket
                    if (auto *uplinkInvoker = putUplink()) {
                        connect(uplinkInvoker, &HttpRequestInvoker::finished, this, [this](HttpError) {
                            // WebSocket will be started even if uplink upload failed.
                            websocket->start();
                        });
                    }
                });
            }
        }

        // This signal needs to be emit.
        emit loginSucceeded();

    } else {
        INFO_LOG("The API client has unlinked.");

        websocket->stop();
        clearOnlineResources();

        emit logoutSucceeded();
    }
}

void SRCLinkApiClient::onOAuth2LinkingFailed()
{
    ERROR_LOG("The API client linking failed.");

    websocket->stop();

    emit loginFailed();
}

void SRCLinkApiClient::onOAuth2RefreshFinished(HttpError error)
{
    if (error != HttpError::NoError) {
        return;
    }
    CHECK_CLIENT_TOKEN();

    // Schedule next refresh (guard against negative/overflow values)
    qint64 refreshInterval = oauth2Client->expires() * 1000 - 60000 - QDateTime::currentMSecsSinceEpoch();
    tokenRefreshTimer->start(
        static_cast<int>(std::clamp(refreshInterval, static_cast<qint64>(1000), static_cast<qint64>(INT_MAX)))
    );
}

void SRCLinkApiClient::onWebSocketReady(bool reconnect)
{
    API_LOG("WebSocket is ready.");
    // The account info will be received by requestAccountInfo() except on reconnecting
    websocket->subscribe("accounts", {{"initial_data", reconnect}});
    // The uplink will be received by WebSocket except on reconnecting
    websocket->subscribe("uplink", {{"uuid", uuid}, {"initial_data", !reconnect}});

    websocket->subscribe("stages", {{"initial_data", true}});
    websocket->subscribe("participants", {{"initial_data", true}});
    websocket->subscribe("ws-portals", {{"initial_data", true}});

    foreach (auto sourceUuid, downlinks.keys()) {
        // The downlink wil be received by putDownlinkStatus()
        websocket->subscribe("downlink", {{"uuid", sourceUuid}});

        if (reconnect) {
            putDownlinkStatus(sourceUuid);
        }
    }

    if (reconnect) {
        //requestAccountInfo(); // Now received by WebSocket
        //syncOnlineResources(); // Now received by WebSocket
        syncUplinkStatus(true);
    }

    emit ready(reconnect);
}

void SRCLinkApiClient::onWebSocketAborted(const QString &reason)
{
    ERROR_LOG("WebSocket is aborted: %s", qUtf8Printable(reason));
    if (reason == "token-expired" || reason == "not-authorized") {
        // Try to refresh access token
        refresh();
    }
}

void SRCLinkApiClient::onWebSocketDisconnected()
{
    emit webSocketDisconnected();
}

void SRCLinkApiClient::onWebSocketDataChanged(const WebSocketMessage &message)
{
    auto name = message.getName();
    auto id = message.getId();
    auto payload = message.getPayload();
    API_LOG("WebSocket data changed: %s,%s,%d", qUtf8Printable(name), qUtf8Printable(id), message.getContinuous());

    blockSignals(message.getContinuous());
    [&]() {
        if (name == "uplink.allocations") {
            StageSeatAllocation newAllocation = payload;
            if (!newAllocation.isValid()) {
                ERROR_LOG("Malformed allocation data received.");
                API_LOG("dump=%s", dumpJsonObject(payload).c_str());
                return;
            }

            uplink.setAllocation(newAllocation);
            emit uplinkReady(uplink);

        } else if (name == "uplink.stages") {
            Stage newStage = payload;
            if (!newStage.isValid()) {
                ERROR_LOG("Malformed stage data received.");
                API_LOG("dump=%s", dumpJsonObject(payload).c_str());
                return;
            }

            uplink.setStage(newStage);
            emit uplinkReady(uplink);

        } else if (name == "uplink.connections") {
            StageConnection newConnection = payload;
            if (!newConnection.isValid()) {
                ERROR_LOG("Malformed connection data received.");
                API_LOG("dump=%s", dumpJsonObject(payload).c_str());
                return;
            }

            auto connections = uplink.getConnections();
            auto index =
                connections.findIndex([id](const StageConnection &connection) { return connection.getId() == id; });
            if (index >= 0) {
                connections.replace(index, newConnection);
            } else {
                connections.append(newConnection);
            }
            uplink["connections"] = connections;
            emit uplinkReady(uplink);

        } else if (name == "downlink.connections") {
            StageConnection newConnection = payload;
            if (!newConnection.isValid()) {
                ERROR_LOG("Malformed connection data received.");
                API_LOG("dump=%s", dumpJsonObject(payload).c_str());
                return;
            }

            downlinks[id]["connection"] = newConnection;
            emit downlinkReady(downlinks[id]);

        } else if (name == "stages") {
            Stage newStage = payload;
            if (!newStage.isValid()) {
                ERROR_LOG("Malformed stage data received.");
                API_LOG("dump=%s", dumpJsonObject(payload).c_str());
                return;
            }

            auto index = stages.findIndex([id](const Stage &stage) { return stage.getId() == id; });
            if (index >= 0) {
                stages.replace(index, newStage);
            } else {
                stages.append(newStage);
            }
            emit stagesReady(stages);

        } else if (name == "participants") {
            PartyEventParticipant newParticipant = payload;
            if (!newParticipant.isValid()) {
                ERROR_LOG("Malformed participant data received.");
                API_LOG("dump=%s", dumpJsonObject(payload).c_str());
                return;
            }

            auto index = participants.findIndex([id](const PartyEventParticipant &p) { return p.getId() == id; });
            if (index >= 0) {
                participants.replace(index, newParticipant);
            } else {
                participants.append(newParticipant);
            }
            emit participantsReady(participants);

        } else if (name == "accounts") {
            Account newAccount = payload;
            if (!newAccount.isValid()) {
                ERROR_LOG("Malformed account data received.");
                API_LOG("dump=%s", dumpJsonObject(payload).c_str());
                return;
            }

            accountInfo.setAccount(newAccount);
            emit accountInfoReady(accountInfo);

        } else if (name == "accounts.licenses") {
            SubscriptionLicense newLicense = payload;
            if (!newLicense.isValid()) {
                ERROR_LOG("Malformed license data received.");
                API_LOG("dump=%s", dumpJsonObject(payload).c_str());
                return;
            }

            auto emitLicenseChanged = !accountInfo.isEmpty() && accountInfo.getSubscriptionLicense().getLicenseValid(
                                                                ) != newLicense.getLicenseValid();

            accountInfo.setSubscriptionLicense(newLicense);
            emit accountInfoReady(accountInfo);

            if (emitLicenseChanged) {
                emit licenseChanged(newLicense);
            }

        } else if (name == "accounts.resourceUsage") {
            AccountResourceUsage newResourceUsage = payload;
            if (!newResourceUsage.isValid()) {
                ERROR_LOG("Malformed resource usage data received.");
                API_LOG("dump=%s", dumpJsonObject(payload).c_str());
                return;
            }

            accountInfo.setResourceUsage(newResourceUsage);
            emit accountInfoReady(accountInfo);

        } else if (name == "ws-portals") {
            WsPortal newPortal = payload;
            if (!newPortal.isValid()) {
                ERROR_LOG("Malformed portal data received.");
                API_LOG("dump=%s", dumpJsonObject(payload).c_str());
                return;
            }

            auto index = wsPortals.findIndex([id](const WsPortal &portal) { return portal.getId() == id; });
            if (index >= 0) {
                wsPortals.replace(index, newPortal);
            } else {
                wsPortals.append(newPortal);
            }
            emit wsPortalsReady(wsPortals);
        }
    }();
    blockSignals(false);
}

void SRCLinkApiClient::onWebSocketDataRemoved(const WebSocketMessage &message)
{
    auto name = message.getName();
    auto id = message.getId();
    API_LOG("WebSocket data removed: %s,%s,%d", qUtf8Printable(name), qUtf8Printable(id), message.getContinuous());

    blockSignals(message.getContinuous());
    [&]() {
        if (name == "uplink.allocations") {
            if (uplink.getAllocation().getId() == id) {
                uplink.remove("allocation");
                emit uplinkReady(uplink);
            }

        } else if (name == "uplink.stages") {
            if (uplink.getStage().getId() == id) {
                uplink.remove("stage");
                emit uplinkReady(uplink);
            }

        } else if (name == "uplink.connections") {
            auto connections = uplink.getConnections();
            auto index =
                connections.findIndex([id](const StageConnection &connection) { return connection.getId() == id; });
            if (index >= 0) {
                connections.removeAt(index);
                uplink["connections"] = connections;
                emit uplinkReady(uplink);
            }

        } else if (name == "downlink.connections") {
            if (downlinks.contains(id)) {
                downlinks.remove(id);
                emit downlinkRemoved(id);
            }

        } else if (name == "stages") {
            auto index = stages.findIndex([id](const Stage &stage) { return stage.getId() == id; });
            if (index >= 0) {
                stages.removeAt(index);
                emit stagesReady(stages);
            }

        } else if (name == "participants") {
            auto index = participants.findIndex([id](const PartyEventParticipant &participant) {
                return participant.getId() == id;
            });
            if (index >= 0) {
                participants.removeAt(index);
                emit participantsReady(participants);
            }

        } else if (name == "accounts.licenses" || name == "accounts.resourceUsage") {
            // Do nothing

        } else if (name == "accounts") {
            // Account removed -> logout immediately
            logout();

        } else if (name == "ws-portals") {
            auto index = wsPortals.findIndex([id](const WsPortal &portal) { return portal.getId() == id; });
            if (index >= 0) {
                wsPortals.removeAt(index);
                emit wsPortalsReady(wsPortals);
            }
        }
    }();
    blockSignals(false);
}
