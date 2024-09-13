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
#include "UI/common.hpp"

#define CHECK_CLIENT_TOKEN(...)                                \
    {                                                          \
        if (client->refreshToken().isEmpty()) {                \
            obs_log(LOG_ERROR, "Client has no access token."); \
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
      requestor(nullptr),
      accountInfo(nullptr),
      seat(nullptr),
      seatAllocationRefs(0)
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
    connect(client, SIGNAL(linkingFailed()), this, SIGNAL(linkingFailed()));
    connect(client, SIGNAL(linkingSucceeded()), this, SLOT(onO2LinkingSucceeded()));
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

    obs_log(LOG_DEBUG, "SourceLinkApiClient created");
}

SourceLinkApiClient::~SourceLinkApiClient()
{
    if (requestQueue.size() > 0) {
        obs_log(LOG_WARNING, "Remaining %d requests in queue.", requestQueue.size());
    }

    obs_log(LOG_DEBUG, "SourceLinkApiClient destroyed");
}

void SourceLinkApiClient::login()
{
    obs_log(LOG_DEBUG, "Starting OAuth 2 with grant flow type %s", qPrintable(GRANTFLOW_STR(client->grantFlow())));
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

    obs_log(LOG_DEBUG, "Pinging API server.");
    auto invoker = new RequestInvoker(this);
    connect(invoker, &RequestInvoker::finished, this, [this](QNetworkReply::NetworkError error, QByteArray replyData) {
        CHECK_RESPONSE_NOERROR(pingFailed, "Pinging API server failed: %d", error);

        auto jsonDoc = QJsonDocument::fromJson(replyData);
        if (accountInfo) {
            accountInfo->deleteLater();
        }
        auto jsonObj = jsonDoc.object();
        accountInfo = AccountInfo::fromJsonObject(jsonObj["account"].toObject(), this);

        obs_log(LOG_INFO, "Pong from API server: %s", qPrintable(accountInfo->getDisplayName()));
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

    obs_log(LOG_DEBUG, "The API client link has been changed.");
}

void SourceLinkApiClient::onO2LinkingSucceeded()
{
    CHECK_CLIENT_TOKEN();

    obs_log(LOG_DEBUG, "The API client has linked up.");

    requestOnlineResources();

    emit linkingSucceeded();
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

    obs_log(LOG_DEBUG, "Requesting account info.");
    auto invoker = new RequestInvoker(this);
    connect(invoker, &RequestInvoker::finished, this, [this](QNetworkReply::NetworkError error, QByteArray replyData) {
        CHECK_RESPONSE_NOERROR(accountInfoFailed, "Requesting account info failed: %d", error);

        auto jsonDoc = QJsonDocument::fromJson(replyData);
        if (accountInfo) {
            accountInfo->deleteLater();
        }
        accountInfo = AccountInfo::fromJsonObject(jsonDoc.object(), this);

        obs_log(LOG_INFO, "Received account: %s", qPrintable(accountInfo->getDisplayName()));
        emit accountInfoReady(accountInfo);
    });
    invoker->get(QNetworkRequest(QUrl(QString(ACCOUNT_INFO_URL))));

    return true;
}

bool SourceLinkApiClient::requestParties()
{
    CHECK_CLIENT_TOKEN(false);

    obs_log(LOG_DEBUG, "Requesting parties.");
    auto invoker = new RequestInvoker(this);
    connect(invoker, &RequestInvoker::finished, [this](QNetworkReply::NetworkError error, QByteArray replyData) {
        CHECK_RESPONSE_NOERROR(partiesFailed, "Requesting parties failed: %d", error);

        auto jsonDoc = QJsonDocument::fromJson(replyData);
        qDeleteAll(parties);
        parties.clear();
        foreach(const QJsonValue &partyItem, jsonDoc.array())
        {
            auto party = Party::fromJsonObject(partyItem.toObject(), this);
            parties.append(party);
        }

        if (getPartyId().isEmpty() && parties.size() > 0) {
            setPartyId(parties[0]->getId());
        }

        obs_log(LOG_INFO, "Received %d parties", parties.size());
        emit partiesReady(parties);
    });
    invoker->get(QNetworkRequest(QUrl(QString(PARTIES_URL))));

    return true;
}

bool SourceLinkApiClient::requestPartyEvents()
{
    CHECK_CLIENT_TOKEN(false);

    obs_log(LOG_DEBUG, "Requesting party events");
    auto invoker = new RequestInvoker(this);
    connect(invoker, &RequestInvoker::finished, [this](QNetworkReply::NetworkError error, QByteArray replyData) {
        CHECK_RESPONSE_NOERROR(partyEventsFailed, "Requesting party events failed: %d", error);

        auto jsonDoc = QJsonDocument::fromJson(replyData);
        qDeleteAll(partyEvents);
        partyEvents.clear();
        foreach(const QJsonValue &partyEventItem, jsonDoc.array())
        {
            auto partyEvent = PartyEvent::fromJsonObject(partyEventItem.toObject(), this);
            partyEvents.append(partyEvent);
        }

        if (getPartyEventId().isEmpty() && partyEvents.size() > 0) {
            setPartyEventId(partyEvents[0]->getId());
        }

        obs_log(LOG_INFO, "Received %d party events", partyEvents.size());
        emit partyEventsReady(partyEvents);
    });
    invoker->get(QNetworkRequest(QUrl(QString(PARTY_EVENTS_URL))));

    return true;
}

bool SourceLinkApiClient::requestStages()
{
    CHECK_CLIENT_TOKEN(false);

    obs_log(LOG_DEBUG, "Requesting stages.");
    auto invoker = new RequestInvoker(this);
    connect(invoker, &RequestInvoker::finished, [this](QNetworkReply::NetworkError error, QByteArray replyData) {
        CHECK_RESPONSE_NOERROR(stagesFailed, "Requesting stages failed: %d", error);

        auto jsonDoc = QJsonDocument::fromJson(replyData);
        qDeleteAll(stages);
        stages.clear();
        foreach(const QJsonValue &stageItem, jsonDoc.array())
        {
            auto stage = Stage::fromJsonObject(stageItem.toObject(), this);
            stages.append(stage);
        }

        obs_log(LOG_INFO, "Received %d stages", stages.size());
        emit stagesReady(stages);
    });
    invoker->get(QNetworkRequest(QUrl(QString(STAGES_URL))));

    return true;
}

bool SourceLinkApiClient::requestSeatAllocation()
{
    CHECK_CLIENT_TOKEN(false);

    obs_log(LOG_DEBUG, "Requesting seat allocation for %s", qPrintable(uuid));
    auto invoker = new RequestInvoker(this);
    connect(invoker, &RequestInvoker::finished, [this](QNetworkReply::NetworkError error, QByteArray replyData) {
        CHECK_RESPONSE_NOERROR(
            seatAllocationFailed, "Requesting seat allocation for %s failed: %d", qPrintable(uuid), error
        );

        auto jsonDoc = QJsonDocument::fromJson(replyData);
        if (seat) {
            seat->deleteLater();
        }
        seat = StageSeatInfo::fromJsonObject(jsonDoc.object(), this);

        obs_log(LOG_INFO, "Received seat allocation for %s", qPrintable(uuid));
        emit seatAllocationReady(seat);
    });
    invoker->get(QNetworkRequest(QUrl(QString(STAGE_SEAT_ALLOCATIONS_URL) + "/" + uuid)));

    return true;
}

bool SourceLinkApiClient::putConnection(
    const QString &sourceUuid, const QString &stageId, const QString &seatName, const QString &sourceName,
    const QString &protocol, const int port, const QString &parameters, const int maxBitrate, const int minBitrate,
    const int width, const int height
)
{
    CHECK_CLIENT_TOKEN(false);

    auto req = QNetworkRequest(QUrl(QString(STAGES_CONNECTIONS_URL) + "/" + sourceUuid));
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

    obs_log(LOG_DEBUG, "Putting stage connection: %s", qPrintable(sourceUuid));
    auto invoker = new RequestInvoker(this);
    connect(
        invoker, &RequestInvoker::finished,
        [this, sourceUuid](QNetworkReply::NetworkError error, QByteArray replyData) {
            CHECK_RESPONSE_NOERROR(
                connectionPutFailed, "Putting stage connection %s failed: %d", qPrintable(sourceUuid), error
            );

            auto jsonDoc = QJsonDocument::fromJson(replyData);
            auto connection = StageConnection::fromJsonObject(jsonDoc.object(), this);

            obs_log(LOG_INFO, "Put stage connection %s succeeded", qPrintable(connection->getId()));
            emit connectionPutSucceeded(connection);
        }
    );
    invoker->put(req, QJsonDocument(body).toJson());

    return true;
}

bool SourceLinkApiClient::deleteConnection(const QString &sourceUuid)
{
    CHECK_CLIENT_TOKEN(false);

    auto req = QNetworkRequest(QUrl(QString(STAGES_CONNECTIONS_URL) + "/" + sourceUuid));

    obs_log(LOG_DEBUG, "Deleting stage connection of %s", qPrintable(sourceUuid));
    auto invoker = new RequestInvoker(this);
    connect(invoker, &RequestInvoker::finished, [this, sourceUuid](QNetworkReply::NetworkError error, QByteArray) {
        CHECK_RESPONSE_NOERROR(
            connectionDeleteFailed, "Deleting stage connection of %s failed: %d", qPrintable(sourceUuid), error
        );

        obs_log(LOG_INFO, "Delete stage connection of %s succeeded", qPrintable(sourceUuid));
        emit connectionDeleteSucceeded(sourceUuid);
    });
    invoker->deleteResource(req);

    return true;
}

bool SourceLinkApiClient::putSeatAllocation(const bool force)
{
    CHECK_CLIENT_TOKEN(false);

    auto req = QNetworkRequest(QUrl(QString(STAGE_SEAT_ALLOCATIONS_URL) + "/" + uuid));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QJsonObject body;
    body["party_event_id"] = getPartyEventId();
    body["force"] = force || getForceConnection() ? "1" : "0";

    obs_log(LOG_DEBUG, "Putting stage seat allocation of %s", qPrintable(uuid));
    auto invoker = new RequestInvoker(this);
    connect(invoker, &RequestInvoker::finished, [this](QNetworkReply::NetworkError error, QByteArray replyData) {
        CHECK_RESPONSE_NOERROR(
            seatAllocationPutFailed, "Putting stage seat allocation of %s failed: %d", qPrintable(uuid), error
        );

        auto jsonDoc = QJsonDocument::fromJson(replyData);
        seat = StageSeatInfo::fromJsonObject(jsonDoc.object(), this);

        obs_log(LOG_INFO, "Put stage seat allocation of %s succeeded", qPrintable(uuid));
        emit seatAllocationPutSucceeded(seat);
        emit seatAllocationReady(seat);
    });
    invoker->put(req, QJsonDocument(body).toJson());

    return true;
}

bool SourceLinkApiClient::deleteSeatAllocation()
{
    CHECK_CLIENT_TOKEN(false);

    auto req = QNetworkRequest(QUrl(QString(STAGE_SEAT_ALLOCATIONS_URL) + "/" + uuid));

    obs_log(LOG_DEBUG, "Deleting stage seat allocation of %s", qPrintable(uuid));
    auto invoker = new RequestInvoker(this);
    connect(invoker, &RequestInvoker::finished, [this](QNetworkReply::NetworkError error, QByteArray) {
        CHECK_RESPONSE_NOERROR(
            seatAllocationDeleteFailed, "Deleting stage seat allocation of %s failed: %d", qPrintable(uuid), error
        );

        obs_log(LOG_INFO, "Delete stage seat allocation %s succeeded", qPrintable(uuid));
        emit seatAllocationDeleteSucceeded(uuid);
    });
    invoker->deleteResource(req);

    return true;
}

bool SourceLinkApiClient::putScreenshot(const QString &sourceName, const QImage &image)
{
    CHECK_CLIENT_TOKEN(false);

    auto req = QNetworkRequest(QUrl(QString(ACCOUNT_SCREENSHOTS_URL) + "/" + uuid + "/" + sourceName));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "image/jpeg");

    QByteArray imageBytes(image.sizeInBytes(), Qt::Uninitialized);
    QBuffer imageBuffer(&imageBytes);
    imageBuffer.open(QIODevice::WriteOnly);
    image.save(&imageBuffer, "JPG");

    obs_log(LOG_DEBUG, "Putting screenshot of %s", qPrintable(sourceName));
    auto invoker = new RequestInvoker(this);
    connect(invoker, &RequestInvoker::finished, [this, sourceName](QNetworkReply::NetworkError error, QByteArray) {
        CHECK_RESPONSE_NOERROR(
            screenshotPutFailed, "Putting screenshot of %s failed: %d", qPrintable(sourceName), error
        );

        obs_log(LOG_INFO, "Put screenshot of %s succeeded", qPrintable(sourceName));
        emit screenshotPutSucceeded(sourceName);
    });
    invoker->put(req, imageBytes);

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
    // Polling to ping endpoint
    ping();
}

//--- SourceLinkSettingsStore class ---//

SourceLinkApiClientSettingsStore::SourceLinkApiClientSettingsStore(QObject *parent) : O0AbstractStore(parent)
{
    auto config_dir_path = obs_module_get_config_path(obs_current_module(), "");
    os_mkdirs(config_dir_path);
    bfree(config_dir_path);

    auto path = obs_module_get_config_path(obs_current_module(), SETTINGS_JSON_NAME);
    settingsData = obs_data_create_from_json_file(path);
    if (!settingsData) {
        settingsData = obs_data_create();
    }
    bfree(path);
    obs_log(LOG_DEBUG, "SourceLinkApiClientSettingsStore created");
}

SourceLinkApiClientSettingsStore::~SourceLinkApiClientSettingsStore()
{
    obs_data_release(settingsData);
    obs_log(LOG_DEBUG, "SourceLinkApiClientSettingsStore destroyed");
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
    auto path = obs_module_get_config_path(obs_current_module(), SETTINGS_JSON_NAME);
    obs_data_save_json_safe(settingsData, path, "tmp", "bak");
    bfree(path);
}
