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

#include <QUrl>

#include <obs-module.h>

#include "local-http-server.hpp"
#include "../plugin-support.h"

#ifndef _WIN32
#include <fcntl.h>
#endif

LocalHttpServer::LocalHttpServer(QObject *parent)
    : QObject(parent),
      serverSocket(INVALID_SOCKET_HANDLE),
      listenPort(0),
      pollTimer(new QTimer(this)),
      timeoutTimer(new QTimer(this)),
      listening(false)
{
    obs_log(LOG_DEBUG, "LocalHttpServer created");

    pollTimer->setInterval(LOCAL_SERVER_POLL_INTERVAL_MSECS);
    connect(pollTimer, &QTimer::timeout, this, &LocalHttpServer::pollAccept);

    timeoutTimer->setSingleShot(true);
    timeoutTimer->setInterval(LOCAL_SERVER_TIMEOUT_MSECS);
    connect(timeoutTimer, &QTimer::timeout, this, [this]() {
        if (!listening) {
            return; // Already closed — avoid double verificationReceived from queued pollTimer signals
        }
        obs_log(LOG_WARNING, "LocalHttpServer: Auth flow timed out after %d seconds", LOCAL_SERVER_TIMEOUT_MSECS / 1000);
        close();
        // Emit empty params to signal timeout — OAuth2Client will detect missing "code" and emit linkingFailed
        emit verificationReceived({});
    });
}

LocalHttpServer::~LocalHttpServer()
{
    obs_log(LOG_DEBUG, "LocalHttpServer destroying");
    close();
}

bool LocalHttpServer::listen(int port)
{
    if (listening) {
        close();
    }

    serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket == INVALID_SOCKET_HANDLE) {
        obs_log(LOG_ERROR, "LocalHttpServer: Failed to create socket");
        return false;
    }

    int optval = 1;
#ifdef _WIN32
    // Prevent other processes from binding to the same port (OAuth2 callback hijacking)
    setsockopt(serverSocket, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char *>(&optval), sizeof(optval));
#else
    // Allow address reuse for quick restart after close
    setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char *>(&optval), sizeof(optval));
#endif

    if (!setNonBlocking(serverSocket)) {
        obs_log(LOG_ERROR, "LocalHttpServer: Failed to set non-blocking mode");
        closeSocket(serverSocket);
        serverSocket = INVALID_SOCKET_HANDLE;
        return false;
    }

    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(static_cast<uint16_t>(port));

    if (bind(serverSocket, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr)) != 0) {
        obs_log(LOG_ERROR, "LocalHttpServer: Failed to bind to port %d", port);
        closeSocket(serverSocket);
        serverSocket = INVALID_SOCKET_HANDLE;
        return false;
    }

    if (::listen(serverSocket, 1) != 0) {
        obs_log(LOG_ERROR, "LocalHttpServer: Failed to listen on port %d", port);
        closeSocket(serverSocket);
        serverSocket = INVALID_SOCKET_HANDLE;
        return false;
    }

    listenPort = port;
    listening = true;
    pollTimer->start();
    timeoutTimer->start();

    obs_log(LOG_DEBUG, "LocalHttpServer: Listening on 127.0.0.1:%d", port);
    return true;
}

void LocalHttpServer::close()
{
    pollTimer->stop();
    timeoutTimer->stop();

    if (serverSocket != INVALID_SOCKET_HANDLE) {
        closeSocket(serverSocket);
        serverSocket = INVALID_SOCKET_HANDLE;
    }

    listening = false;
}

void LocalHttpServer::setReplyContent(const QByteArray &content)
{
    replyContent = content;
}

bool LocalHttpServer::isListening() const
{
    return listening;
}

int LocalHttpServer::port() const
{
    return listenPort;
}

void LocalHttpServer::pollAccept()
{
    if (!listening) {
        return;
    }

    struct sockaddr_in clientAddr = {};
    socklen_t clientAddrLen = sizeof(clientAddr);
    SocketHandle client = accept(serverSocket, reinterpret_cast<struct sockaddr *>(&clientAddr), &clientAddrLen);

    if (client != INVALID_SOCKET_HANDLE) {
        handleClient(client);
    }
}

void LocalHttpServer::handleClient(SocketHandle clientSocket)
{
    // Read the HTTP request
    QByteArray requestData;
    char buffer[4096];

    // Set client socket to blocking with a 5-second receive timeout
    // to prevent UI thread blocking from slow or malicious clients
#ifdef _WIN32
    unsigned long blockingMode = 0;
    ioctlsocket(clientSocket, FIONBIO, &blockingMode);
    DWORD rcvTimeout = 5000;
    setsockopt(clientSocket, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char *>(&rcvTimeout), sizeof(rcvTimeout));
#else
    int flags = fcntl(clientSocket, F_GETFL, 0);
    fcntl(clientSocket, F_SETFL, flags & ~O_NONBLOCK);
    struct timeval rcvTimeout = {5, 0};
    setsockopt(clientSocket, SOL_SOCKET, SO_RCVTIMEO, &rcvTimeout, sizeof(rcvTimeout));
#endif

    // Read until HTTP header terminator (\r\n\r\n) is found.
    // TCP may split the request across multiple segments.
    while (requestData.indexOf("\r\n\r\n") == -1 && requestData.size() < static_cast<int>(sizeof(buffer)) - 1) {
        int bytesRead = recv(clientSocket, buffer, sizeof(buffer) - 1 - requestData.size(), 0);
        if (bytesRead <= 0) {
            break;
        }
        requestData.append(buffer, bytesRead);
    }

    // Parse the first line: GET /path?query HTTP/1.1
    QString request = QString::fromUtf8(requestData);
    QString firstLine = request.section("\r\n", 0, 0);
    QStringList parts = firstLine.split(' ');

    QMap<QString, QString> params;
    if (parts.size() >= 2) {
        QString path = parts[1];
        int queryIndex = path.indexOf('?');
        if (queryIndex >= 0) {
            QString queryString = path.mid(queryIndex + 1);
            params = parseQueryString(queryString);
        }
    }

    // Send HTTP response
    QByteArray responseBody = replyContent;
    QByteArray response = "HTTP/1.1 200 OK\r\n"
                          "Content-Type: text/html; charset=utf-8\r\n"
                          "Content-Length: " +
                          QByteArray::number(responseBody.size()) +
                          "\r\n"
                          "Connection: close\r\n"
                          "\r\n" +
                          responseBody;

    send(clientSocket, response.constData(), static_cast<int>(response.size()), 0);
    closeSocket(clientSocket);

    obs_log(LOG_DEBUG, "LocalHttpServer: Received OAuth2 callback with %d parameters", params.size());
    emit verificationReceived(params);
}

QMap<QString, QString> LocalHttpServer::parseQueryString(const QString &query)
{
    QMap<QString, QString> params;
    QStringList pairs = query.split('&', Qt::SkipEmptyParts);

    for (const QString &pair : pairs) {
        int eqIndex = pair.indexOf('=');
        if (eqIndex >= 0) {
            QString key = QUrl::fromPercentEncoding(pair.left(eqIndex).toUtf8());
            QString value = QUrl::fromPercentEncoding(pair.mid(eqIndex + 1).toUtf8());
            params.insert(key, value);
        } else {
            QString key = QUrl::fromPercentEncoding(pair.toUtf8());
            params.insert(key, QString());
        }
    }

    return params;
}

void LocalHttpServer::closeSocket(SocketHandle sock)
{
#ifdef _WIN32
    closesocket(sock);
#else
    ::close(sock);
#endif
}

bool LocalHttpServer::setNonBlocking(SocketHandle sock)
{
#ifdef _WIN32
    unsigned long mode = 1;
    return ioctlsocket(sock, FIONBIO, &mode) == 0;
#else
    int flags = fcntl(sock, F_GETFL, 0);
    if (flags == -1) {
        return false;
    }
    return fcntl(sock, F_SETFL, flags | O_NONBLOCK) != -1;
#endif
}
