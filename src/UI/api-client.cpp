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
#include "../plugin-support.h"
#include "api-client.hpp"
#include "common.hpp"

#define QENUM_NAME(o, e, v) (o::staticMetaObject.enumerator(o::staticMetaObject.indexOfEnumerator(#e)).valueToKey((v)))
#define GRANTFLOW_STR(v) QString(QENUM_NAME(O2, GrantFlow, v))

#define CLIENT_ID "testClientId"
#define CLIENT_SECRET "testClientSecret"
#define AUTHORIZE_URL "http://localhost:3000/oauth2/authorize"
#define TOKEN_URL "http://localhost:3000/oauth2/token"
#define ACCOUNT_INFO_URL "http://localhost:3000/api/v1/accounts/me"
#define PARTY_EVENTS_URL "http://localhost:3000/api/v1/events/my"
#define SCOPE "read write"
#define SETTINGS_JSON_NAME "settings.json"

//--- SourceLinkApiClient class ---

SourceLinkApiClient::SourceLinkApiClient(QObject *parent)
    : client(parent),
      settings(this),
      networkManager(this),
      requestor(&networkManager, &client, this)
{
    client.setRequestUrl(AUTHORIZE_URL);
    client.setTokenUrl(TOKEN_URL);
    client.setRefreshTokenUrl(TOKEN_URL);
    client.setClientId(CLIENT_ID);
    // FIXME: o2 didn't support PKCE yet
    client.setClientSecret(CLIENT_SECRET);
    client.setLocalPort(QRandomGenerator::system()->bounded(8000, 9000));
    client.setScope(QString::fromLatin1(SCOPE));
    client.setGrantFlow(O2::GrantFlow::GrantFlowAuthorizationCode);

    // Create a store object for writing the received tokens
    client.setStore(&settings);

    connect(&client, SIGNAL(linkedChanged()), this, SLOT(onLinkedChanged()));
    connect(&client, SIGNAL(linkingFailed()), this, SIGNAL(linkingFailed()));
    connect(&client, SIGNAL(linkingSucceeded()), this, SLOT(onLinkingSucceeded()));
    connect(&client, SIGNAL(openBrowser(QUrl)), this, SLOT(onOpenBrowser(QUrl)));
    connect(&client, SIGNAL(closeBrowser()), this, SLOT(onCloseBrowser()));
}

void SourceLinkApiClient::login(QWidget *parent)
{
    obs_log(
        LOG_DEBUG, "Starting OAuth 2 with grant flow type %s...", GRANTFLOW_STR(client.grantFlow()).toUtf8().constData()
    );
    client.link();
}

void SourceLinkApiClient::logout()
{
    client.unlink();
}

void SourceLinkApiClient::onOpenBrowser(const QUrl &url)
{
    QDesktopServices::openUrl(url);
}

void SourceLinkApiClient::onCloseBrowser() {}

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

    emit linkingSucceeded();
}

void SourceLinkApiClient::getAccountInfo()
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

        obs_log(LOG_INFO, "Received account: %s", accountInfo->getDisplayName().toUtf8().constData());
        emit accountInfoReady(accountInfo);
    });
    handler->get(QNetworkRequest(QUrl(QString(ACCOUNT_INFO_URL))));
}

void SourceLinkApiClient::getPartyEvents()
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
        QList<PartyEvent *> partyEvents;
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

//--- AbstractRequestHandler class ---

RequestHandler::RequestHandler(SourceLinkApiClient *apiClient)
    : apiClient(apiClient),
      QObject(apiClient)
{
    connect(
        &apiClient->requestor, SIGNAL(finished(int, QNetworkReply::NetworkError, QByteArray)), this,
        SLOT(onFinished(int, QNetworkReply::NetworkError, QByteArray))
    );
}

RequestHandler::~RequestHandler()
{
    disconnect(
        &apiClient->requestor, SIGNAL(finished(int, QNetworkReply::NetworkError, QByteArray)), this,
        SLOT(onFinished(int, QNetworkReply::NetworkError, QByteArray))
    );
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

//--- SourceLinkSettingsStore class ---

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
}

SourceLinkApiClientSettingsStore::~SourceLinkApiClientSettingsStore()
{
    obs_data_release(settingsData);
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
