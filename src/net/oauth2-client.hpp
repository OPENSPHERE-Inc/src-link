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

#pragma once

#include <QObject>
#include <QString>
#include <QUrl>
#include <QTimer>

#include "http-error.hpp"

class CurlHttpClient;
class LocalHttpServer;

/// Token store interface — implemented by SRCLinkSettingsStore
class OAuth2TokenStore {
public:
    virtual ~OAuth2TokenStore() = default;
    virtual QString value(const QString &key, const QString &defaultValue = QString()) = 0;
    virtual void setValue(const QString &key, const QString &value) = 0;
};

class OAuth2Client : public QObject {
    Q_OBJECT

public:
    explicit OAuth2Client(CurlHttpClient *httpClient, QObject *parent = nullptr);
    ~OAuth2Client();

    // Configuration (call before link())
    void setClientId(const QString &clientId);
    void setClientSecret(const QString &clientSecret);
    void setScope(const QString &scope);
    void setRequestUrl(const QString &url);
    void setTokenUrl(const QString &url);
    void setRefreshTokenUrl(const QString &url);
    void setLocalhostPolicy(const QString &policy);
    void setLocalPort(int port);
    void setReplyContent(const QByteArray &content);
    void setStore(OAuth2TokenStore *store);

    // State accessors
    bool linked() const;
    QString token() const;
    qint64 expires() const;

signals:
    void openBrowser(const QUrl &url);
    void closeBrowser();
    void linkingSucceeded();
    void linkingFailed();
    void linkedChanged();
    void refreshFinished(HttpError error);

public slots:
    /// Start OAuth2 Authorization Code flow
    void link();

    /// Clear tokens and set linked = false
    void unlink();

    /// Refresh the access token
    void refresh();

public:
    // Token accessors for HttpRequestInvoker
    inline const QString &accessToken() const { return token_; }
    inline const QString &refreshTokenValue() const { return refreshToken_; }

private:
    CurlHttpClient *httpClient;
    LocalHttpServer *localServer;

    // Configuration
    QString clientId_;
    QString clientSecret_;
    QString scope_;
    QUrl requestUrl_;
    QUrl tokenUrl_;
    QUrl refreshTokenUrl_;
    QString localhostPolicy_;
    int localPort_;
    QByteArray replyContent_;
    OAuth2TokenStore *store_;

    // State
    bool linked_;
    bool refreshing_; // Re-entrance guard for refresh()
    QString token_;
    QString refreshToken_;
    qint64 expires_;
    QString pendingState_;
    QString redirectUri_; // Saved at link() time for use in token exchange

    void exchangeCodeForToken(const QString &code);
    void saveTokens();
    void loadTokens();

private slots:
    void onVerificationReceived(const QMap<QString, QString> &params);
};
