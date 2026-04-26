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

#include <cstring>

#include <QJsonDocument>
#include <QJsonObject>
#include <QUrlQuery>
#include <QRandomGenerator>
#include <QDateTime>
#include <QPointer>
#include <QThread>

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
    // Disconnect all signal/slot connections to this object to prevent dangling
    // `this` access in lambdas queued via CurlHttpClient callbacks (R4-4).
    // Note: We intentionally do NOT call httpClient->cancelAll() here because
    // CurlHttpClient is a shared resource — canceling all requests would kill
    // unrelated API requests from other components (C-1).
    disconnect(this);
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

    // Fixed port (configured) or OS-assigned ephemeral port (port=0). The unpredictable
    // ephemeral port together with the state-CSRF check forms layered defense against a
    // local attacker grabbing the redirect endpoint.
    int requestedPort = localPort_ > 0 ? localPort_ : 0;
    if (!localServer->listen(requestedPort)) {
        if (localPort_ > 0) {
            obs_log(LOG_ERROR, "OAuth2Client: Configured port %d unavailable", localPort_);
        } else {
            obs_log(LOG_ERROR, "OAuth2Client: Failed to bind ephemeral port");
        }
        emit linkingFailed();
        return;
    }

    localServer->setReplyContent(replyContent_);

    // RFC 6749 §10.12: state must be unguessable. Use the system CSPRNG, not QUuid (which uses
    // the non-cryptographic global RNG on most platforms).
    constexpr int stateBytes = 32;
    static_assert(stateBytes % sizeof(quint32) == 0, "stateBytes must be a multiple of sizeof(quint32)");
    // Fill into an aligned local buffer, then memcpy — avoids strict-aliasing / alignment UB
    // from casting QByteArray::data() to quint32*.
    quint32 buf[stateBytes / sizeof(quint32)];
    QRandomGenerator::system()->fillRange(buf, stateBytes / sizeof(quint32));
    QByteArray stateRaw(stateBytes, Qt::Uninitialized);
    memcpy(stateRaw.data(), buf, stateBytes);
    pendingState_ = QString::fromLatin1(stateRaw.toHex());

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
    // FIXME: refresh() relies on the UI-thread reentrancy contract — concurrent callers
    // hooking refreshFinished before refresh() emits it. If this assert ever fires,
    // explicitly queue pending callers (separate PR) instead of leaning on signal timing.
    Q_ASSERT(thread() == QThread::currentThread());

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

    QPointer<OAuth2Client> self = this;
    httpClient->post(url, headers, body, [self](HttpResponse response) {
        if (!self) {
            obs_log(LOG_DEBUG, "OAuth2Client: Destroyed before refresh callback, ignoring response");
            return;
        }

        if (response.error != HttpError::NoError) {
            obs_log(LOG_ERROR, "OAuth2Client: Token refresh failed with error %d", static_cast<int>(response.error));
            // RFC 6749 §5.2: Parse error response fields for diagnostics
            QJsonDocument errDoc = QJsonDocument::fromJson(response.data);
            if (errDoc.isObject()) {
                QJsonObject errObj = errDoc.object();
                QString errCode = errObj.value("error").toString();
                QString errDesc = errObj.value("error_description").toString();
                if (!errCode.isEmpty()) {
                    obs_log(LOG_ERROR, "OAuth2Client: Server error: %s", qUtf8Printable(errCode));
                }
                if (!errDesc.isEmpty()) {
                    obs_log(LOG_ERROR, "OAuth2Client: Error description: %s", qUtf8Printable(errDesc));
                }
            }
            self->refreshing_ = false;
            // O2 compatibility: unlink() emits linkedChanged + linkingSucceeded (logout flow),
            // and refreshFinished is emitted last. HttpRequestInvoker relies on this ordering:
            // its 401-retry slot only needs the final refreshFinished error to abort the request.
            self->unlink();
            emit self->refreshFinished(HttpError::AuthenticationRequired);
            return;
        }

        QJsonDocument doc = QJsonDocument::fromJson(response.data);
        if (!doc.isObject()) {
            obs_log(LOG_ERROR, "OAuth2Client: Invalid JSON in refresh response");
            self->refreshing_ = false;
            // Same signal order as the error path above (linkedChanged + linkingSucceeded → refreshFinished).
            self->unlink();
            emit self->refreshFinished(HttpError::AuthenticationRequired);
            return;
        }

        QJsonObject obj = doc.object();
        QString accessToken = obj.value("access_token").toString();
        if (accessToken.isEmpty()) {
            obs_log(LOG_ERROR, "OAuth2Client: Server returned empty access_token on refresh");
            self->refreshing_ = false;
            // Same signal order as the error path above (linkedChanged + linkingSucceeded → refreshFinished).
            self->unlink();
            emit self->refreshFinished(HttpError::AuthenticationRequired);
            return;
        }

        self->token_ = accessToken;

        // Some providers return a new refresh token
        if (obj.contains("refresh_token")) {
            self->refreshToken_ = obj.value("refresh_token").toString();
        }

        qint64 expiresIn = static_cast<qint64>(obj.value("expires_in").toInt(0));
        if (expiresIn <= 0) {
            if (!obj.contains("expires_in")) {
                obs_log(LOG_WARNING, "OAuth2Client: Refresh response missing expires_in field");
            } else {
                obs_log(LOG_WARNING, "OAuth2Client: Refresh response has non-positive expires_in (%lld)", expiresIn);
            }
        }
        self->expires_ = QDateTime::currentSecsSinceEpoch() + expiresIn;

        self->saveTokens();

        self->refreshing_ = false;
        obs_log(LOG_DEBUG, "OAuth2Client: Token refreshed successfully, expires in %lld seconds", expiresIn);
        // O2 compatibility: O2::onRefreshFinished() emits linkedChanged,
        // linkingSucceeded, then refreshFinished in that order.
        emit self->linkedChanged();
        emit self->linkingSucceeded();
        emit self->refreshFinished(HttpError::NoError);
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

    QPointer<OAuth2Client> self = this;
    httpClient->post(tokenUrl_, headers, body, [self](HttpResponse response) {
        if (!self) {
            obs_log(LOG_DEBUG, "OAuth2Client: Destroyed before token exchange callback, ignoring response");
            return;
        }

        if (response.error != HttpError::NoError) {
            obs_log(LOG_ERROR, "OAuth2Client: Token exchange failed with error %d", static_cast<int>(response.error));
            // RFC 6749 §5.2: Parse error response fields for diagnostics
            QJsonDocument errDoc = QJsonDocument::fromJson(response.data);
            if (errDoc.isObject()) {
                QJsonObject errObj = errDoc.object();
                QString errCode = errObj.value("error").toString();
                QString errDesc = errObj.value("error_description").toString();
                if (!errCode.isEmpty()) {
                    obs_log(LOG_ERROR, "OAuth2Client: Server error: %s", qUtf8Printable(errCode));
                }
                if (!errDesc.isEmpty()) {
                    obs_log(LOG_ERROR, "OAuth2Client: Error description: %s", qUtf8Printable(errDesc));
                }
            }
            emit self->linkingFailed();
            return;
        }

        QJsonDocument doc = QJsonDocument::fromJson(response.data);
        if (!doc.isObject()) {
            obs_log(LOG_ERROR, "OAuth2Client: Invalid JSON in token response");
            emit self->linkingFailed();
            return;
        }

        QJsonObject obj = doc.object();
        QString accessToken = obj.value("access_token").toString();
        if (accessToken.isEmpty()) {
            obs_log(LOG_ERROR, "OAuth2Client: Server returned empty access_token");
            emit self->linkingFailed();
            return;
        }

        self->token_ = accessToken;

        // RFC 6749 §4.1.4: refresh_token is OPTIONAL in the token response.
        // Only update if the server returns one; preserve any existing value otherwise.
        QString newRefreshToken = obj.value("refresh_token").toString();
        if (!newRefreshToken.isEmpty()) {
            self->refreshToken_ = newRefreshToken;
        }

        qint64 expiresIn = static_cast<qint64>(obj.value("expires_in").toInt(0));
        if (expiresIn <= 0) {
            if (!obj.contains("expires_in")) {
                obs_log(LOG_WARNING, "OAuth2Client: Token response missing expires_in field");
            } else {
                obs_log(LOG_WARNING, "OAuth2Client: Token response has non-positive expires_in (%lld)", expiresIn);
            }
        }
        self->expires_ = QDateTime::currentSecsSinceEpoch() + expiresIn;

        self->linked_ = true;

        self->saveTokens();

        obs_log(LOG_DEBUG, "OAuth2Client: Token exchange successful, expires in %lld seconds", expiresIn);
        // O2 compatibility: O2::onTokenReplyFinished() calls setLinked(true) (emits linkedChanged)
        // then emits linkingSucceeded(). Match that order.
        emit self->linkedChanged();
        emit self->linkingSucceeded();
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
