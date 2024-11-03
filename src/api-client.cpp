/*
SR Link
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

#include <QMessageBox>
#include <QRandomGenerator>
#include <QDesktopServices>
#include <QMetaEnum>
#include <QString>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPromise>
#include <QTimer>
#include <QByteArray>
#include <QBuffer>

#include <o0settingsstore.h>
#include <o0globals.h>

#include "plugin-support.h"
#include "api-client.hpp"
#include "api-websocket.hpp"
#include "utils.hpp"

//#define LOCAL_DEBUG
#define SCOPE "read write"
#define SCREENSHOT_QUALITY 75

// REST Endpoints
#ifdef LOCAL_DEBUG
#define API_SERVER "http://localhost:3000"
#define API_WS_SERVER "ws://localhost:3000"
#else
#define API_SERVER "https://source-link-test.opensphere.co.jp"
#define API_WS_SERVER "wss://source-link-test.opensphere.co.jp"
#endif
#define AUTHORIZE_URL (API_SERVER "/oauth2/authorize")
#define TOKEN_URL (API_SERVER "/oauth2/token")
#define ACCOUNT_INFO_URL (API_SERVER "/api/v1/accounts/me")
#define PARTIES_URL (API_SERVER "/api/v1/parties/my")
#define PARTY_EVENTS_URL (API_SERVER "/api/v1/events/my")
#define PARTICIPANTS_URL (API_SERVER "/api/v1/participants/my")
#define STAGES_URL (API_SERVER "/api/v1/stages")
#define DOWNLINK_URL (API_SERVER "/api/v1/downlink/%1")
#define UPLINK_URL (API_SERVER "/api/v1/uplink/%1")
#define UPLINK_STATUS_URL (API_SERVER "/api/v1/uplink/%1/status")
#define SCREENSHOTS_URL (API_SERVER "/api/v1/screenshots/%1/%2")
#define PICTURES_URL (API_SERVER "/pictures/%1")
#define STAGES_MANAGEMENT_PAGE_URL (API_SERVER "/stages")
#define WEBSOCKET_URL (API_WS_SERVER "/api/v1/websocket")

// Embedded client ID and secret
#define CLIENT_ID "testClientId"
#define CLIENT_SECRET "testClientSecret"

//--- Macros ---//
#define QENUM_NAME(o, e, v) (o::staticMetaObject.enumerator(o::staticMetaObject.indexOfEnumerator(#e)).valueToKey((v)))
#define GRANTFLOW_STR(v) QString(QENUM_NAME(O2, GrantFlow, v))

#define CHECK_CLIENT_TOKEN(...)                             \
    {                                                       \
        if (client->refreshToken().isEmpty()) {             \
            obs_log(LOG_ERROR, "client: No access token."); \
            return __VA_ARGS__;                             \
        }                                                   \
    }

#define CHECK_RESPONSE_NOERROR(signal, message, ...)  \
    {                                                 \
        if (error != QNetworkReply::NoError) {        \
            obs_log(LOG_ERROR, message, __VA_ARGS__); \
            emit signal();                            \
            return;                                   \
        }                                             \
    }

//--- SRLinkApiClient class ---//

SRLinkApiClient::SRLinkApiClient(QObject *parent)
    : QObject(parent),
      // Create store object for writing the received tokens (It will be child of O2 instance)
      settings(new SRLinkSettingsStore()),
      networkManager(nullptr),
      client(nullptr),
      activeOutputs(0),
      standByOutputs(0),
      uplinkStatus(UPLINK_STATUS_INACTIVE)
{
    obs_log(LOG_DEBUG, "client: SRLinkApiClient creating with %s", API_SERVER);

    networkManager = new QNetworkAccessManager(this);
    client = new O2(this, networkManager, settings);
    sequencer = new RequestSequencer(networkManager, client, this);
    websocket = new SRLinkWebSocketClient(QUrl(WEBSOCKET_URL), this, this);

    uuid = settings->value("uuid");
    if (uuid.isEmpty()) {
        // Generate new UUID for the client
        auto defaultUuid = os_generate_uuid();
        uuid = QString(defaultUuid);
        settings->setValue("uuid", uuid);
        bfree(defaultUuid);
    }

    client->setRequestUrl(AUTHORIZE_URL);
    client->setTokenUrl(TOKEN_URL);
    client->setRefreshTokenUrl(TOKEN_URL);
    client->setClientId(CLIENT_ID);
    // FIXME: o2 didn't support PKCE yet, so we use embeded client secret for now
    client->setClientSecret(CLIENT_SECRET);
    client->setLocalPort(QRandomGenerator::system()->bounded(8000, 9000));
    client->setScope(QString::fromLatin1(SCOPE));
    client->setGrantFlow(O2::GrantFlow::GrantFlowAuthorizationCode);
    client->setReplyContent(
        "<html><body><h2>Authorization complete. You can close this window and return to OBS Studio</h2></body></html>"
    );

    connect(client, SIGNAL(linkedChanged()), this, SLOT(onO2LinkedChanged()));
    connect(client, SIGNAL(linkingSucceeded()), this, SLOT(onO2LinkingSucceeded()));
    connect(client, SIGNAL(linkingFailed()), this, SLOT(onO2LinkingFailed()));
    connect(client, SIGNAL(openBrowser(QUrl)), this, SLOT(onO2OpenBrowser(QUrl)));
    connect(
        client, SIGNAL(refreshFinished(QNetworkReply::NetworkError)), this,
        SLOT(onO2RefreshFinished(QNetworkReply::NetworkError))
    );
    connect(websocket, SIGNAL(ready(bool)), this, SLOT(onWebSocketReady(bool)));
    connect(websocket, SIGNAL(disconnected()), this, SLOT(onWebSocketDisconnected()));
    connect(
        websocket, SIGNAL(added(const QString &, const QString &, const QJsonObject &)), this,
        SLOT(onWebSocketDataChanged(const QString &, const QString &, const QJsonObject &))
    );
    connect(
        websocket, SIGNAL(changed(const QString &, const QString &, const QJsonObject &)), this,
        SLOT(onWebSocketDataChanged(const QString &, const QString &, const QJsonObject &))
    );
    connect(
        websocket, SIGNAL(removed(const QString &, const QString &, const QJsonObject &)), this,
        SLOT(onWebSocketDataRemoved(const QString &, const QString &, const QJsonObject &))
    );

    if (client->expires() - 60 <= QDateTime().currentSecsSinceEpoch()) {
        // Refresh token now
        auto invoker = new RequestInvoker(sequencer, this);
        invoker->refresh();
    } else {
        connect(requestAccountInfo(), &RequestInvoker::finished, this, [this](QNetworkReply::NetworkError error) {
            if (error == QNetworkReply::NoError) {
                resyncOnlineResources();

                connect(putUplink(), &RequestInvoker::finished, this, [this](QNetworkReply::NetworkError error) {
                    if (error != QNetworkReply::NoError) {
                        return;
                    }
                    websocket->start();
                });
            }
        });

        // Schedule next refresh
        QTimer::singleShot(
            client->expires() * 1000 - 60000 - QDateTime().currentMSecsSinceEpoch(), client, SLOT(refresh())
        );
    }

    obs_log(LOG_DEBUG, "client: SRLinkApiClient created");
}

SRLinkApiClient::~SRLinkApiClient()
{
    obs_log(LOG_DEBUG, "client: SRLinkApiClient destroyed");
}

void SRLinkApiClient::login()
{
    obs_log(
        LOG_DEBUG, "client: Starting OAuth 2 with grant flow type %s", qPrintable(GRANTFLOW_STR(client->grantFlow()))
    );
    client->link();
}

void SRLinkApiClient::logout()
{
    client->unlink();
}

bool SRLinkApiClient::isLoggedIn()
{
    return client->linked();
}

const RequestInvoker *SRLinkApiClient::refresh()
{
    auto invoker = new RequestInvoker(sequencer, this);
    invoker->refresh();
    return invoker;
}

const int SRLinkApiClient::getFreePort()
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

void SRLinkApiClient::releasePort(const int port)
{
    usedPorts[port] = false;
}

void SRLinkApiClient::resyncOnlineResources()
{
    CHECK_CLIENT_TOKEN();

    requestParties();
    requestPartyEvents();
    requestParticipants();
    requestStages();
}

void SRLinkApiClient::clearOnlineResources()
{
    accountInfo = AccountInfo();
    parties = PartyArray();
    partyEvents = PartyEventArray();
    participants = PartyEventParticipantArray();
    stages = StageArray();
    uplink = UplinkInfo();
    downlinks.clear();
}

void SRLinkApiClient::terminate()
{
    obs_log(LOG_DEBUG, "client: Terminating API client.");
    deleteUplink(true);
}

// Call putUplinkStatus() when uplinkStatus is changed
void SRLinkApiClient::syncUplinkStatus()
{
    auto nextUplinkStatus = activeOutputs > 0    ? UPLINK_STATUS_ACTIVE
                            : standByOutputs > 0 ? UPLINK_STATUS_STANDBY
                                                 : UPLINK_STATUS_INACTIVE;
    if (nextUplinkStatus != uplinkStatus) {
        uplinkStatus = nextUplinkStatus;
        putUplinkStatus();
    }
}

const RequestInvoker *SRLinkApiClient::requestAccountInfo()
{
    CHECK_CLIENT_TOKEN(nullptr);

    obs_log(LOG_DEBUG, "client: Requesting account info.");
    auto invoker = new RequestInvoker(sequencer, this);
    connect(invoker, &RequestInvoker::finished, this, [this](QNetworkReply::NetworkError error, QByteArray replyData) {
        CHECK_RESPONSE_NOERROR(accountInfoFailed, "client: Requesting account info failed: %d", error);

        accountInfo = QJsonDocument::fromJson(replyData).object();
        obs_log(LOG_DEBUG, "client: Received account: %s", qPrintable(accountInfo.getDisplayName()));

        emit accountInfoReady(accountInfo);
    });
    invoker->get(QNetworkRequest(QUrl(ACCOUNT_INFO_URL)));

    return invoker;
}

const RequestInvoker *SRLinkApiClient::requestParties()
{
    CHECK_CLIENT_TOKEN(nullptr);

    obs_log(LOG_DEBUG, "client: Requesting parties.");
    auto invoker = new RequestInvoker(sequencer, this);
    connect(invoker, &RequestInvoker::finished, [this](QNetworkReply::NetworkError error, QByteArray replyData) {
        CHECK_RESPONSE_NOERROR(partiesFailed, "client: Requesting parties failed: %d", error);

        parties = QJsonDocument::fromJson(replyData).array();
        obs_log(LOG_DEBUG, "client: Received %d parties", parties.size());

        if (settings->getPartyId().isEmpty() && parties.size() > 0) {
            settings->setPartyId(parties[0].getId());
        }

        emit partiesReady(parties);
    });
    invoker->get(QNetworkRequest(QUrl(PARTIES_URL)));

    return invoker;
}

const RequestInvoker *SRLinkApiClient::requestPartyEvents()
{
    CHECK_CLIENT_TOKEN(nullptr);

    obs_log(LOG_DEBUG, "client: Requesting party events");
    auto invoker = new RequestInvoker(sequencer, this);
    connect(invoker, &RequestInvoker::finished, [this](QNetworkReply::NetworkError error, QByteArray replyData) {
        CHECK_RESPONSE_NOERROR(partyEventsFailed, "client: Requesting party events failed: %d", error);

        partyEvents = QJsonDocument::fromJson(replyData).array();
        obs_log(LOG_DEBUG, "client: Received %d party events", partyEvents.size());

        emit partyEventsReady(partyEvents);
    });
    invoker->get(QNetworkRequest(QUrl(PARTY_EVENTS_URL)));

    return invoker;
}

const RequestInvoker *SRLinkApiClient::requestParticipants()
{
    CHECK_CLIENT_TOKEN(nullptr);

    obs_log(LOG_DEBUG, "client: Requesting participants");
    auto invoker = new RequestInvoker(sequencer, this);
    connect(invoker, &RequestInvoker::finished, [this](QNetworkReply::NetworkError error, QByteArray replyData) {
        CHECK_RESPONSE_NOERROR(participantsFailed, "client: Requesting participants failed: %d", error);

        participants = QJsonDocument::fromJson(replyData).array();
        obs_log(LOG_DEBUG, "client: Received %d participants", participants.size());

        if (settings->getParticipantId().isEmpty() && participants.size() > 0) {
            settings->setParticipantId(participants[0].getId());
        }

        emit participantsReady(participants);
    });
    invoker->get(QNetworkRequest(QUrl(PARTICIPANTS_URL)));

    return invoker;
}

const RequestInvoker *SRLinkApiClient::requestStages()
{
    CHECK_CLIENT_TOKEN(nullptr);

    obs_log(LOG_DEBUG, "client: Requesting receivers.");
    auto invoker = new RequestInvoker(sequencer, this);
    connect(invoker, &RequestInvoker::finished, [this](QNetworkReply::NetworkError error, QByteArray replyData) {
        CHECK_RESPONSE_NOERROR(stagesFailed, "client: Requesting receivers failed: %d", error);

        stages = QJsonDocument::fromJson(replyData).array();
        obs_log(LOG_DEBUG, "client: Received %d receivers", stages.size());

        emit stagesReady(stages);
    });
    invoker->get(QNetworkRequest(QUrl(STAGES_URL)));

    return invoker;
}

const RequestInvoker *SRLinkApiClient::requestUplink()
{
    CHECK_CLIENT_TOKEN(nullptr);

    obs_log(LOG_DEBUG, "client: Requesting uplink for %s", qPrintable(uuid));
    auto invoker = new RequestInvoker(sequencer, this);
    connect(invoker, &RequestInvoker::finished, [this](QNetworkReply::NetworkError error, QByteArray replyData) {
        if (error != QNetworkReply::NoError) {
            obs_log(LOG_ERROR, "client: Requesting uplink for %s failed: %d", qPrintable(uuid), error);
            emit uplinkFailed(uuid);
            return;
        }
        obs_log(LOG_DEBUG, "client: Received uplink for %s", qPrintable(uuid));

        uplink = QJsonDocument::fromJson(replyData).object();

        emit uplinkReady(uplink);
    });
    invoker->get(QNetworkRequest(QUrl(QString(UPLINK_URL).arg(uuid))));

    return invoker;
}

const RequestInvoker *SRLinkApiClient::requestDownlink(const QString &sourceUuid)
{
    CHECK_CLIENT_TOKEN(nullptr);

    obs_log(LOG_DEBUG, "client: Requesting downlink for %s", qPrintable(sourceUuid));
    auto invoker = new RequestInvoker(sequencer, this);
    connect(
        invoker, &RequestInvoker::finished,
        [this, sourceUuid](QNetworkReply::NetworkError error, QByteArray replyData) {
            if (error != QNetworkReply::NoError) {
                obs_log(LOG_ERROR, "client: Requesting downlink for %s failed: %d", qPrintable(sourceUuid), error);
                emit downlinkFailed(sourceUuid);
                return;
            }
            obs_log(LOG_DEBUG, "client: Received downlink for %s", qPrintable(sourceUuid));

            downlinks[sourceUuid] = QJsonDocument::fromJson(replyData).object();

            emit downlinkReady(downlinks[sourceUuid]);
        }
    );
    invoker->get(QNetworkRequest(QUrl(QString(DOWNLINK_URL).arg(sourceUuid))));

    return invoker;
}

const RequestInvoker *SRLinkApiClient::putDownlink(const QString &sourceUuid, const DownlinkRequestBody &params)
{
    CHECK_CLIENT_TOKEN(nullptr);

    auto req = QNetworkRequest(QUrl(QString(DOWNLINK_URL).arg(sourceUuid)));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    obs_log(LOG_DEBUG, "client: Putting downlink: %s rev.%d", qPrintable(sourceUuid), params.getRevision());
    auto invoker = new RequestInvoker(sequencer, this);
    connect(
        invoker, &RequestInvoker::finished,
        [this, sourceUuid, params](QNetworkReply::NetworkError error, QByteArray replyData) {
            if (error != QNetworkReply::NoError) {
                if (error != QNetworkReply::NoError) {
                    obs_log(
                        LOG_ERROR, "client: Putting downlink %s rev.%d failed: %d", qPrintable(sourceUuid),
                        params.getRevision(), error
                    );
                    emit putDownlinkFailed(sourceUuid);
                    return;
                }
            }

            downlinks[sourceUuid] = QJsonDocument::fromJson(replyData).object();
            obs_log(
                LOG_DEBUG, "client: Put downlink %s rev.%d succeeded",
                qPrintable(downlinks[sourceUuid].getConnection().getId()), params.getRevision()
            );

            websocket->subscribe("downlink", {{"uuid", sourceUuid}});

            emit putDownlinkSucceeded(downlinks[sourceUuid]);
        }
    );
    invoker->put(req, QJsonDocument(params).toJson());

    return invoker;
}

const RequestInvoker *SRLinkApiClient::deleteDownlink(const QString &sourceUuid, const bool parallel)
{
    CHECK_CLIENT_TOKEN(nullptr);

    auto req = QNetworkRequest(QUrl(QString(DOWNLINK_URL).arg(sourceUuid)));

    obs_log(LOG_DEBUG, "client: Deleting downlink of %s", qPrintable(sourceUuid));
    auto invoker = !parallel ? new RequestInvoker(sequencer, this) : new RequestInvoker(networkManager, client, this);
    connect(invoker, &RequestInvoker::finished, [this, sourceUuid](QNetworkReply::NetworkError error, QByteArray) {
        if (error != QNetworkReply::NoError) {
            obs_log(LOG_ERROR, "client: Deleting downlink of %s failed: %d", qPrintable(sourceUuid), error);
            emit deleteDownlinkFailed(sourceUuid);
            return;
        }
        obs_log(LOG_DEBUG, "client: Delete downlink of %s succeeded", qPrintable(sourceUuid));

        websocket->unsubscribe("downlink", {{"uuid", sourceUuid}});

        if (downlinks.contains(sourceUuid)) {
            downlinks.remove(sourceUuid);
            emit deleteDownlinkSucceeded(sourceUuid);
        }
    });
    invoker->deleteResource(req);

    return invoker;
}

const RequestInvoker *SRLinkApiClient::putUplink(const bool force)
{
    CHECK_CLIENT_TOKEN(nullptr);

    auto req = QNetworkRequest(QUrl(QString(UPLINK_URL).arg(uuid)));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QJsonObject body;
    body["participant_id"] = settings->getParticipantId();
    body["force"] = force || settings->getForceConnection() ? "1" : "0";
    body["uplink_status"] = uplinkStatus;

    obs_log(LOG_DEBUG, "client: Putting uplink of %s", qPrintable(uuid));
    auto invoker = new RequestInvoker(sequencer, this);
    connect(invoker, &RequestInvoker::finished, [this](QNetworkReply::NetworkError error, QByteArray replyData) {
        if (error != QNetworkReply::NoError) {
            if (error == QNetworkReply::ContentOperationNotPermittedError) {
                obs_log(LOG_ERROR, "client: Putting uplink of %s failed: %d", qPrintable(uuid), error);
                emit putUplinkFailed(uuid);
                emit uplinkFailed(uuid);
                return;
            }
        }
        obs_log(LOG_DEBUG, "client: Put uplink of %s succeeded", qPrintable(uuid));

        uplink = QJsonDocument::fromJson(replyData).object();

        websocket->subscribe("uplink", {{"uuid", uuid}});

        emit putUplinkSucceeded(uplink);
        emit uplinkReady(uplink);
    });
    invoker->put(req, QJsonDocument(body).toJson());

    return invoker;
}

const RequestInvoker *SRLinkApiClient::putUplinkStatus()
{
    CHECK_CLIENT_TOKEN(nullptr);

    auto req = QNetworkRequest(QUrl(QString(UPLINK_STATUS_URL).arg(uuid)));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QJsonObject body;
    body["uplink_status"] = uplinkStatus;

    obs_log(LOG_DEBUG, "client: Putting uplink status of %s", qPrintable(uuid));
    auto invoker = new RequestInvoker(sequencer, this);
    connect(invoker, &RequestInvoker::finished, [this](QNetworkReply::NetworkError error, QByteArray replyData) {
        if (error != QNetworkReply::NoError) {
            if (error == QNetworkReply::ContentOperationNotPermittedError) {
                obs_log(LOG_ERROR, "client: Putting uplink status of %s failed: %d", qPrintable(uuid), error);
                emit putUplinkStatusFailed(uuid);
                emit uplinkFailed(uuid);
                return;
            }
        }
        obs_log(LOG_DEBUG, "client: Put uplink status of %s succeeded", qPrintable(uuid));

        uplink = QJsonDocument::fromJson(replyData).object();

        emit putUplinkStatusSucceeded(uplink);
        emit uplinkReady(uplink);
    });
    invoker->put(req, QJsonDocument(body).toJson());

    return invoker;
}

const RequestInvoker *SRLinkApiClient::deleteUplink(const bool parallel)
{
    CHECK_CLIENT_TOKEN(nullptr);

    auto req = QNetworkRequest(QUrl(QString(UPLINK_URL).arg(uuid)));

    obs_log(LOG_DEBUG, "client: Deleting uplink of %s", qPrintable(uuid));
    auto invoker = !parallel ? new RequestInvoker(sequencer, this) : new RequestInvoker(networkManager, client, this);
    connect(invoker, &RequestInvoker::finished, [this](QNetworkReply::NetworkError error, QByteArray) {
        if (error != QNetworkReply::NoError) {
            obs_log(LOG_ERROR, "client: Deleting uplink of %s failed: %d", qPrintable(uuid), error);
            emit deleteUplinkFailed(uuid);
            return;
        }
        obs_log(LOG_DEBUG, "client: Delete uplink %s succeeded", qPrintable(uuid));

        websocket->unsubscribe("uplink", {{"uuid", uuid}});

        uplink = QJsonObject();

        emit deleteUplinkSucceeded(uuid);
    });
    invoker->deleteResource(req);

    return invoker;
}

const RequestInvoker *SRLinkApiClient::putScreenshot(const QString &sourceName, const QImage &image)
{
    CHECK_CLIENT_TOKEN(nullptr);

    auto req = QNetworkRequest(QUrl(QString(SCREENSHOTS_URL).arg(uuid).arg(sourceName)));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "image/jpeg");

    QByteArray imageBytes;
    QBuffer imageBuffer(&imageBytes);
    imageBuffer.open(QIODevice::WriteOnly);
    image.save(&imageBuffer, "JPG", SCREENSHOT_QUALITY);

    obs_log(LOG_DEBUG, "client: Putting screenshot of %s", qPrintable(sourceName));
    auto invoker = new RequestInvoker(sequencer, this);
    connect(invoker, &RequestInvoker::finished, [this, sourceName](QNetworkReply::NetworkError error, QByteArray) {
        if (error != QNetworkReply::NoError) {
            obs_log(LOG_ERROR, "client: Putting screenshot of %s failed: %d", qPrintable(sourceName), error);
            emit putScreenshotFailed(sourceName);
            return;
        }
        obs_log(LOG_DEBUG, "client: Put screenshot of %s succeeded", qPrintable(sourceName));

        emit putScreenshotSucceeded(sourceName);
    });
    invoker->put(req, imageBuffer.data());

    return invoker;
}

void SRLinkApiClient::getPicture(const QString &pictureId)
{
    auto reply = networkManager->get(QNetworkRequest(QUrl(QString(PICTURES_URL).arg(pictureId))));
    connect(reply, &QNetworkReply::finished, [this, pictureId, reply]() {
        if (reply->error() != QNetworkReply::NoError) {
            obs_log(LOG_ERROR, "client: Getting picture of %s failed: %d", qPrintable(pictureId), reply->error());
            emit getPictureFailed(pictureId);
            return;
        }
        obs_log(LOG_DEBUG, "client: Get picture of %s succeeded", qPrintable(pictureId));

        auto picture = QImage::fromData(reply->readAll());

        emit getPictureSucceeded(pictureId, picture);
        reply->deleteLater();
    });
}

void SRLinkApiClient::openStagesManagementPage()
{
    QDesktopServices::openUrl(QUrl(STAGES_MANAGEMENT_PAGE_URL));
}

void SRLinkApiClient::onO2OpenBrowser(const QUrl &url)
{
    QDesktopServices::openUrl(url);
}

void SRLinkApiClient::onO2LinkedChanged()
{
    CHECK_CLIENT_TOKEN();

    obs_log(LOG_DEBUG, "client: The API client link has been changed.");
}

// This is called when link or refresh token succeeded
void SRLinkApiClient::onO2LinkingSucceeded()
{
    if (client->linked()) {
        CHECK_CLIENT_TOKEN();
        obs_log(LOG_DEBUG, "client: The API client has linked up.");

        connect(requestAccountInfo(), &RequestInvoker::finished, this, [this](QNetworkReply::NetworkError error) {
            if (error != QNetworkReply::NoError) {
                return;
            }

            resyncOnlineResources();

            connect(putUplink(), &RequestInvoker::finished, this, [this](QNetworkReply::NetworkError error) {
                if (error != QNetworkReply::NoError) {
                    return;
                }
                websocket->start();
            });
        });

        emit loginSucceeded();

    } else {
        obs_log(LOG_DEBUG, "client: The API client has unlinked.");

        websocket->stop();
        clearOnlineResources();

        emit logoutSucceeded();
    }
}

void SRLinkApiClient::onO2LinkingFailed()
{
    obs_log(LOG_ERROR, "client: The API client linking failed.");

    websocket->stop();

    emit loginFailed();
}

void SRLinkApiClient::onO2RefreshFinished(QNetworkReply::NetworkError error)
{
    if (error != QNetworkReply::NoError) {
        return;
    }
    CHECK_CLIENT_TOKEN();

    // Schedule next refresh
    QTimer::singleShot(client->expires() * 1000 - 60000 - QDateTime().currentMSecsSinceEpoch(), client, SLOT(refresh()));
}

void SRLinkApiClient::onWebSocketReady(bool reconnect)
{
    obs_log(LOG_DEBUG, "client: WebSocket is ready.");
    websocket->subscribe("uplink", {{"uuid", uuid}});
    websocket->subscribe("stages");
    websocket->subscribe("participants");

    foreach (auto sourceUuid, downlinks.keys()) {
        websocket->subscribe("downlink", {{"uuid", sourceUuid}});
    }

    if (reconnect) {
        requestAccountInfo();
        resyncOnlineResources();
    }

    emit webSocketReady(reconnect);
}

void SRLinkApiClient::onWebSocketDisconnected()
{
    emit webSocketDisconnected();
}

void SRLinkApiClient::onWebSocketDataChanged(const QString &name, const QString &id, const QJsonObject &payload)
{
    obs_log(LOG_DEBUG, "client: WebSocket data changed: %s,%s", qPrintable(name), qPrintable(id));

    if (name == "uplink.allocations") {
        uplink.setAllocation(payload);
        emit uplinkReady(uplink);

    } else if (name == "uplink.stages") {
        uplink.setStage(payload);
        emit uplinkReady(uplink);

    } else if (name == "uplink.connections") {
        auto connections = uplink.getConnections();
        auto index =
            connections.findIndex([id](const StageConnection &connection) { return connection.getId() == id; });
        if (index >= 0) {
            connections.replace(index, payload);
        } else {
            connections.append(payload);
        }
        uplink["connections"] = connections;
        emit uplinkReady(uplink);

    } else if (name == "downlink.connections") {
        StageConnection newConnection = payload;
        downlinks[id]["connection"] = newConnection;
        emit downlinkReady(downlinks[id]);

    } else if (name == "stages") {
        Stage stage = payload;
        auto index = stages.findIndex([id](const Stage &stage) { return stage.getId() == id; });
        if (index >= 0) {
            stages.replace(index, stage);
        } else {
            stages.append(stage);
        }
        emit stagesReady(stages);

    } else if (name == "participants") {
        PartyEventParticipant participant = payload;
        auto index = participants.findIndex([id](const PartyEventParticipant &participant) {
            return participant.getId() == id;
        });
        if (index >= 0) {
            participants.replace(index, participant);
        } else {
            participants.append(participant);
        }
        emit participantsReady(participants);
    }
}

void SRLinkApiClient::onWebSocketDataRemoved(const QString &name, const QString &id, const QJsonObject &payload)
{
    obs_log(LOG_DEBUG, "client: WebSocket data removed: %s,%s", qPrintable(name), qPrintable(id));

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
            emit deleteDownlinkSucceeded(id);
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
    }
}
