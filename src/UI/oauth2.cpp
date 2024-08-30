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

#include <o2requestor.h>
#include <o0settingsstore.h>
#include <o0globals.h>
#include "../plugin-support.h"
#include "oauth2.hpp"
#include "common.hpp"

#define QENUM_NAME(o, e, v) (o::staticMetaObject.enumerator(o::staticMetaObject.indexOfEnumerator(#e)).valueToKey((v)))
#define GRANTFLOW_STR(v) QString(QENUM_NAME(O2, GrantFlow, v))

#define CLIENT_ID "testClientId"
#define CLIENT_SECRET "testClientSecret"
#define AUTHORIZE_URL "http://localhost:3000/oauth2/authorize"
#define TOKEN_URL "http://localhost:3000/oauth2/token"
#define ACCOUNT_INFO_URL "http://localhost:3000/api/v1/accounts/me"
#define SCOPE "read write"
#define SETTINGS_JSON_NAME "settings.json"

//--- SourceLinkAuth class ---

SourceLinkAuth::SourceLinkAuth(QObject *parent) : client(parent), settings(this)
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

void SourceLinkAuth::login(QWidget *parent)
{
    obs_log(
        LOG_DEBUG, "Starting OAuth 2 with grant flow type %s...", GRANTFLOW_STR(client.grantFlow()).toUtf8().constData()
    );
    client.link();
}

void SourceLinkAuth::logout()
{
    client.unlink();
}

void SourceLinkAuth::getAccountInfo()
{
    if (!client.linked()) {
        obs_log(LOG_WARNING, "ERROR: Application is not linked!");
        emit linkingFailed();
        return;
    }

    QString accountInfoUrl = QString(ACCOUNT_INFO_URL);
    QNetworkRequest request = QNetworkRequest(QUrl(accountInfoUrl));
    QNetworkAccessManager *mgr = new QNetworkAccessManager(this);
    O2Requestor *requestor = new O2Requestor(mgr, &client, this);
    requestor->setAddAccessTokenInQuery(false);
    requestor->setAccessTokenInAuthenticationHTTPHeaderFormat(QString::fromLatin1("Bearer %1"));

    connect(
        requestor, SIGNAL(finished(int, QNetworkReply::NetworkError, QByteArray)), this,
        SLOT(onAccountInfoReceived(int, QNetworkReply::NetworkError, QByteArray))
    );

    requestor->get(request);
    obs_log(LOG_DEBUG, "Requesting account info... Please wait.");
}

void SourceLinkAuth::onOpenBrowser(const QUrl &url)
{
    QDesktopServices::openUrl(url);
}

void SourceLinkAuth::onCloseBrowser() {}

void SourceLinkAuth::onLinkedChanged()
{
    obs_log(LOG_DEBUG, "Link changed!");
}

void SourceLinkAuth::onLinkingSucceeded()
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

void SourceLinkAuth::onAccountInfoReceived(int, QNetworkReply::NetworkError error, QByteArray replyData)
{
    if (error != QNetworkReply::NoError) {
        obs_log(LOG_ERROR, "Reply error: %d", error);
        emit accountInfoFailed();
        return;
    }

    QString reply(replyData);
    obs_log(LOG_DEBUG, "Reply: %s", reply.toUtf8().constData());

    auto jsonDoc = QJsonDocument::fromJson(replyData);
    auto accountInfo = AccountInfo::fromJson(jsonDoc.object());

    obs_log(LOG_INFO, "Account info: %s", accountInfo->getDisplayName().toUtf8().constData());
    emit accountInfoReceived(accountInfo);
}

//--- SourceLinkSettingsStore class ---

SourceLinkSettingsStore::SourceLinkSettingsStore(QObject *parent) : O0AbstractStore(parent)
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

SourceLinkSettingsStore::~SourceLinkSettingsStore()
{
    obs_data_release(settingsData);
}

QString SourceLinkSettingsStore::value(const QString &key, const QString &defaultValue)
{
    auto value = obs_data_get_string(settingsData, key.toUtf8().constData());
    if (value) {
        return QString::fromUtf8(value);
    } else {
        return defaultValue;
    }
}

void SourceLinkSettingsStore::setValue(const QString &key, const QString &value)
{
    obs_data_set_string(settingsData, key.toUtf8().constData(), value.toUtf8().constData());
    auto path = obs_module_get_config_path(obs_current_module(), SETTINGS_JSON_NAME);
    obs_data_save_json_safe(settingsData, path, "tmp", "bak");
    bfree(path);
}
