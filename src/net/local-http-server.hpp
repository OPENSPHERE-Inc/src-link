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
#include <QByteArray>
#include <QMap>
#include <QString>
#include <QTimer>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET SocketHandle;
#define INVALID_SOCKET_HANDLE INVALID_SOCKET
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
typedef int SocketHandle;
#define INVALID_SOCKET_HANDLE (-1)
#endif

class LocalHttpServer : public QObject {
    Q_OBJECT

public:
    static constexpr int POLL_INTERVAL_MSECS = 100;
    static constexpr int TIMEOUT_MSECS = 120000;

    explicit LocalHttpServer(QObject *parent = nullptr);
    ~LocalHttpServer();

    /// Start listening on the specified port. Returns true on success.
    bool listen(int port);

    /// Stop listening and close the server socket.
    void close();

    /// Set the HTML content to serve as response
    void setReplyContent(const QByteArray &content);

    /// Check if currently listening
    bool isListening() const;

    /// Get the port being listened on
    int port() const;

signals:
    /// Emitted when OAuth2 callback is received with query parameters
    void verificationReceived(const QMap<QString, QString> &params);

private:
    SocketHandle serverSocket;
    int listenPort;
    QTimer *pollTimer;
    QTimer *timeoutTimer;
    QByteArray replyContent;
    bool listening;

    void pollAccept();
    void handleClient(SocketHandle clientSocket);
    QMap<QString, QString> parseQueryString(const QString &query);
    void closeSocket(SocketHandle sock);
    bool setNonBlocking(SocketHandle sock);
};
