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

#include <o2requestor.h>
#include <o0settingsstore.h>
#include <o0globals.h>
#include "plugin-support.h"
#include "api-client.hpp"
#include "UI/common.hpp"

#define QENUM_NAME(o, e, v) (o::staticMetaObject.enumerator(o::staticMetaObject.indexOfEnumerator(#e)).valueToKey((v)))
#define GRANTFLOW_STR(v) QString(QENUM_NAME(O2, GrantFlow, v))

#define CLIENT_ID "testClientId"
#define CLIENT_SECRET "testClientSecret"
#define AUTHORIZE_URL "http://localhost:3000/oauth2/authorize"
#define TOKEN_URL "http://localhost:3000/oauth2/token"
#define ACCOUNT_INFO_URL "http://localhost:3000/api/v1/accounts/me"
#define PARTY_EVENTS_URL "http://localhost:3000/api/v1/events/my"
#define STAGES_URL "http://localhost:3000/api/v1/stages"
#define STAGES_CONNECTIONS_URL "http://localhost:3000/api/v1/stages/connections"
#define SCOPE "read write"
#define SETTINGS_JSON_NAME "settings.json"

//--- SourceLinkApiClient class ---//

SourceLinkApiClient::SourceLinkApiClient(QObject *parent)
    : QObject(parent),
      networkManager(this),
      // Create a store object for writing the received tokens (It will be child of O2 instance)
      settings(new SourceLinkApiClientSettingsStore()),
      client(this, &networkManager, settings),
      requestor(&networkManager, &client, this)
{
    client.setRequestUrl(AUTHORIZE_URL);
    client.setTokenUrl(TOKEN_URL);
    client.setRefreshTokenUrl(TOKEN_URL);
    client.setClientId(CLIENT_ID);
    // FIXME: o2 didn't support PKCE yet, so we use embeded client secret for now
    client.setClientSecret(CLIENT_SECRET);
    client.setLocalPort(QRandomGenerator::system()->bounded(8000, 9000));
    client.setScope(QString::fromLatin1(SCOPE));
    client.setGrantFlow(O2::GrantFlow::GrantFlowAuthorizationCode);

    // Load stored settings
    accountId = settings->value("account.id");
    accountDisplayName = settings->value("account.displayName");
    accountPictureId = settings->value("account.pictureId");

    // Requestor uses header as access token transport
    requestor.setAddAccessTokenInQuery(false);
    requestor.setAccessTokenInAuthenticationHTTPHeaderFormat(QString::fromLatin1("Bearer %1"));

    connect(&client, SIGNAL(linkedChanged()), this, SLOT(onLinkedChanged()));
    connect(&client, SIGNAL(linkingFailed()), this, SIGNAL(linkingFailed()));
    connect(&client, SIGNAL(linkingSucceeded()), this, SLOT(onLinkingSucceeded()));
    connect(&client, SIGNAL(openBrowser(QUrl)), this, SLOT(onOpenBrowser(QUrl)));

    if (client.linked()) {
        requestAccountInfo();
        requestPartyEvents();
        requestStages();
    }

    obs_log(LOG_DEBUG, "SourceLinkApiClient created");
}

SourceLinkApiClient::~SourceLinkApiClient()
{
    obs_log(LOG_DEBUG, "SourceLinkApiClient destroyed");
}

void SourceLinkApiClient::login()
{
    obs_log(
        LOG_DEBUG, "Starting OAuth 2 with grant flow type %s", GRANTFLOW_STR(client.grantFlow()).toUtf8().constData()
    );
    client.link();
}

void SourceLinkApiClient::logout()
{
    client.unlink();
}

bool SourceLinkApiClient::isLoggedIn()
{
    return client.linked();
}

void SourceLinkApiClient::onOpenBrowser(const QUrl &url)
{
    QDesktopServices::openUrl(url);
}

void SourceLinkApiClient::onLinkedChanged()
{
    obs_log(LOG_DEBUG, "Link changed!");
}

void SourceLinkApiClient::onLinkingSucceeded()
{
    if (!client.linked()) {
        return;
    }

    QVariantMap tokens = client.extraTokens();
    if (!tokens.isEmpty()) {
        emit extraTokensReady(tokens);
        obs_log(LOG_DEBUG, "Extra tokens in response:");

        foreach(QString key, tokens.keys())
        {
            obs_log(
                LOG_DEBUG, "\t%s: %s...", key.toUtf8().constData(),
                tokens.value(key).toString().left(3).toUtf8().constData()
            );
        }
    }

    requestAccountInfo();
    requestPartyEvents();
    requestStages();

    emit linkingSucceeded();
}

void SourceLinkApiClient::requestAccountInfo()
{
    if (!client.linked()) {
        obs_log(LOG_ERROR, "Client is offline.");
        emit accountInfoFailed();
        return;
    }

    obs_log(LOG_DEBUG, "Requesting account info.");
    auto handler = new RequestHandler(this);
    connect(handler, &RequestHandler::finished, this, [this](QNetworkReply::NetworkError error, QByteArray replyData) {
        if (error != QNetworkReply::NoError) {
            obs_log(LOG_ERROR, "Reply error: %d", error);
            emit accountInfoFailed();
            return;
        }

        auto jsonDoc = QJsonDocument::fromJson(replyData);
        auto accountInfo = AccountInfo::fromJsonObject(jsonDoc.object(), this);

        accountId = accountInfo->getId();
        accountDisplayName = accountInfo->getDisplayName();
        accountPictureId = accountInfo->getPictureId();

        // Save account info to settings.json
        settings->setValue("account.id", accountId);
        settings->setValue("account.displayName", accountDisplayName);
        settings->setValue("account.pictureId", accountPictureId);

        obs_log(LOG_INFO, "Received account: %s", accountInfo->getDisplayName().toUtf8().constData());
        emit accountInfoReady(accountInfo);
    });
    handler->get(QNetworkRequest(QUrl(QString(ACCOUNT_INFO_URL))));
}

void SourceLinkApiClient::requestPartyEvents()
{
    if (!client.linked()) {
        obs_log(LOG_ERROR, "Client is offline.");
        emit partyEventsFailed();
        return;
    }

    obs_log(LOG_DEBUG, "Requesting party events.");
    auto handler = new RequestHandler(this);
    connect(handler, &RequestHandler::finished, [this](QNetworkReply::NetworkError error, QByteArray replyData) {
        if (error != QNetworkReply::NoError) {
            obs_log(LOG_ERROR, "Reply error: %d", error);
            emit partyEventsFailed();
            return;
        }

        auto jsonDoc = QJsonDocument::fromJson(replyData);
        partyEvents.clear();
        foreach(const QJsonValue partyEventItem, jsonDoc.array())
        {
            auto partyEvent = PartyEvent::fromJsonObject(partyEventItem.toObject(), this);
            partyEvents.append(partyEvent);
        }

        obs_log(LOG_INFO, "Received %d party events", partyEvents.size());
        emit partyEventsReady(partyEvents);
    });
    handler->get(QNetworkRequest(QUrl(QString(PARTY_EVENTS_URL))));
}

void SourceLinkApiClient::requestStages()
{
    if (!client.linked()) {
        obs_log(LOG_ERROR, "Client is offline.");
        emit stagesFailed();
        return;
    }

    obs_log(LOG_DEBUG, "Requesting stages.");
    auto handler = new RequestHandler(this);
    connect(handler, &RequestHandler::finished, [this](QNetworkReply::NetworkError error, QByteArray replyData) {
        if (error != QNetworkReply::NoError) {
            obs_log(LOG_ERROR, "Reply error: %d", error);
            emit stagesFailed();
            return;
        }

        auto jsonDoc = QJsonDocument::fromJson(replyData);
        stages.clear();
        foreach(const QJsonValue stageItem, jsonDoc.array())
        {
            auto stage = Stage::fromJsonObject(stageItem.toObject(), this);
            stages.append(stage);
        }

        obs_log(LOG_INFO, "Received %d stages", stages.size());
        emit stagesReady(stages);
    });
    handler->get(QNetworkRequest(QUrl(QString(STAGES_URL))));
}

void SourceLinkApiClient::putConnection(
    const QString &uuid, const QString &stageId, const QString &seatName, const QString &sourceName,
    const QString &protocol, const int port, const QString &parameters, const int maxBitrate, const int minBitrate,
    const int width, const int height
)
{
    if (!client.linked()) {
        obs_log(LOG_ERROR, "Client is offline.");
        emit connectionPutFailed();
        return;
    }

    obs_log(LOG_DEBUG, "Requesting stages.");
    auto handler = new RequestHandler(this);
    connect(handler, &RequestHandler::finished, [this](QNetworkReply::NetworkError error, QByteArray replyData) {
        if (error != QNetworkReply::NoError) {
            obs_log(LOG_ERROR, "Reply error: %d", error);
            emit connectionPutFailed();
            return;
        }

        auto jsonDoc = QJsonDocument::fromJson(replyData);
        auto connection = StageConnection::fromJsonObject(jsonDoc.object(), this);

        obs_log(LOG_INFO, "Put stage connection %1 succeeded", connection->getId());
        emit connectionPutSucceeded(connection);
    });

    auto req = QNetworkRequest(QUrl(QString(STAGES_CONNECTIONS_URL) + "/" + uuid));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    auto body = QJsonObject();
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

    handler->put(req, QJsonDocument(body).toJson());
}

void SourceLinkApiClient::deleteConnection(const QString &uuid)
{
    if (!client.linked()) {
        obs_log(LOG_ERROR, "Client is offline.");
        emit connectionDeleteFailed();
        return;
    }

    obs_log(LOG_DEBUG, "Requesting stages.");
    auto handler = new RequestHandler(this);
    connect(handler, &RequestHandler::finished, [this, uuid](QNetworkReply::NetworkError error, QByteArray) {
        if (error != QNetworkReply::NoError) {
            obs_log(LOG_ERROR, "Reply error: %d", error);
            emit connectionDeleteFailed();
            return;
        }

        obs_log(LOG_INFO, "Delete stage connection %1 succeeded", uuid.toUtf8().constData());
        emit connectionDeleteSucceeded(uuid);
    });

    auto req = QNetworkRequest(QUrl(QString(STAGES_CONNECTIONS_URL) + "/" + uuid));
    handler->deleteResource(req);
}

//--- AbstractRequestHandler class ---//

RequestHandler::RequestHandler(SourceLinkApiClient *apiClient) : QObject(apiClient), apiClient(apiClient)
{
    connect(
        &apiClient->getRequestor(), SIGNAL(finished(int, QNetworkReply::NetworkError, QByteArray)), this,
        SLOT(onFinished(int, QNetworkReply::NetworkError, QByteArray))
    );
    obs_log(LOG_DEBUG, "RequestHandler created");
}

RequestHandler::~RequestHandler()
{
    disconnect(
        &apiClient->getRequestor(), SIGNAL(finished(int, QNetworkReply::NetworkError, QByteArray)), this,
        SLOT(onFinished(int, QNetworkReply::NetworkError, QByteArray))
    );
    obs_log(LOG_DEBUG, "RequestHandler destroyed");
}

void RequestHandler::onFinished(int _requestId, QNetworkReply::NetworkError error, QByteArray data)
{
    if (requestId != _requestId) {
        return;
    }

    apiClient->requestQueue.removeOne(this);
    emit finished(error, data);
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
    auto value = obs_data_get_string(settingsData, key.toUtf8().constData());
    if (value) {
        return QString::fromUtf8(value);
    } else {
        return defaultValue;
    }
}

void SourceLinkApiClientSettingsStore::setValue(const QString &key, const QString &value)
{
    obs_data_set_string(settingsData, key.toUtf8().constData(), value.toUtf8().constData());
    auto path = obs_module_get_config_path(obs_current_module(), SETTINGS_JSON_NAME);
    obs_data_save_json_safe(settingsData, path, "tmp", "bak");
    bfree(path);
}
