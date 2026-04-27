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

#include <QElapsedTimer>
#include <QUrl>

#include <obs-module.h>

#include "local-http-server.hpp"
#include "../plugin-support.h"

#ifndef _WIN32
#include <errno.h>
#include <fcntl.h>
#endif

namespace {
// FIXME: handleClient() still blocks the UI thread up to RECV_DEADLINE_MSECS.
// Move recv() loop off the UI thread (QtConcurrent::run) or convert to fully
// non-blocking incremental reads driven by pollAccept(). Tracked for a follow-up PR.
//
// Absolute deadline for the recv() loop in handleClient(). Localhost-only OAuth2
// callback, so the wall-clock budget is intentionally tight.
constexpr int RECV_DEADLINE_MSECS = 2000;
// Per-recv() timeout used as a backstop against a peer that opens the connection
// and never sends data; the absolute deadline above bounds total time.
constexpr int RECV_PER_CALL_TIMEOUT_MSECS = 250;
} // namespace

LocalHttpServer::LocalHttpServer(QObject *parent)
    : QObject(parent),
      serverSocket(INVALID_SOCKET_HANDLE),
      listenPort(0),
      pollTimer(new QTimer(this)),
      timeoutTimer(new QTimer(this)),
      listening(false)
{
    obs_log(LOG_DEBUG, "LocalHttpServer created");

    pollTimer->setInterval(LocalHttpServer::POLL_INTERVAL_MSECS);
    connect(pollTimer, &QTimer::timeout, this, &LocalHttpServer::pollAccept);

    timeoutTimer->setSingleShot(true);
    timeoutTimer->setInterval(LocalHttpServer::TIMEOUT_MSECS);
    connect(timeoutTimer, &QTimer::timeout, this, [this]() {
        if (!listening) {
            return; // Already closed — avoid double verificationReceived from queued pollTimer signals
        }
        obs_log(
            LOG_WARNING, "LocalHttpServer: Auth flow timed out after %d seconds", LocalHttpServer::TIMEOUT_MSECS / 1000
        );
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
    if (setsockopt(
            serverSocket, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char *>(&optval), sizeof(optval)
        ) != 0) {
        obs_log(LOG_WARNING, "LocalHttpServer: setsockopt(SO_EXCLUSIVEADDRUSE) failed with error %d", WSAGetLastError());
    }
#else
    // Allow address reuse for quick restart after close
    if (setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char *>(&optval), sizeof(optval)) !=
        0) {
        obs_log(LOG_WARNING, "LocalHttpServer: setsockopt(SO_REUSEADDR) failed with errno %d", errno);
    }
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

    if (::listen(serverSocket, 5) != 0) {
        obs_log(LOG_ERROR, "LocalHttpServer: Failed to listen on port %d", port);
        closeSocket(serverSocket);
        serverSocket = INVALID_SOCKET_HANDLE;
        return false;
    }

    // Resolve the actual bound port (port==0 requests an OS-assigned ephemeral port).
    struct sockaddr_in boundAddr = {};
    socklen_t boundLen = sizeof(boundAddr);
    if (getsockname(serverSocket, reinterpret_cast<struct sockaddr *>(&boundAddr), &boundLen) != 0) {
        obs_log(LOG_ERROR, "LocalHttpServer: getsockname() failed");
        closeSocket(serverSocket);
        serverSocket = INVALID_SOCKET_HANDLE;
        return false;
    }
    listenPort = ntohs(boundAddr.sin_port);

    listening = true;
    pollTimer->start();
    timeoutTimer->start();

    obs_log(LOG_DEBUG, "LocalHttpServer: Listening on 127.0.0.1:%d", listenPort);
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
        // Defense in depth: bind() restricts to loopback, but reject any peer that is
        // not 127.0.0.1 in case a future socket option change widens the listening scope.
        if (clientAddr.sin_family != AF_INET || clientAddr.sin_addr.s_addr != htonl(INADDR_LOOPBACK)) {
            obs_log(LOG_WARNING, "LocalHttpServer: Rejecting non-loopback connection");
            closeSocket(client);
            return;
        }
        handleClient(client);
    }
}

void LocalHttpServer::handleClient(SocketHandle clientSocket)
{
    // Read the HTTP request
    QByteArray requestData;
    static constexpr int MAX_REQUEST_HEADER_SIZE = 8192;
    char buffer[MAX_REQUEST_HEADER_SIZE];

    // Switch to blocking mode with a per-recv() timeout. The absolute deadline
    // below bounds total wall-clock time spent in this function.
#ifdef _WIN32
    unsigned long blockingMode = 0;
    if (ioctlsocket(clientSocket, FIONBIO, &blockingMode) != 0) {
        obs_log(
            LOG_WARNING, "LocalHttpServer: ioctlsocket(FIONBIO) failed with error %d; aborting client",
            WSAGetLastError()
        );
        closeSocket(clientSocket);
        return;
    }
    DWORD rcvTimeout = RECV_PER_CALL_TIMEOUT_MSECS;
    if (setsockopt(
            clientSocket, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char *>(&rcvTimeout), sizeof(rcvTimeout)
        ) != 0) {
        obs_log(LOG_WARNING, "LocalHttpServer: setsockopt(SO_RCVTIMEO) failed with error %d", WSAGetLastError());
    }
#else
    int flags = fcntl(clientSocket, F_GETFL, 0);
    if (flags == -1 || fcntl(clientSocket, F_SETFL, flags & ~O_NONBLOCK) == -1) {
        obs_log(LOG_WARNING, "LocalHttpServer: fcntl(F_SETFL) failed with errno %d; aborting client", errno);
        closeSocket(clientSocket);
        return;
    }
    struct timeval rcvTimeout = {RECV_PER_CALL_TIMEOUT_MSECS / 1000, (RECV_PER_CALL_TIMEOUT_MSECS % 1000) * 1000};
    if (setsockopt(clientSocket, SOL_SOCKET, SO_RCVTIMEO, &rcvTimeout, sizeof(rcvTimeout)) != 0) {
        obs_log(LOG_WARNING, "LocalHttpServer: setsockopt(SO_RCVTIMEO) failed with errno %d", errno);
    }
#endif

    // Read until HTTP header terminator (\r\n\r\n) is found, bounded by an
    // absolute deadline. TCP may split the request across multiple segments.
    QElapsedTimer deadlineTimer;
    deadlineTimer.start();
    bool deadlineExceeded = false;
    while (requestData.indexOf("\r\n\r\n") == -1 && requestData.size() < static_cast<int>(sizeof(buffer)) - 1) {
        if (deadlineTimer.elapsed() >= RECV_DEADLINE_MSECS) {
            deadlineExceeded = true;
            obs_log(LOG_WARNING, "LocalHttpServer: recv() loop exceeded %d ms deadline", RECV_DEADLINE_MSECS);
            break;
        }
        const size_t remaining = sizeof(buffer) - 1 - static_cast<size_t>(requestData.size());
        int bytesRead = recv(clientSocket, buffer, static_cast<int>(remaining), 0);
        if (bytesRead < 0) {
#ifdef _WIN32
            int err = WSAGetLastError();
            if (err == WSAETIMEDOUT) {
                // Per-call timeout — let the absolute deadline decide whether to keep going.
                continue;
            }
            obs_log(LOG_WARNING, "LocalHttpServer: recv() failed with error %d", err);
#else
            int err = errno;
            if (err == EAGAIN || err == EWOULDBLOCK || err == EINTR) {
                // Per-call timeout / interrupted — let the absolute deadline decide.
                continue;
            }
            obs_log(LOG_WARNING, "LocalHttpServer: recv() failed with errno %d", err);
#endif
            break;
        }
        if (bytesRead == 0) {
            // Client closed connection before sending complete headers
            break;
        }
        requestData.append(buffer, bytesRead);
    }

    // Parse the first line: METHOD /path?query HTTP/1.1
    QString request = QString::fromUtf8(requestData);
    QString firstLine = request.section("\r\n", 0, 0);
    QStringList parts = firstLine.split(' ');

    bool methodIsGet = parts.size() >= 1 && parts[0] == "GET";

    QMap<QString, QString> params;
    if (methodIsGet && parts.size() >= 2) {
        QString path = parts[1];
        int queryIndex = path.indexOf('?');
        if (queryIndex >= 0) {
            QString queryString = path.mid(queryIndex + 1);
            params = parseQueryString(queryString);
        }
    }

    // Build HTTP response based on request validity.
    QByteArray response;
    if (deadlineExceeded) {
        static const QByteArray body = QByteArrayLiteral("Request Timeout");
        response = "HTTP/1.1 408 Request Timeout\r\n"
                   "Content-Type: text/plain; charset=utf-8\r\n"
                   "Content-Length: " +
                   QByteArray::number(body.size()) +
                   "\r\n"
                   "Connection: close\r\n"
                   "\r\n" +
                   body;
    } else if (!methodIsGet) {
        static const QByteArray body = QByteArrayLiteral("Method Not Allowed");
        response = "HTTP/1.1 405 Method Not Allowed\r\n"
                   "Allow: GET\r\n"
                   "Content-Type: text/plain; charset=utf-8\r\n"
                   "Content-Length: " +
                   QByteArray::number(body.size()) +
                   "\r\n"
                   "Connection: close\r\n"
                   "\r\n" +
                   body;
    } else {
        const QByteArray &responseBody = replyContent;
        response = "HTTP/1.1 200 OK\r\n"
                   "Content-Type: text/html; charset=utf-8\r\n"
                   "Content-Length: " +
                   QByteArray::number(responseBody.size()) +
                   "\r\n"
                   "Connection: close\r\n"
                   "\r\n" +
                   responseBody;
    }

    int sendResult = send(clientSocket, response.constData(), static_cast<int>(response.size()), 0);
    if (sendResult < 0) {
#ifdef _WIN32
        obs_log(LOG_WARNING, "LocalHttpServer: send() failed with error %d", WSAGetLastError());
#else
        obs_log(LOG_WARNING, "LocalHttpServer: send() failed with errno %d", errno);
#endif
    }
#ifdef _WIN32
    shutdown(clientSocket, SD_SEND);
#else
    shutdown(clientSocket, SHUT_WR);
#endif
    closeSocket(clientSocket);

    if (deadlineExceeded || !methodIsGet) {
        // Do not emit verificationReceived for invalid requests; let timeoutTimer
        // fire eventually or wait for the real callback.
        return;
    }

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
