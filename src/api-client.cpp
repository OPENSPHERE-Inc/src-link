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

#include <o2requestor.h>
#include <o0settingsstore.h>
#include <o0globals.h>
#include "plugin-support.h"
#include "api-client.hpp"
#include "UI/common.hpp"

#define QENUM_NAME(o, e, v) (o::staticMetaObject.enumerator(o::staticMetaObject.indexOfEnumerator(#e)).valueToKey((v)))
#define GRANTFLOW_STR(v) QString(QENUM_NAME(O2, GrantFlow, v))

#define LOCAL_DEBUG

#ifdef LOCAL_DEBUG
#  define API_SERVER "http://localhost:3000"
#else
#  define API_SERVER "https://source-link-test.opensphere.co.jp"
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
#define PING_URL (API_SERVER "/api/v1/ping")
#define SCOPE "read write"
#define SETTINGS_JSON_NAME "settings.json"
#define POLLING_INTERVAL 10000

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
      seatAllocation(nullptr),
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
    pollingTimer->setInterval(POLLING_INTERVAL);
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

void SourceLinkApiClient::ping()
{
    if (!client->linked()) {
        obs_log(LOG_ERROR, "Client is offline.");
        emit pingFailed();
        return;
    }

    obs_log(LOG_DEBUG, "Pinging API server.");
    auto invoker = new RequestInvoker(this);
    connect(invoker, &RequestInvoker::finished, this, [this](QNetworkReply::NetworkError error, QByteArray replyData) {
        if (error != QNetworkReply::NoError) {
            obs_log(LOG_ERROR, "Pinging API server failed: %d", error);
            emit pingFailed();
            return;
        }

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
}

void SourceLinkApiClient::onO2OpenBrowser(const QUrl &url)
{
    QDesktopServices::openUrl(url);
}

void SourceLinkApiClient::onO2LinkedChanged()
{
    if (!client->linked()) {
        obs_log(LOG_ERROR, "Client is offline.");
        return;        
    }

    obs_log(LOG_DEBUG, "The API client link has been changed.");
}

void SourceLinkApiClient::onO2LinkingSucceeded()
{
    if (!client->linked()) {
        obs_log(LOG_ERROR, "Client is offline.");
        return;        
    }

    obs_log(LOG_DEBUG, "The API client has linked up.");

    requestOnlineResources();

    emit linkingSucceeded();
}

void SourceLinkApiClient::onO2RefreshFinished(QNetworkReply::NetworkError error)
{
    if (error != QNetworkReply::NoError) {
        obs_log(LOG_ERROR, "Refreshing token failed: %d", error);
        return;
    }
    if (!client->linked()) {
        obs_log(LOG_ERROR, "Client is offline.");
        return;
    }

    // Schedule next refresh
    QTimer::singleShot(client->expires() * 1000 - 60000 - QDateTime().currentMSecsSinceEpoch(), client, SLOT(refresh()));
}

void SourceLinkApiClient::requestOnlineResources()
{
    if (!client->linked()) {
        obs_log(LOG_ERROR, "Client is offline.");
        return;
    }

    requestAccountInfo();
    requestParties();
    if (!getPartyId().isEmpty()) {
        requestPartyEvents(getPartyId());
    }
    requestStages();
}

void SourceLinkApiClient::requestAccountInfo()
{
    if (!client->linked()) {
        obs_log(LOG_ERROR, "Client is offline.");
        emit accountInfoFailed();
        return;
    }

    obs_log(LOG_DEBUG, "Requesting account info.");
    auto invoker = new RequestInvoker(this);
    connect(invoker, &RequestInvoker::finished, this, [this](QNetworkReply::NetworkError error, QByteArray replyData) {
        if (error != QNetworkReply::NoError) {
            obs_log(LOG_ERROR, "Requesting account info failed: %d", error);
            emit accountInfoFailed();
            return;
        }

        auto jsonDoc = QJsonDocument::fromJson(replyData);
        if (accountInfo) {
            accountInfo->deleteLater();
        }
        accountInfo = AccountInfo::fromJsonObject(jsonDoc.object(), this);

        obs_log(LOG_INFO, "Received account: %s", qPrintable(accountInfo->getDisplayName()));
        emit accountInfoReady(accountInfo);
    });
    invoker->get(QNetworkRequest(QUrl(QString(ACCOUNT_INFO_URL))));
}

void SourceLinkApiClient::requestParties()
{
    if (!client->linked()) {
        obs_log(LOG_ERROR, "Client is offline.");
        emit partiesFailed();
        return;
    }

    obs_log(LOG_DEBUG, "Requesting parties.");
    auto invoker = new RequestInvoker(this);
    connect(invoker, &RequestInvoker::finished, [this](QNetworkReply::NetworkError error, QByteArray replyData) {
        if (error != QNetworkReply::NoError) {
            obs_log(LOG_ERROR, "Requesting parties failed: %d", error);
            emit partiesFailed();
            return;
        }

        auto jsonDoc = QJsonDocument::fromJson(replyData);
        qDeleteAll(parties);
        parties.clear();
        foreach(const QJsonValue &partyItem, jsonDoc.array())
        {
            auto party = Party::fromJsonObject(partyItem.toObject(), this);
            parties.append(party);
        }

        obs_log(LOG_INFO, "Received %d parties", parties.size());
        emit partiesReady(parties);
    });
    invoker->get(QNetworkRequest(QUrl(QString(PARTIES_URL))));
}

void SourceLinkApiClient::requestPartyEvents(const QString &partyId)
{
    if (!client->linked()) {
        obs_log(LOG_ERROR, "Client is offline.");
        emit partyEventsFailed();
        return;
    }

    obs_log(LOG_DEBUG, "Requesting party events for %s.", qPrintable(partyId));
    auto invoker = new RequestInvoker(this);
    connect(
        invoker, &RequestInvoker::finished,
        [this, partyId](QNetworkReply::NetworkError error, QByteArray replyData) {
            if (error != QNetworkReply::NoError) {
                obs_log(LOG_ERROR, "Requesting party events for %s failed: %d", qPrintable(partyId), error);
                emit partyEventsFailed();
                return;
            }

            auto jsonDoc = QJsonDocument::fromJson(replyData);
            qDeleteAll(partyEvents);
            partyEvents.clear();
            foreach(const QJsonValue &partyEventItem, jsonDoc.array())
            {
                auto partyEvent = PartyEvent::fromJsonObject(partyEventItem.toObject(), this);
                partyEvents.append(partyEvent);
            }

            obs_log(LOG_INFO, "Received %d party events for %s", partyEvents.size(), qPrintable(partyId));
            emit partyEventsReady(partyEvents);
        }
    );

    auto url = QUrl(QString(PARTY_EVENTS_URL));
    auto query = QUrlQuery();
    query.addQueryItem("party_id", partyId);
    url.setQuery(query);
    invoker->get(QNetworkRequest(url));
}

void SourceLinkApiClient::requestStages()
{
    if (!client->linked()) {
        obs_log(LOG_ERROR, "Client is offline.");
        emit stagesFailed();
        return;
    }

    obs_log(LOG_DEBUG, "Requesting stages.");
    auto invoker = new RequestInvoker(this);
    connect(invoker, &RequestInvoker::finished, [this](QNetworkReply::NetworkError error, QByteArray replyData) {
        if (error != QNetworkReply::NoError) {
            obs_log(LOG_ERROR, "Requesting stages failed: %d", error);
            emit stagesFailed();
            return;
        }

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
}

void SourceLinkApiClient::requestSeatAllocation()
{
    if (!client->linked()) {
        obs_log(LOG_ERROR, "Client is offline.");
        emit seatAllocationFailed();
        return;
    }

    obs_log(LOG_DEBUG, "Requesting seat allocation for %s", qPrintable(uuid));
    auto invoker = new RequestInvoker(this);
    connect(invoker, &RequestInvoker::finished, [this](QNetworkReply::NetworkError error, QByteArray replyData) {
        if (error != QNetworkReply::NoError) {
            obs_log(LOG_ERROR, "Requesting seat allocation for %s failed: %d", qPrintable(uuid), error);
            emit seatAllocationFailed();
            return;
        }

        auto jsonDoc = QJsonDocument::fromJson(replyData);
        if (seatAllocation) {
            seatAllocation->deleteLater();
        }
        seatAllocation = StageSeatAllocation::fromJsonObject(jsonDoc.object(), this);

        obs_log(LOG_INFO, "Received seat allocation for %s", qPrintable(seatAllocation->getId()));
        emit seatAllocationReady(seatAllocation);
    });
    invoker->get(QNetworkRequest(QUrl(QString(STAGE_SEAT_ALLOCATIONS_URL) + "/" + uuid)));
}

void SourceLinkApiClient::putConnection(
    const QString &sourceUuid, const QString &stageId, const QString &seatName, const QString &sourceName,
    const QString &protocol, const int port, const QString &parameters, const int maxBitrate, const int minBitrate,
    const int width, const int height
)
{
    if (!client->linked()) {
        obs_log(LOG_ERROR, "Client is offline.");
        emit connectionPutFailed();
        return;
    }

    obs_log(LOG_DEBUG, "Putting stage connection: %s", qPrintable(sourceUuid));
    auto invoker = new RequestInvoker(this);
    connect(
        invoker, &RequestInvoker::finished,
        [this, sourceUuid](QNetworkReply::NetworkError error, QByteArray replyData) {
            if (error != QNetworkReply::NoError) {
                obs_log(LOG_ERROR, "Putting stage coinnection %s failed: %d", qPrintable(sourceUuid), error);
                emit connectionPutFailed();
                return;
            }

            auto jsonDoc = QJsonDocument::fromJson(replyData);
            auto connection = StageConnection::fromJsonObject(jsonDoc.object(), this);

            obs_log(LOG_INFO, "Put stage connection %s succeeded", qPrintable(connection->getId()));
            emit connectionPutSucceeded(connection);
        }
    );

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

    invoker->put(req, QJsonDocument(body).toJson());
}

void SourceLinkApiClient::deleteConnection(const QString &sourceUuid)
{
    if (!client->linked()) {
        obs_log(LOG_ERROR, "Client is offline.");
        emit connectionDeleteFailed();
        return;
    }

    obs_log(LOG_DEBUG, "Deleting stage connection of %s", qPrintable(sourceUuid));
    auto invoker = new RequestInvoker(this);
    connect(invoker, &RequestInvoker::finished, [this, sourceUuid](QNetworkReply::NetworkError error, QByteArray) {
        if (error != QNetworkReply::NoError) {
            obs_log(LOG_ERROR, "RDeleting stage connection of %s: %d", qPrintable(sourceUuid), error);
            emit connectionDeleteFailed();
            return;
        }

        obs_log(LOG_INFO, "Delete stage connection of %s succeeded", qPrintable(sourceUuid));
        emit connectionDeleteSucceeded(sourceUuid);
    });

    auto req = QNetworkRequest(QUrl(QString(STAGES_CONNECTIONS_URL) + "/" + sourceUuid));
    invoker->deleteResource(req);
}

void SourceLinkApiClient::putSeatAllocation()
{
    if (!client->linked()) {
        obs_log(LOG_ERROR, "Client is offline.");
        emit seatAllocationPutFailed();
        return;
    }

    obs_log(LOG_DEBUG, "Putting stage seat allocation of %s", qPrintable(uuid));
    auto invoker = new RequestInvoker(this);
    connect(invoker, &RequestInvoker::finished, [this](QNetworkReply::NetworkError error, QByteArray replyData) {
        if (error != QNetworkReply::NoError) {
            obs_log(LOG_ERROR, "Putting stage seat allocation of %s failed: %d", qPrintable(uuid), error);
            emit seatAllocationPutFailed();
            return;
        }

        auto jsonDoc = QJsonDocument::fromJson(replyData);
        auto seatAllocation = StageSeatAllocation::fromJsonObject(jsonDoc.object(), this);

        obs_log(LOG_INFO, "Put stage seat allocation of %s succeeded", qPrintable(uuid));
        emit seatAllocationPutSucceeded(seatAllocation);
    });

    auto req = QNetworkRequest(QUrl(QString(STAGE_SEAT_ALLOCATIONS_URL) + "/" + uuid));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QJsonObject body;
    body["party_id"] = getPartyId();
    body["party_event_id"] = getPartyEventId();

    invoker->put(req, QJsonDocument(body).toJson());
}

void SourceLinkApiClient::deleteSeatAllocation()
{
    if (!client->linked()) {
        obs_log(LOG_ERROR, "Client is offline.");
        emit seatAllocationDeleteFailed();
        return;
    }

    obs_log(LOG_DEBUG, "Deleting stage seat allocation of %s", qPrintable(uuid));
    auto invoker = new RequestInvoker(this);
    connect(invoker, &RequestInvoker::finished, [this](QNetworkReply::NetworkError error, QByteArray) {
        if (error != QNetworkReply::NoError) {
            obs_log(LOG_ERROR, "Deleting stage seat allocation of %s failed: %d", qPrintable(uuid), error);
            emit seatAllocationDeleteFailed();
            return;
        }

        obs_log(LOG_INFO, "Delete stage seat allocation %s succeeded", qPrintable(uuid));
        emit seatAllocationDeleteSucceeded(uuid);
    });

    auto req = QNetworkRequest(QUrl(QString(STAGE_SEAT_ALLOCATIONS_URL) + "/" + uuid));
    invoker->deleteResource(req);
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

//--- AbstractRequestHandler class ---//

RequestInvoker::RequestInvoker(SourceLinkApiClient *_apiClient, QObject *parent)
    : QObject(parent),
      apiClient(_apiClient),
      requestor(_apiClient->getRequestor())
{
    connect(
        requestor, SIGNAL(finished(int, QNetworkReply::NetworkError, QByteArray)), this,
        SLOT(onRequestorFinished(int, QNetworkReply::NetworkError, QByteArray))
    );
    connect(
        apiClient->getO2Client(), SIGNAL(refreshFinished(QNetworkReply::NetworkError)), this,
        SLOT(onO2RefreshFinished(QNetworkReply::NetworkError))
    );
    obs_log(LOG_DEBUG, "RequestInvoker created");
}

RequestInvoker::~RequestInvoker()
{
    disconnect(
        requestor, SIGNAL(finished(int, QNetworkReply::NetworkError, QByteArray)), this,
        SLOT(onRequestorFinished(int, QNetworkReply::NetworkError, QByteArray))
    );
    obs_log(LOG_DEBUG, "RequestInvoker destroyed");
}

void RequestInvoker::onRequestorFinished(int _requestId, QNetworkReply::NetworkError error, QByteArray data)
{
    if (requestId != _requestId) {
        return;
    }
    obs_log(LOG_DEBUG, "Request finished: %d", _requestId);

    apiClient->requestQueue.removeOne(this);
    emit finished(error, data);
    deleteLater();
}

void RequestInvoker::onO2RefreshFinished(QNetworkReply::NetworkError error)
{
    if (requestId != -2) {
        return;
    }
    obs_log(LOG_DEBUG, "Refresh finished");

    apiClient->requestQueue.removeOne(this);
    emit finished(error, nullptr);
    deleteLater();
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
