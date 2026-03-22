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

#include <QJsonDocument>
#include <QJsonObject>
#include <QUrlQuery>
#include <QUuid>
#include <QRandomGenerator>
#include <QDateTime>

#include <obs-module.h>

#include "oauth2-client.hpp"
#include "curl-http-client.hpp"
#include "local-http-server.hpp"
#include "http-response.hpp"
#include "../plugin-support.h"

OAuth2Client::OAuth2Client(CurlHttpClient *httpClient, QObject *parent)
    : QObject(parent),
      httpClient(httpClient),
      localServer(new LocalHttpServer(this)),
      localPort_(0),
      store_(nullptr),
      linked_(false),
      refreshing_(false),
      expires_(0)
{
    obs_log(LOG_DEBUG, "OAuth2Client created");

    localhostPolicy_ = "http://127.0.0.1:%1/";
    connect(localServer, &LocalHttpServer::verificationReceived, this, &OAuth2Client::onVerificationReceived);
}

OAuth2Client::~OAuth2Client()
{
    obs_log(LOG_DEBUG, "OAuth2Client destroying");
    // Cancel any in-flight HTTP requests (token exchange, refresh) without invoking
    // callbacks, to avoid dangling `this` access in lambdas (R4-4) and prevent
    // canceling unrelated requests if CurlHttpClient is shared (R5-1).
    disconnect(this);
    httpClient->cancelAll(false);
}

void OAuth2Client::setClientId(const QString &clientId)
{
    clientId_ = clientId;
}

void OAuth2Client::setClientSecret(const QString &clientSecret)
{
    clientSecret_ = clientSecret;
}

void OAuth2Client::setScope(const QString &scope)
{
    scope_ = scope;
}

void OAuth2Client::setRequestUrl(const QString &url)
{
    requestUrl_ = QUrl(url);
}

void OAuth2Client::setTokenUrl(const QString &url)
{
    tokenUrl_ = QUrl(url);
}

void OAuth2Client::setRefreshTokenUrl(const QString &url)
{
    refreshTokenUrl_ = QUrl(url);
}

void OAuth2Client::setLocalhostPolicy(const QString &policy)
{
    localhostPolicy_ = policy;
}

void OAuth2Client::setLocalPort(int port)
{
    localPort_ = port;
}

void OAuth2Client::setReplyContent(const QByteArray &content)
{
    replyContent_ = content;
}

void OAuth2Client::setStore(OAuth2TokenStore *store)
{
    store_ = store;
    loadTokens();
}

bool OAuth2Client::linked() const
{
    return linked_;
}

QString OAuth2Client::token() const
{
    return token_;
}

qint64 OAuth2Client::expires() const
{
    return expires_;
}

void OAuth2Client::link()
{
    if (linked_) {
        // O2 compatibility: O2::link() emits linkingSucceeded() when already linked,
        // allowing the consumer to re-trigger the post-login flow (account info, uplink, websocket).
        obs_log(LOG_DEBUG, "OAuth2Client: Already linked");
        emit linkingSucceeded();
        return;
    }

    // Guard against re-entrant calls while auth flow is in progress
    if (localServer->isListening()) {
        obs_log(LOG_DEBUG, "OAuth2Client: Auth flow already in progress");
        return;
    }

    // Choose port — retry with different random ports if bind fails
    int port = 0;
    static constexpr int maxRetries = 5;
    for (int i = 0; i < maxRetries; i++) {
        port = localPort_ > 0 ? localPort_ : QRandomGenerator::global()->bounded(8000, 9000);
        if (localServer->listen(port)) {
            break;
        }
        obs_log(LOG_WARNING, "OAuth2Client: Port %d unavailable, retrying (%d/%d)", port, i + 1, maxRetries);
        port = 0;
        // If a fixed port was specified, no point retrying
        if (localPort_ > 0) {
            break;
        }
    }
    if (port == 0) {
        obs_log(LOG_ERROR, "OAuth2Client: Failed to start local server after %d attempts", maxRetries);
        emit linkingFailed();
        return;
    }

    localServer->setReplyContent(replyContent_);

    // Generate random state for CSRF protection
    pendingState_ = QUuid::createUuid().toString(QUuid::WithoutBraces);

    // Build authorization URL
    QUrl authUrl(requestUrl_);
    QUrlQuery query;
    query.addQueryItem("response_type", "code");
    query.addQueryItem("client_id", clientId_);
    redirectUri_ = QString(localhostPolicy_).arg(localServer->port());
    query.addQueryItem("redirect_uri", redirectUri_);
    query.addQueryItem("scope", scope_);
    query.addQueryItem("state", pendingState_);
    authUrl.setQuery(query);

    obs_log(LOG_DEBUG, "OAuth2Client: Starting OAuth2 Authorization Code flow");
    emit openBrowser(authUrl);
}

void OAuth2Client::unlink()
{
    obs_log(LOG_DEBUG, "OAuth2Client: Unlinking");

    bool wasLinked = linked_;

    token_.clear();
    refreshToken_.clear();
    expires_ = 0;
    linked_ = false;

    saveTokens();
    // O2 compatibility: O2::unlink() emits linkedChanged (only if value changed)
    // then linkingSucceeded. Consumer checks linked()==false in onLinkingSucceeded
    // to run logout flow.
    if (wasLinked) {
        emit linkedChanged();
    }
    emit linkingSucceeded();
}

void OAuth2Client::refresh()
{
    if (refreshToken_.isEmpty()) {
        obs_log(LOG_WARNING, "OAuth2Client: No refresh token available");
        emit refreshFinished(HttpError::AuthenticationRequired);
        return;
    }

    if (refreshing_) {
        // Already refreshing — callers will be notified via the in-progress refreshFinished signal.
        // This prevents token rotation race when multiple 401 responses trigger concurrent refreshes.
        obs_log(LOG_DEBUG, "OAuth2Client: Refresh already in progress, waiting for completion");
        return;
    }

    refreshing_ = true;
    obs_log(LOG_DEBUG, "OAuth2Client: Refreshing access token");

    QUrlQuery postData;
    postData.addQueryItem("grant_type", "refresh_token");
    postData.addQueryItem("refresh_token", refreshToken_);
    postData.addQueryItem("client_id", clientId_);
    postData.addQueryItem("client_secret", clientSecret_);

    QByteArray body = postData.query(QUrl::FullyEncoded).toUtf8();

    QMap<QByteArray, QByteArray> headers;
    headers.insert("Content-Type", "application/x-www-form-urlencoded");

    QUrl url = refreshTokenUrl_.isEmpty() ? tokenUrl_ : refreshTokenUrl_;

    httpClient->post(url, headers, body, [this](HttpResponse response) {
        if (response.error != HttpError::NoError) {
            obs_log(LOG_ERROR, "OAuth2Client: Token refresh failed with error %d", static_cast<int>(response.error));
            refreshing_ = false;
            // O2 compatibility: O2::onRefreshError() calls unlink() before emitting refreshFinished.
            // This clears stale tokens and triggers the consumer's logout flow,
            // preventing a zombie "linked" state with an invalid token.
            unlink();
            emit refreshFinished(HttpError::AuthenticationRequired);
            return;
        }

        QJsonDocument doc = QJsonDocument::fromJson(response.data);
        if (!doc.isObject()) {
            obs_log(LOG_ERROR, "OAuth2Client: Invalid JSON in refresh response");
            refreshing_ = false;
            unlink();
            emit refreshFinished(HttpError::AuthenticationRequired);
            return;
        }

        QJsonObject obj = doc.object();
        QString accessToken = obj.value("access_token").toString();
        if (accessToken.isEmpty()) {
            obs_log(LOG_ERROR, "OAuth2Client: Server returned empty access_token on refresh");
            refreshing_ = false;
            unlink();
            emit refreshFinished(HttpError::AuthenticationRequired);
            return;
        }

        token_ = accessToken;

        // Some providers return a new refresh token
        if (obj.contains("refresh_token")) {
            refreshToken_ = obj.value("refresh_token").toString();
        }

        qint64 expiresIn = static_cast<qint64>(obj.value("expires_in").toInt(0));
        expires_ = QDateTime::currentSecsSinceEpoch() + expiresIn;

        saveTokens();

        refreshing_ = false;
        obs_log(LOG_DEBUG, "OAuth2Client: Token refreshed successfully, expires in %lld seconds", expiresIn);
        // O2 compatibility: O2::onRefreshFinished() emits linkedChanged,
        // linkingSucceeded, then refreshFinished in that order.
        emit linkedChanged();
        emit linkingSucceeded();
        emit refreshFinished(HttpError::NoError);
    });
}

void OAuth2Client::onVerificationReceived(const QMap<QString, QString> &params)
{
    localServer->close();
    emit closeBrowser();

    // Verify state for CSRF protection
    if (params.value("state") != pendingState_) {
        obs_log(LOG_ERROR, "OAuth2Client: State mismatch in OAuth2 callback");
        emit linkingFailed();
        return;
    }
    pendingState_.clear(); // Prevent replay of the same state value

    // Check for error response
    if (params.contains("error")) {
        obs_log(LOG_ERROR, "OAuth2Client: OAuth2 error: %s", qUtf8Printable(params.value("error")));
        emit linkingFailed();
        return;
    }

    QString code = params.value("code");
    if (code.isEmpty()) {
        obs_log(LOG_ERROR, "OAuth2Client: No authorization code in callback");
        emit linkingFailed();
        return;
    }

    exchangeCodeForToken(code);
}

void OAuth2Client::exchangeCodeForToken(const QString &code)
{
    obs_log(LOG_DEBUG, "OAuth2Client: Exchanging authorization code for token");

    QUrlQuery postData;
    postData.addQueryItem("grant_type", "authorization_code");
    postData.addQueryItem("code", code);
    postData.addQueryItem("redirect_uri", redirectUri_);
    postData.addQueryItem("client_id", clientId_);
    postData.addQueryItem("client_secret", clientSecret_);

    QByteArray body = postData.query(QUrl::FullyEncoded).toUtf8();

    QMap<QByteArray, QByteArray> headers;
    headers.insert("Content-Type", "application/x-www-form-urlencoded");

    httpClient->post(tokenUrl_, headers, body, [this](HttpResponse response) {
        if (response.error != HttpError::NoError) {
            obs_log(
                LOG_ERROR, "OAuth2Client: Token exchange failed with error %d", static_cast<int>(response.error)
            );
            emit linkingFailed();
            return;
        }

        QJsonDocument doc = QJsonDocument::fromJson(response.data);
        if (!doc.isObject()) {
            obs_log(LOG_ERROR, "OAuth2Client: Invalid JSON in token response");
            emit linkingFailed();
            return;
        }

        QJsonObject obj = doc.object();
        QString accessToken = obj.value("access_token").toString();
        if (accessToken.isEmpty()) {
            obs_log(LOG_ERROR, "OAuth2Client: Server returned empty access_token");
            emit linkingFailed();
            return;
        }

        token_ = accessToken;

        // RFC 6749 §4.1.4: refresh_token is OPTIONAL in the token response.
        // Only update if the server returns one; preserve any existing value otherwise.
        QString newRefreshToken = obj.value("refresh_token").toString();
        if (!newRefreshToken.isEmpty()) {
            refreshToken_ = newRefreshToken;
        }

        qint64 expiresIn = static_cast<qint64>(obj.value("expires_in").toInt(0));
        expires_ = QDateTime::currentSecsSinceEpoch() + expiresIn;

        linked_ = true;

        saveTokens();

        obs_log(LOG_DEBUG, "OAuth2Client: Token exchange successful, expires in %lld seconds", expiresIn);
        // O2 compatibility: O2::onTokenReplyFinished() calls setLinked(true) (emits linkedChanged)
        // then emits linkingSucceeded(). Match that order.
        emit linkedChanged();
        emit linkingSucceeded();
    });
}

void OAuth2Client::saveTokens()
{
    if (!store_) {
        return;
    }

    // Use O2-compatible key format: "key.{clientId}" for seamless migration
    store_->setValue(QString("token.%1").arg(clientId_), token_);
    store_->setValue(QString("refreshtoken.%1").arg(clientId_), refreshToken_);
    store_->setValue(QString("expires.%1").arg(clientId_), QString::number(static_cast<qlonglong>(expires_)));
    store_->setValue(QString("linked.%1").arg(clientId_), linked_ ? "1" : "");
}

void OAuth2Client::loadTokens()
{
    if (!store_) {
        return;
    }

    // Use O2-compatible key format: "key.{clientId}" for seamless migration
    token_ = store_->value(QString("token.%1").arg(clientId_));
    refreshToken_ = store_->value(QString("refreshtoken.%1").arg(clientId_));
    expires_ = store_->value(QString("expires.%1").arg(clientId_), "0").toLongLong();
    linked_ = !store_->value(QString("linked.%1").arg(clientId_)).isEmpty();

    obs_log(LOG_DEBUG, "OAuth2Client: Loaded tokens from store, linked=%s", linked_ ? "true" : "false");
}
