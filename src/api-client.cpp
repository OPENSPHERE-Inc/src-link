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

#include <o2requestor.h>
#include <o0settingsstore.h>
#include <o0globals.h>

#include "plugin-support.h"
#include "api-client.hpp"
#include "request-invoker.hpp"
#include "utils.hpp"

//#define LOCAL_DEBUG
#define SCOPE "read write"
#define SETTINGS_JSON_NAME "settings.json"
#define POLLING_INTERVAL_MSECS 20000
#define SCREENSHOT_QUALITY 75

// REST Endpoints
#ifdef LOCAL_DEBUG
#define API_SERVER "http://localhost:3000"
#else
#define API_SERVER "https://source-link-test.opensphere.co.jp"
#endif
#define AUTHORIZE_URL (API_SERVER "/oauth2/authorize")
#define TOKEN_URL (API_SERVER "/oauth2/token")
#define ACCOUNT_INFO_URL (API_SERVER "/api/v1/accounts/me")
#define PARTIES_URL (API_SERVER "/api/v1/parties/my")
#define PARTY_EVENTS_URL (API_SERVER "/api/v1/events/my")
#define STAGES_URL (API_SERVER "/api/v1/stages")
#define STAGES_CONNECTIONS_URL (API_SERVER "/api/v1/stages/connections/%1")
#define STAGE_SEAT_ALLOCATIONS_URL (API_SERVER "/api/v1/stages/seat-allocations/%1")
#define PARTY_MEMBERS_SCREENSHOTS_URL (API_SERVER "/api/v1/parties/%1/members/me/screenshots/%2")
#define PING_URL (API_SERVER "/api/v1/ping")
#define PICTURES_URL (API_SERVER "/pictures/%1")

// Embedded client ID and secret
#define CLIENT_ID "testClientId"
#define CLIENT_SECRET "testClientSecret"

//--- Macros ---//
#define QENUM_NAME(o, e, v) (o::staticMetaObject.enumerator(o::staticMetaObject.indexOfEnumerator(#e)).valueToKey((v)))
#define GRANTFLOW_STR(v) QString(QENUM_NAME(O2, GrantFlow, v))

#define CHECK_CLIENT_TOKEN(...)                                \
    {                                                          \
        if (client->refreshToken().isEmpty()) {                \
            obs_log(LOG_ERROR, "client: No access token."); \
            return __VA_ARGS__;                                \
        }                                                      \
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
      // Create a store object for writing the received tokens (It will be child of O2 instance)
      settings(new SourceLinkApiClientSettingsStore()),
      pollingTimer(nullptr),
      networkManager(nullptr),
      client(nullptr),
      requestor(nullptr)
{
    pollingTimer = new QTimer(this);
    networkManager = new QNetworkAccessManager(this);
    client = new O2(this, networkManager, settings);
    requestor = new O2Requestor(networkManager, client, this);
    requestor->setAddAccessTokenInQuery(false);
    requestor->setAccessTokenInAuthenticationHTTPHeaderFormat("Bearer %1");

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
        auto invoker = new RequestInvoker(this);
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
    if (requestQueue.size() > 0) {
        obs_log(LOG_WARNING, "client: Remaining %d requests in queue.", requestQueue.size());
    }

    obs_log(LOG_DEBUG, "client: SourceLinkApiClient destroyed");
}

void SourceLinkApiClient::login()
{
    obs_log(LOG_DEBUG, "client: Starting OAuth 2 with grant flow type %s", qPrintable(GRANTFLOW_STR(client->grantFlow())));
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

void SourceLinkApiClient::refresh()
{
    client->refresh();
}

bool SourceLinkApiClient::ping()
{
    CHECK_CLIENT_TOKEN(false);

    obs_log(LOG_DEBUG, "client: Pinging API server.");
    auto invoker = new RequestInvoker(this);
    connect(invoker, &RequestInvoker::finished, this, [this](QNetworkReply::NetworkError error, QByteArray replyData) {
        CHECK_RESPONSE_NOERROR(pingFailed, "client: Pinging API server failed: %d", error);

        accountInfo = QJsonDocument::fromJson(replyData).object();

        obs_log(LOG_INFO, "client: Pong from API server: %s", qPrintable(accountInfo.getDisplayName()));
        emit pingSucceeded();
    });
    invoker->get(QNetworkRequest(QUrl(QString(PING_URL))));

    return true;
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

bool SourceLinkApiClient::requestOnlineResources()
{
    CHECK_CLIENT_TOKEN(false);

    requestAccountInfo();
    requestParties();
    requestPartyEvents();
    requestStages();

    return true;
}

bool SourceLinkApiClient::requestAccountInfo()
{
    CHECK_CLIENT_TOKEN(false);

    obs_log(LOG_DEBUG, "client: Requesting account info.");
    auto invoker = new RequestInvoker(this);
    connect(invoker, &RequestInvoker::finished, this, [this](QNetworkReply::NetworkError error, QByteArray replyData) {
        CHECK_RESPONSE_NOERROR(accountInfoFailed, "client: Requesting account info failed: %d", error);

        accountInfo = QJsonDocument::fromJson(replyData).object();

        obs_log(LOG_INFO, "client: Received account: %s", qPrintable(accountInfo.getDisplayName()));
        emit accountInfoReady(accountInfo);
    });
    invoker->get(QNetworkRequest(QUrl(QString(ACCOUNT_INFO_URL))));

    return true;
}

bool SourceLinkApiClient::requestParties()
{
    CHECK_CLIENT_TOKEN(false);

    obs_log(LOG_DEBUG, "client: Requesting parties.");
    auto invoker = new RequestInvoker(this);
    connect(invoker, &RequestInvoker::finished, [this](QNetworkReply::NetworkError error, QByteArray replyData) {
        CHECK_RESPONSE_NOERROR(partiesFailed, "client: Requesting parties failed: %d", error);

        parties.clear();
        foreach (const QJsonValue &partyItem, QJsonDocument::fromJson(replyData).array()) {
            parties.append(partyItem.toObject());
        }

        if (getPartyId().isEmpty() && parties.size() > 0) {
            setPartyId(parties[0].getId());
        }

        obs_log(LOG_INFO, "client: Received %d parties", parties.size());
        emit partiesReady(parties);
    });
    invoker->get(QNetworkRequest(QUrl(QString(PARTIES_URL))));

    return true;
}

bool SourceLinkApiClient::requestPartyEvents()
{
    CHECK_CLIENT_TOKEN(false);

    obs_log(LOG_DEBUG, "client: Requesting party events");
    auto invoker = new RequestInvoker(this);
    connect(invoker, &RequestInvoker::finished, [this](QNetworkReply::NetworkError error, QByteArray replyData) {
        CHECK_RESPONSE_NOERROR(partyEventsFailed, "client: Requesting party events failed: %d", error);

        partyEvents.clear();
        foreach (const QJsonValue &partyEventItem, QJsonDocument::fromJson(replyData).array()) {
            partyEvents.append(partyEventItem.toObject());
        }

        if (getPartyEventId().isEmpty() && partyEvents.size() > 0) {
            setPartyEventId(partyEvents[0].getId());
        }

        obs_log(LOG_INFO, "client: Received %d party events", partyEvents.size());
        emit partyEventsReady(partyEvents);
    });
    invoker->get(QNetworkRequest(QUrl(QString(PARTY_EVENTS_URL))));

    return true;
}

bool SourceLinkApiClient::requestStages()
{
    CHECK_CLIENT_TOKEN(false);

    obs_log(LOG_DEBUG, "client: Requesting stages.");
    auto invoker = new RequestInvoker(this);
    connect(invoker, &RequestInvoker::finished, [this](QNetworkReply::NetworkError error, QByteArray replyData) {
        CHECK_RESPONSE_NOERROR(stagesFailed, "client: Requesting stages failed: %d", error);

        stages.clear();
        foreach (const QJsonValue &stageItem, QJsonDocument::fromJson(replyData).array()) {
            stages.append(stageItem.toObject());
        }

        obs_log(LOG_INFO, "client: Received %d stages", stages.size());
        emit stagesReady(stages);
    });
    invoker->get(QNetworkRequest(QUrl(QString(STAGES_URL))));

    return true;
}

bool SourceLinkApiClient::requestSeatAllocation()
{
    CHECK_CLIENT_TOKEN(false);

    obs_log(LOG_DEBUG, "client: Requesting seat allocation for %s", qPrintable(uuid));
    auto invoker = new RequestInvoker(this);
    connect(invoker, &RequestInvoker::finished, [this](QNetworkReply::NetworkError error, QByteArray replyData) {
        CHECK_RESPONSE_NOERROR(
            seatAllocationFailed, "clinet: Requesting seat allocation for %s failed: %d", qPrintable(uuid), error
        );

        seat = QJsonDocument::fromJson(replyData).object();

        obs_log(LOG_INFO, "client: Received seat allocation for %s", qPrintable(uuid));
        emit seatAllocationReady(seat);
    });
    invoker->get(QNetworkRequest(QUrl(QString(STAGE_SEAT_ALLOCATIONS_URL).arg(uuid))));

    return true;
}

bool SourceLinkApiClient::putConnection(
    const QString &sourceUuid, const QString &stageId, const QString &seatName, const QString &sourceName,
    const QString &protocol, const int port, const QString &parameters, const int maxBitrate, const int minBitrate,
    const int width, const int height
)
{
    CHECK_CLIENT_TOKEN(false);

    auto req = QNetworkRequest(QUrl(QString(STAGES_CONNECTIONS_URL).arg(sourceUuid)));
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

    obs_log(LOG_DEBUG, "client: Putting stage connection: %s", qPrintable(sourceUuid));
    auto invoker = new RequestInvoker(this);
    connect(
        invoker, &RequestInvoker::finished,
        [this, sourceUuid](QNetworkReply::NetworkError error, QByteArray replyData) {
            CHECK_RESPONSE_NOERROR(
                connectionPutFailed, "client: Putting stage connection %s failed: %d", qPrintable(sourceUuid), error
            );

            StageConnection connection = QJsonDocument::fromJson(replyData).object();

            obs_log(LOG_INFO, "client: Put stage connection %s succeeded", qPrintable(connection.getId()));
            emit connectionPutSucceeded(connection);
        }
    );
    invoker->put(req, QJsonDocument(body).toJson());

    return true;
}

bool SourceLinkApiClient::deleteConnection(const QString &sourceUuid, const bool noSignal)
{
    CHECK_CLIENT_TOKEN(false);

    auto req = QNetworkRequest(QUrl(QString(STAGES_CONNECTIONS_URL).arg(sourceUuid)));

    obs_log(LOG_DEBUG, "client: Deleting stage connection of %s", qPrintable(sourceUuid));
    auto invoker = new RequestInvoker(this);
    if (!noSignal) {
        connect(invoker, &RequestInvoker::finished, [this, sourceUuid](QNetworkReply::NetworkError error, QByteArray) {
            CHECK_RESPONSE_NOERROR(
                connectionDeleteFailed, "clinet: Deleting stage connection of %s failed: %d", qPrintable(sourceUuid), error
            );

            obs_log(LOG_INFO, "client: Delete stage connection of %s succeeded", qPrintable(sourceUuid));
            emit connectionDeleteSucceeded(sourceUuid);
        });
    }
    invoker->deleteResource(req);

    return true;
}

bool SourceLinkApiClient::putSeatAllocation(const bool force)
{
    CHECK_CLIENT_TOKEN(false);

    auto req = QNetworkRequest(QUrl(QString(STAGE_SEAT_ALLOCATIONS_URL).arg(uuid)));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QJsonObject body;
    body["party_event_id"] = getPartyEventId();
    body["force"] = force || getForceConnection() ? "1" : "0";

    obs_log(LOG_DEBUG, "client: Putting stage seat allocation of %s", qPrintable(uuid));
    auto invoker = new RequestInvoker(this);
    connect(invoker, &RequestInvoker::finished, [this](QNetworkReply::NetworkError error, QByteArray replyData) {
        if (error != QNetworkReply::NoError) {
            if (error == QNetworkReply::ContentOperationNotPermittedError) {
                obs_log(LOG_ERROR, "client: Putting stage seat allocation of %s failed: %d", qPrintable(uuid), error);
                emit seatAllocationPutFailed();
                emit seatAllocationFailed();
                return;
            }
        }

        seat = QJsonDocument::fromJson(replyData).object();

        obs_log(LOG_INFO, "client: Put stage seat allocation of %s succeeded", qPrintable(uuid));
        emit seatAllocationPutSucceeded(seat);
        emit seatAllocationReady(seat);
    });
    invoker->put(req, QJsonDocument(body).toJson());

    return true;
}

bool SourceLinkApiClient::deleteSeatAllocation(const bool noSignal)
{
    CHECK_CLIENT_TOKEN(false);

    auto req = QNetworkRequest(QUrl(QString(STAGE_SEAT_ALLOCATIONS_URL).arg(uuid)));

    obs_log(LOG_DEBUG, "client: Deleting stage seat allocation of %s", qPrintable(uuid));
    auto invoker = new RequestInvoker(this);
    if (!noSignal) {
        connect(invoker, &RequestInvoker::finished, [this](QNetworkReply::NetworkError error, QByteArray) {
            CHECK_RESPONSE_NOERROR(
                seatAllocationDeleteFailed, "clinet: Deleting stage seat allocation of %s failed: %d", qPrintable(uuid), error
            );

            obs_log(LOG_INFO, "client: Delete stage seat allocation %s succeeded", qPrintable(uuid));
            emit seatAllocationDeleteSucceeded(uuid);
        });
    }
    invoker->deleteResource(req);

    return true;
}

bool SourceLinkApiClient::putScreenshot(const QString &sourceName, const QImage &image)
{
    CHECK_CLIENT_TOKEN(false);

    auto partyId = getPartyId();
    if (partyId.isEmpty()) {
        obs_log(LOG_ERROR, "client: No party ID is set");
        return false;
    }

    auto req = QNetworkRequest(QUrl(QString(PARTY_MEMBERS_SCREENSHOTS_URL).arg(partyId).arg(sourceName)));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "image/jpeg");

    QByteArray imageBytes(image.sizeInBytes(), Qt::Uninitialized);
    QBuffer imageBuffer(&imageBytes);
    imageBuffer.open(QIODevice::WriteOnly);
    image.save(&imageBuffer, "JPG", SCREENSHOT_QUALITY);

    obs_log(LOG_DEBUG, "client: Putting screenshot of %s", qPrintable(sourceName));
    auto invoker = new RequestInvoker(this);
    connect(invoker, &RequestInvoker::finished, [this, sourceName](QNetworkReply::NetworkError error, QByteArray) {
        CHECK_RESPONSE_NOERROR(
            screenshotPutFailed, "client: Putting screenshot of %s failed: %d", qPrintable(sourceName), error
        );

        obs_log(LOG_INFO, "client: Put screenshot of %s succeeded", qPrintable(sourceName));
        emit screenshotPutSucceeded(sourceName);
    });
    invoker->put(req, imageBytes);

    return true;
}

bool SourceLinkApiClient::getPicture(const QString &pictureId)
{
    auto reply = networkManager->get(QNetworkRequest(QUrl(QString(PICTURES_URL).arg(pictureId))));
    connect(reply, &QNetworkReply::finished, [this, pictureId, reply]() {
        if (reply->error() != QNetworkReply::NoError) {
            obs_log(LOG_ERROR, "client: Getting picture of %s failed: %d", qPrintable(pictureId), reply->error());
            emit pictureGetFailed(pictureId);
            return;
        }

        auto picture = QImage::fromData(reply->readAll());

        obs_log(LOG_INFO, "client: Get picture of %s succeeded", qPrintable(pictureId));
        emit pictureGetSucceeded(pictureId, picture);
        reply->deleteLater();
    });

    return true;
}

const int SourceLinkApiClient::getFreePort()
{
    auto min = settings->value("portRange.min", "10000").toInt();
    auto max = settings->value("portRange.max", "10099").toInt();

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

void SourceLinkApiClient::onPollingTimerTimeout()
{
    // Polling seat allocation
    requestSeatAllocation();
}

//--- SourceLinkSettingsStore class ---//

SourceLinkApiClientSettingsStore::SourceLinkApiClientSettingsStore(QObject *parent) : O0AbstractStore(parent)
{
    OBSString config_dir_path = obs_module_get_config_path(obs_current_module(), "");
    os_mkdirs(config_dir_path);

    OBSString path = obs_module_get_config_path(obs_current_module(), SETTINGS_JSON_NAME);
    settingsData = obs_data_create_from_json_file(path);
    if (!settingsData) {
        settingsData = obs_data_create();
    }

    obs_log(LOG_DEBUG, "client: SourceLinkApiClientSettingsStore created");
}

SourceLinkApiClientSettingsStore::~SourceLinkApiClientSettingsStore()
{
    obs_log(LOG_DEBUG, "client: SourceLinkApiClientSettingsStore destroyed");
}

QString SourceLinkApiClientSettingsStore::value(const QString &key, const QString &defaultValue)
{
    auto value = QString(obs_data_get_string(settingsData, qPrintable(key)));
    if (!value.isEmpty()) {
        return value;
    } else {
        return defaultValue;
    }
}

void SourceLinkApiClientSettingsStore::setValue(const QString &key, const QString &value)
{
    obs_data_set_string(settingsData, qPrintable(key), qPrintable(value));
    OBSString path = obs_module_get_config_path(obs_current_module(), SETTINGS_JSON_NAME);
    obs_data_save_json_safe(settingsData, path, "tmp", "bak");
}
