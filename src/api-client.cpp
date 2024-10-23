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

#include <util/platform.h>

#include <QMessageBox>
#include <QRandomGenerator>
#include <QDesktopServices>
#include <QMetaEnum>
#include <QString>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtConcurrent>
#include <QPromise>
#include <QTimer>
#include <QByteArray>

#include <o0settingsstore.h>
#include <o0globals.h>

#include "plugin-support.h"
#include "api-client.hpp"
#include "utils.hpp"

#define LOCAL_DEBUG
#define SCOPE "read write"
#define POLLING_INTERVAL_MSECS 10000
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

//--- SourceLinkApiClient class ---//

SourceLinkApiClient::SourceLinkApiClient(QObject *parent)
    : QObject(parent),
      // Create store object for writing the received tokens (It will be child of O2 instance)
      settings(new SourceLinkSettingsStore()),
      pollingTimer(nullptr),
      networkManager(nullptr),
      client(nullptr),
      activeOutputs(0),
      standByOutputs(0)
{
    obs_log(LOG_DEBUG, "client: SourceLinkApiClient creating with %s", API_SERVER);

    pollingTimer = new QTimer(this);
    networkManager = new QNetworkAccessManager(this);
    client = new O2(this, networkManager, settings);
    sequencer = new RequestSequencer(networkManager, client, this);

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

    if (client->expires() - 60 <= QDateTime().currentSecsSinceEpoch()) {
        // Refresh token now
        auto invoker = new RequestInvoker(sequencer, this);
        invoker->refresh();
    } else {
        requestOnlineResources();

        // Schedule next refresh
        QTimer::singleShot(
            client->expires() * 1000 - 60000 - QDateTime().currentMSecsSinceEpoch(), client, SLOT(refresh())
        );
    }

    // Setup interval timer for polling
    connect(pollingTimer, SIGNAL(timeout()), this, SLOT(onPollingTimerTimeout()));
    pollingTimer->setInterval(POLLING_INTERVAL_MSECS);
    pollingTimer->start();

    obs_log(LOG_DEBUG, "client: SourceLinkApiClient created");
}

SourceLinkApiClient::~SourceLinkApiClient()
{
    obs_log(LOG_DEBUG, "client: SourceLinkApiClient destroyed");
}

void SourceLinkApiClient::login()
{
    obs_log(
        LOG_DEBUG, "client: Starting OAuth 2 with grant flow type %s", qPrintable(GRANTFLOW_STR(client->grantFlow()))
    );
    client->link();
}

void SourceLinkApiClient::logout()
{
    client->unlink();
}

bool SourceLinkApiClient::isLoggedIn()
{
    return client->linked();
}

const RequestInvoker *SourceLinkApiClient::refresh()
{
    auto invoker = new RequestInvoker(sequencer, this);
    invoker->refresh();
    return invoker;
}

const int SourceLinkApiClient::getFreePort()
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

void SourceLinkApiClient::releasePort(const int port)
{
    usedPorts[port] = false;
}

void SourceLinkApiClient::requestOnlineResources()
{
    CHECK_CLIENT_TOKEN();

    requestAccountInfo();
    requestParties();
    requestPartyEvents();
    requestStages();
    putUplink();
}

const RequestInvoker *SourceLinkApiClient::requestAccountInfo()
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

const RequestInvoker *SourceLinkApiClient::requestParties()
{
    CHECK_CLIENT_TOKEN(nullptr);

    obs_log(LOG_DEBUG, "client: Requesting parties.");
    auto invoker = new RequestInvoker(sequencer, this);
    connect(invoker, &RequestInvoker::finished, [this](QNetworkReply::NetworkError error, QByteArray replyData) {
        CHECK_RESPONSE_NOERROR(partiesFailed, "client: Requesting parties failed: %d", error);

        parties = QJsonDocument::fromJson(replyData).array();

        auto size = parties.size();
        if (settings->getPartyId().isEmpty() && size > 0) {
            settings->setPartyId(parties[0].getId());
        }

        obs_log(LOG_DEBUG, "client: Received %d parties", parties.size());
        emit partiesReady(parties);
    });
    invoker->get(QNetworkRequest(QUrl(PARTIES_URL)));

    return invoker;
}

const RequestInvoker *SourceLinkApiClient::requestPartyEvents()
{
    CHECK_CLIENT_TOKEN(nullptr);

    obs_log(LOG_DEBUG, "client: Requesting party events");
    auto invoker = new RequestInvoker(sequencer, this);
    connect(invoker, &RequestInvoker::finished, [this](QNetworkReply::NetworkError error, QByteArray replyData) {
        CHECK_RESPONSE_NOERROR(partyEventsFailed, "client: Requesting party events failed: %d", error);

        partyEvents = QJsonDocument::fromJson(replyData).array();

        if (settings->getPartyEventId().isEmpty() && partyEvents.size() > 0) {
            settings->setPartyEventId(partyEvents[0].getId());
        }

        obs_log(LOG_DEBUG, "client: Received %d party events", partyEvents.size());
        emit partyEventsReady(partyEvents);
    });
    invoker->get(QNetworkRequest(QUrl(PARTY_EVENTS_URL)));

    return invoker;
}

const RequestInvoker *SourceLinkApiClient::requestStages()
{
    CHECK_CLIENT_TOKEN(nullptr);

    obs_log(LOG_DEBUG, "client: Requesting stages.");
    auto invoker = new RequestInvoker(sequencer, this);
    connect(invoker, &RequestInvoker::finished, [this](QNetworkReply::NetworkError error, QByteArray replyData) {
        CHECK_RESPONSE_NOERROR(stagesFailed, "client: Requesting stages failed: %d", error);

        stages = QJsonDocument::fromJson(replyData).array();

        obs_log(LOG_DEBUG, "client: Received %d stages", stages.size());
        emit stagesReady(stages);
    });
    invoker->get(QNetworkRequest(QUrl(STAGES_URL)));

    return invoker;
}

const RequestInvoker *SourceLinkApiClient::requestUplink()
{
    CHECK_CLIENT_TOKEN(nullptr);

    obs_log(LOG_DEBUG, "client: Requesting uplink for %s", qPrintable(uuid));
    auto invoker = new RequestInvoker(sequencer, this);
    connect(invoker, &RequestInvoker::finished, [this](QNetworkReply::NetworkError error, QByteArray replyData) {
        CHECK_RESPONSE_NOERROR(uplinkFailed, "clinet: Requesting uplink for %s failed: %d", qPrintable(uuid), error);

        uplink = QJsonDocument::fromJson(replyData).object();

        obs_log(LOG_DEBUG, "client: Received uplink for %s", qPrintable(uuid));
        emit uplinkReady(uplink);
    });
    invoker->get(QNetworkRequest(QUrl(QString(UPLINK_URL).arg(uuid))));

    return invoker;
}

const RequestInvoker *SourceLinkApiClient::requestDownlink(const QString &sourceUuid)
{
    CHECK_CLIENT_TOKEN(nullptr);

    obs_log(LOG_DEBUG, "client: Requesting downlink for %s", qPrintable(sourceUuid));
    auto invoker = new RequestInvoker(sequencer, this);
    connect(
        invoker, &RequestInvoker::finished,
        [this, sourceUuid](QNetworkReply::NetworkError error, QByteArray replyData) {
            CHECK_RESPONSE_NOERROR(
                downlinkFailed, "clinet: Requesting downlink for %s failed: %d", qPrintable(sourceUuid), error
            );

            DownlinkInfo connection = QJsonDocument::fromJson(replyData).object();

            obs_log(LOG_DEBUG, "client: Received downlink for %s", qPrintable(sourceUuid));
            emit downlinkReady(connection);
        }
    );
    invoker->get(QNetworkRequest(QUrl(QString(DOWNLINK_URL).arg(sourceUuid))));

    return invoker;
}

const RequestInvoker *SourceLinkApiClient::putDownlink(
    const QString &sourceUuid, const QString &stageId, const QString &seatName, const QString &sourceName,
    const QString &protocol, const int port, const QString &parameters, const int maxBitrate, const int minBitrate,
    const int width, const int height, const int revision
)
{
    CHECK_CLIENT_TOKEN(nullptr);

    auto req = QNetworkRequest(QUrl(QString(DOWNLINK_URL).arg(sourceUuid)));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QJsonObject body;
    body["stage_id"] = stageId;
    body["seat_name"] = seatName;
    body["source_name"] = sourceName;
    body["protocol"] = protocol;
    body["port"] = port;
    body["parameters"] = parameters;
    body["max_bitrate"] = maxBitrate;
    body["min_bitrate"] = minBitrate;
    body["width"] = width;
    body["height"] = height;
    body["revision"] = revision;

    obs_log(LOG_DEBUG, "client: Putting downlink: %s", qPrintable(sourceUuid));
    auto invoker = new RequestInvoker(sequencer, this);
    connect(
        invoker, &RequestInvoker::finished,
        [this, sourceUuid](QNetworkReply::NetworkError error, QByteArray replyData) {
            CHECK_RESPONSE_NOERROR(
                putDownlinkFailed, "client: Putting downlink %s failed: %d", qPrintable(sourceUuid), error
            );

            DownlinkInfo connection = QJsonDocument::fromJson(replyData).object();

            obs_log(LOG_DEBUG, "client: Put downlink %s succeeded", qPrintable(connection.getConnection().getId()));
            emit putDownlinkSucceeded(connection);
        }
    );
    invoker->put(req, QJsonDocument(body).toJson());

    return invoker;
}

const RequestInvoker *SourceLinkApiClient::deleteDownlink(const QString &sourceUuid, const bool parallel)
{
    CHECK_CLIENT_TOKEN(nullptr);

    auto req = QNetworkRequest(QUrl(QString(DOWNLINK_URL).arg(sourceUuid)));

    obs_log(LOG_DEBUG, "client: Deleting downlink of %s", qPrintable(sourceUuid));
    auto invoker = !parallel ? new RequestInvoker(sequencer, this) : new RequestInvoker(networkManager, client, this);
    connect(invoker, &RequestInvoker::finished, [this, sourceUuid](QNetworkReply::NetworkError error, QByteArray) {
        CHECK_RESPONSE_NOERROR(
            deleteDownlinkFailed, "clinet: Deleting downlink of %s failed: %d", qPrintable(sourceUuid), error
        );

        obs_log(LOG_DEBUG, "client: Delete downlink of %s succeeded", qPrintable(sourceUuid));
        emit deleteDownlinkSucceeded(sourceUuid);
    });
    invoker->deleteResource(req);

    return invoker;
}

const RequestInvoker *SourceLinkApiClient::putUplink(const bool force)
{
    CHECK_CLIENT_TOKEN(nullptr);

    auto req = QNetworkRequest(QUrl(QString(UPLINK_URL).arg(uuid)));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QJsonObject body;
    body["party_event_id"] = settings->getPartyEventId();
    body["force"] = force || settings->getForceConnection() ? "1" : "0";
    body["uplink_status"] = activeOutputs > 0 ? "active" : standByOutputs > 0 ? "standby" : "inactive";

    obs_log(LOG_DEBUG, "client: Putting uplink of %s", qPrintable(uuid));
    auto invoker = new RequestInvoker(sequencer, this);
    connect(invoker, &RequestInvoker::finished, [this](QNetworkReply::NetworkError error, QByteArray replyData) {
        if (error != QNetworkReply::NoError) {
            if (error == QNetworkReply::ContentOperationNotPermittedError) {
                obs_log(LOG_ERROR, "client: Putting uplink of %s failed: %d", qPrintable(uuid), error);
                emit putUplinkFailed();
                emit uplinkFailed();
                return;
            }
        }

        uplink = QJsonDocument::fromJson(replyData).object();

        obs_log(LOG_DEBUG, "client: Put uplink of %s succeeded", qPrintable(uuid));
        emit putUplinkSucceeded(uplink);
        emit uplinkReady(uplink);
    });
    invoker->put(req, QJsonDocument(body).toJson());

    return invoker;
}

const RequestInvoker *SourceLinkApiClient::putUplinkStatus()
{
    CHECK_CLIENT_TOKEN(nullptr);

    auto req = QNetworkRequest(QUrl(QString(UPLINK_STATUS_URL).arg(uuid)));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QJsonObject body;
    body["uplink_status"] = activeOutputs > 0 ? "active" : standByOutputs > 0 ? "standby" : "inactive";

    obs_log(LOG_DEBUG, "client: Putting uplink status of %s", qPrintable(uuid));
    auto invoker = new RequestInvoker(sequencer, this);
    connect(invoker, &RequestInvoker::finished, [this](QNetworkReply::NetworkError error, QByteArray replyData) {
        if (error != QNetworkReply::NoError) {
            if (error == QNetworkReply::ContentOperationNotPermittedError) {
                obs_log(LOG_ERROR, "client: Putting uplink status of %s failed: %d", qPrintable(uuid), error);
                emit putUplinkStatusFailed();
                emit uplinkFailed();
                return;
            }
        }

        uplink = QJsonDocument::fromJson(replyData).object();

        obs_log(LOG_DEBUG, "client: Put uplink status of %s succeeded", qPrintable(uuid));
        emit putUplinkStatusSucceeded(uplink);
        emit uplinkReady(uplink);
    });
    invoker->put(req, QJsonDocument(body).toJson());

    return invoker;
}

const RequestInvoker *SourceLinkApiClient::deleteUplink(const bool parallel)
{
    CHECK_CLIENT_TOKEN(nullptr);

    auto req = QNetworkRequest(QUrl(QString(UPLINK_URL).arg(uuid)));

    obs_log(LOG_DEBUG, "client: Deleting uplink of %s", qPrintable(uuid));
    auto invoker = !parallel ? new RequestInvoker(sequencer, this) : new RequestInvoker(networkManager, client, this);
    connect(invoker, &RequestInvoker::finished, [this](QNetworkReply::NetworkError error, QByteArray) {
        CHECK_RESPONSE_NOERROR(deleteUplinkFailed, "clinet: Deleting uplink of %s failed: %d", qPrintable(uuid), error);

        obs_log(LOG_DEBUG, "client: Delete uplink %s succeeded", qPrintable(uuid));
        emit deleteUplinkSucceeded(uuid);
    });
    invoker->deleteResource(req);

    return invoker;
}

const RequestInvoker *SourceLinkApiClient::putScreenshot(const QString &sourceName, const QImage &image)
{
    CHECK_CLIENT_TOKEN(nullptr);

    auto req = QNetworkRequest(QUrl(QString(SCREENSHOTS_URL).arg(uuid).arg(sourceName)));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "image/jpeg");

    QByteArray imageBytes(image.sizeInBytes(), Qt::Uninitialized);
    QBuffer imageBuffer(&imageBytes);
    imageBuffer.open(QIODevice::WriteOnly);
    image.save(&imageBuffer, "JPG", SCREENSHOT_QUALITY);

    obs_log(LOG_DEBUG, "client: Putting screenshot of %s", qPrintable(sourceName));
    auto invoker = new RequestInvoker(sequencer, this);
    connect(invoker, &RequestInvoker::finished, [this, sourceName](QNetworkReply::NetworkError error, QByteArray) {
        CHECK_RESPONSE_NOERROR(
            putScreenshotFailed, "client: Putting screenshot of %s failed: %d", qPrintable(sourceName), error
        );

        obs_log(LOG_DEBUG, "client: Put screenshot of %s succeeded", qPrintable(sourceName));
        emit putScreenshotSucceeded(sourceName);
    });
    invoker->put(req, imageBytes);

    return invoker;
}

void SourceLinkApiClient::getPicture(const QString &pictureId)
{
    auto reply = networkManager->get(QNetworkRequest(QUrl(QString(PICTURES_URL).arg(pictureId))));
    connect(reply, &QNetworkReply::finished, [this, pictureId, reply]() {
        if (reply->error() != QNetworkReply::NoError) {
            obs_log(LOG_ERROR, "client: Getting picture of %s failed: %d", qPrintable(pictureId), reply->error());
            emit getPictureFailed(pictureId);
            return;
        }

        auto picture = QImage::fromData(reply->readAll());

        obs_log(LOG_DEBUG, "client: Get picture of %s succeeded", qPrintable(pictureId));
        emit getPictureSucceeded(pictureId, picture);
        reply->deleteLater();
    });
}

void SourceLinkApiClient::openStagesManagementPage()
{
    QDesktopServices::openUrl(QUrl(STAGES_MANAGEMENT_PAGE_URL));
}

void SourceLinkApiClient::onO2OpenBrowser(const QUrl &url)
{
    QDesktopServices::openUrl(url);
}

void SourceLinkApiClient::onO2LinkedChanged()
{
    CHECK_CLIENT_TOKEN();

    obs_log(LOG_DEBUG, "client: The API client link has been changed.");
}

void SourceLinkApiClient::onO2LinkingSucceeded()
{
    if (client->linked()) {
        CHECK_CLIENT_TOKEN();
        obs_log(LOG_DEBUG, "client: The API client has linked up.");

        requestOnlineResources();

        emit loginSucceeded();
    } else {
        obs_log(LOG_DEBUG, "client: The API client has unlinked.");

        emit logoutSucceeded();
    }
}

void SourceLinkApiClient::onO2LinkingFailed()
{
    obs_log(LOG_ERROR, "client: The API client linking failed.");

    emit loginFailed();
}

void SourceLinkApiClient::onO2RefreshFinished(QNetworkReply::NetworkError error)
{
    if (error != QNetworkReply::NoError) {
        return;
    }
    CHECK_CLIENT_TOKEN();

    // Schedule next refresh
    QTimer::singleShot(client->expires() * 1000 - 60000 - QDateTime().currentMSecsSinceEpoch(), client, SLOT(refresh()));
}

void SourceLinkApiClient::onPollingTimerTimeout()
{
    if (!isLoggedIn()) {
        return;
    }

    // Polling uplink
    putUplinkStatus(); //requestUplink();
}

void SourceLinkApiClient::terminate()
{
    obs_log(LOG_DEBUG, "client: Terminating API client.");
    pollingTimer->stop();
    deleteUplink(true);
}
