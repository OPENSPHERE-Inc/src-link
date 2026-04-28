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

#include <obs-module.h>

#include "curl-http-client.hpp"
#include "linux-ca-locations.hpp"
#include "../plugin-support.h"

CurlHttpClient::CurlHttpClient(QObject *parent) : QObject(parent), multi(curl_multi_init()), pollTimer(new QTimer(this))
{
    obs_log(LOG_DEBUG, "CurlHttpClient created");

    if (!multi) {
        obs_log(LOG_ERROR, "CurlHttpClient: curl_multi_init() failed");
    }

    pollTimer->setInterval(CurlHttpClient::POLL_INTERVAL_MSECS);
    connect(pollTimer, &QTimer::timeout, this, &CurlHttpClient::checkCompleted);
}

CurlHttpClient::~CurlHttpClient()
{
    obs_log(LOG_DEBUG, "CurlHttpClient destroying");

    disconnect(pollTimer, nullptr, this, nullptr);
    pollTimer->stop();
    // Callers (e.g. HttpRequestInvoker) may already be partially destroyed during shutdown,
    // so dispatching a "canceled" callback here can dereference dangling captures.
    cancelAllSilently();
    curl_multi_cleanup(multi);
}

CURL *CurlHttpClient::createEasyHandle(
    const QUrl &url, const QMap<QByteArray, QByteArray> &headers, RequestContext *ctx, int timeoutMs
)
{
    CURL *easy = curl_easy_init();
    if (!easy) {
        return nullptr;
    }

    QByteArray urlBytes = url.toEncoded();
    curl_easy_setopt(easy, CURLOPT_URL, urlBytes.constData());

    curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(easy, CURLOPT_WRITEDATA, ctx);
    curl_easy_setopt(easy, CURLOPT_HEADERFUNCTION, headerCallback);
    curl_easy_setopt(easy, CURLOPT_HEADERDATA, ctx);

    struct curl_slist *headerList = nullptr;
    for (auto it = headers.constBegin(); it != headers.constEnd(); ++it) {
        QByteArray headerLine = it.key() + ": " + it.value();
        struct curl_slist *next = curl_slist_append(headerList, headerLine.constData());
        if (!next) {
            obs_log(LOG_WARNING, "CurlHttpClient: curl_slist_append failed for header '%s'", it.key().constData());
            break;
        }
        headerList = next;
    }
    ctx->requestHeaders = headerList;
    if (headerList) {
        curl_easy_setopt(easy, CURLOPT_HTTPHEADER, headerList);
    }

    curl_easy_setopt(easy, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(easy, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(easy, CURLOPT_SSLVERSION, CURL_SSLVERSION_TLSv1_2 | CURL_SSLVERSION_MAX_TLSv1_3);
    // Do not follow redirects: libcurl would replay the caller-supplied Authorization
    // header on the redirect target, leaking the OAuth2 Bearer token to a different host.
    // SRC-Link API endpoints are not expected to return 30x; any future endpoint that
    // needs redirects must handle them at the caller level with explicit host allowlisting.
    curl_easy_setopt(easy, CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(easy, CURLOPT_NOSIGNAL, 1L);
    // Cap connect phase separately so a hung TCP SYN-ACK cannot consume the full
    // CURLOPT_TIMEOUT_MS budget meant for the request.
    // FIXME: migrate the polling driver to curl_multi_socket_action + QSocketNotifier
    // so we stop waking the UI thread at POLL_INTERVAL_MSECS while requests are in-flight.
    curl_easy_setopt(easy, CURLOPT_CONNECTTIMEOUT_MS, 5000L);

#if defined(_WIN32) || defined(__APPLE__)
    // Use OS native CA store (Windows: Schannel, macOS: Secure Transport)
    curl_easy_setopt(easy, CURLOPT_SSL_OPTIONS, CURLSSLOPT_NATIVE_CA);
#elif defined(__linux__)
    // Linux: libcurl is built against mbedtls, which has no compiled-in CA path. The CA path
    // is supplied at build time via the SRC_LINK_LINUX_CA_PATH CMake cache variable, and the
    // helper picks CURLOPT_CAPATH or CURLOPT_CAINFO based on whether the path is a directory
    // or a regular file.
    if (auto *ca = findLinuxCaLocation()) {
        if (ca->isDirectory) {
            curl_easy_setopt(easy, CURLOPT_CAPATH, ca->path);
        } else {
            curl_easy_setopt(easy, CURLOPT_CAINFO, ca->path);
        }
    } else {
        obs_log(LOG_WARNING, "CurlHttpClient: No system CA bundle found; TLS verification will likely fail");
    }
#endif

    curl_easy_setopt(easy, CURLOPT_TIMEOUT_MS, static_cast<long>(timeoutMs));
    curl_easy_setopt(easy, CURLOPT_TCP_KEEPALIVE, 1L);
    curl_easy_setopt(easy, CURLOPT_TCP_KEEPIDLE, 60L);
    curl_easy_setopt(easy, CURLOPT_TCP_KEEPINTVL, 30L);
    curl_easy_setopt(easy, CURLOPT_PRIVATE, ctx);
    ctx->easy = easy;

    return easy;
}

void CurlHttpClient::startRequest(CURL *easy, RequestContext *ctx)
{
    if (!multi) {
        ctx->callback(HttpResponse{0, {}, HttpError::NetworkError});
        cleanupRequest(ctx);
        return;
    }
    curl_multi_add_handle(multi, easy);
    activeRequests.append(ctx);

    if (!pollTimer->isActive()) {
        pollTimer->start();
    }

    int stillRunning = 0;
    curl_multi_perform(multi, &stillRunning);
}

void CurlHttpClient::get(
    const QUrl &url, const QMap<QByteArray, QByteArray> &headers, ResponseCallback callback, int timeoutMs
)
{
    auto *ctx = new RequestContext{};
    ctx->callback = std::move(callback);
    CURL *easy = createEasyHandle(url, headers, ctx, timeoutMs);
    if (!easy) {
        ctx->callback(HttpResponse{0, {}, HttpError::NetworkError});
        delete ctx;
        return;
    }
    startRequest(easy, ctx);
}

void CurlHttpClient::post(
    const QUrl &url, const QMap<QByteArray, QByteArray> &headers, const QByteArray &body, ResponseCallback callback,
    int timeoutMs
)
{
    auto *ctx = new RequestContext{};
    ctx->callback = std::move(callback);
    ctx->requestBody = body;
    CURL *easy = createEasyHandle(url, headers, ctx, timeoutMs);
    if (!easy) {
        ctx->callback(HttpResponse{0, {}, HttpError::NetworkError});
        delete ctx;
        return;
    }
    curl_easy_setopt(easy, CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(ctx->requestBody.size()));
    curl_easy_setopt(easy, CURLOPT_COPYPOSTFIELDS, ctx->requestBody.constData());
    startRequest(easy, ctx);
}

void CurlHttpClient::put(
    const QUrl &url, const QMap<QByteArray, QByteArray> &headers, const QByteArray &body, ResponseCallback callback,
    int timeoutMs
)
{
    auto *ctx = new RequestContext{};
    ctx->callback = std::move(callback);
    ctx->requestBody = body;
    CURL *easy = createEasyHandle(url, headers, ctx, timeoutMs);
    if (!easy) {
        ctx->callback(HttpResponse{0, {}, HttpError::NetworkError});
        delete ctx;
        return;
    }
    curl_easy_setopt(easy, CURLOPT_CUSTOMREQUEST, "PUT");
    curl_easy_setopt(easy, CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(ctx->requestBody.size()));
    curl_easy_setopt(easy, CURLOPT_COPYPOSTFIELDS, ctx->requestBody.constData());
    startRequest(easy, ctx);
}

void CurlHttpClient::del(
    const QUrl &url, const QMap<QByteArray, QByteArray> &headers, ResponseCallback callback, int timeoutMs
)
{
    auto *ctx = new RequestContext{};
    ctx->callback = std::move(callback);
    CURL *easy = createEasyHandle(url, headers, ctx, timeoutMs);
    if (!easy) {
        ctx->callback(HttpResponse{0, {}, HttpError::NetworkError});
        delete ctx;
        return;
    }
    curl_easy_setopt(easy, CURLOPT_CUSTOMREQUEST, "DELETE");
    startRequest(easy, ctx);
}

void CurlHttpClient::head(
    const QUrl &url, const QMap<QByteArray, QByteArray> &headers, ResponseCallback callback, int timeoutMs
)
{
    auto *ctx = new RequestContext{};
    ctx->callback = std::move(callback);
    CURL *easy = createEasyHandle(url, headers, ctx, timeoutMs);
    if (!easy) {
        ctx->callback(HttpResponse{0, {}, HttpError::NetworkError});
        delete ctx;
        return;
    }
    curl_easy_setopt(easy, CURLOPT_NOBODY, 1L);
    startRequest(easy, ctx);
}

void CurlHttpClient::cancelAll()
{
    // Snapshot the list before invoking callbacks: a callback may re-enter and append
    // a new request to activeRequests, and we must not cancel/cleanup that one here.
    auto pending = std::move(activeRequests);
    activeRequests.clear();

    for (auto *ctx : pending) {
        curl_multi_remove_handle(multi, ctx->easy);
        // Invoke callback with OperationCanceled so downstream consumers
        // (HttpRequestInvoker) can emit finished and clean up properly.
        ctx->callback(HttpResponse{0, {}, HttpError::OperationCanceled});
        cleanupRequest(ctx);
    }
}

void CurlHttpClient::cancelAllSilently()
{
    auto pending = std::move(activeRequests);
    activeRequests.clear();

    for (auto *ctx : pending) {
        curl_multi_remove_handle(multi, ctx->easy);
        cleanupRequest(ctx);
    }
}

void CurlHttpClient::checkCompleted()
{
    int stillRunning = 0;
    curl_multi_perform(multi, &stillRunning);

    int msgsInQueue = 0;
    CURLMsg *msg = nullptr;
    while ((msg = curl_multi_info_read(multi, &msgsInQueue)) != nullptr) {
        if (msg->msg != CURLMSG_DONE) {
            continue;
        }

        CURL *easy = msg->easy_handle;
        CURLcode result = msg->data.result;

        RequestContext *ctx = nullptr;
        curl_easy_getinfo(easy, CURLINFO_PRIVATE, &ctx);
        if (!ctx) {
            continue;
        }

        long statusCode = 0;
        curl_easy_getinfo(easy, CURLINFO_RESPONSE_CODE, &statusCode);

        HttpError error;
        if (ctx->responseTooLarge) {
            error = HttpError::ResponseTooLarge;
        } else if (result != CURLE_OK) {
            error = httpErrorFromCurlCode(static_cast<int>(result));
        } else {
            error = httpErrorFromStatusCode(static_cast<int>(statusCode));
        }

        HttpResponse response{static_cast<int>(statusCode), ctx->responseData, error, ctx->responseHeaders};

        // Detach ctx from activeRequests BEFORE invoking the callback so that any
        // re-entrant call (e.g. callback issuing a new request) cannot observe a stale
        // entry, and so cleanupRequest never runs against a ctx the user resurrected.
        curl_multi_remove_handle(multi, easy);
        activeRequests.removeOne(ctx);

        ctx->callback(response);
        cleanupRequest(ctx);
    }

    if (activeRequests.isEmpty() && stillRunning == 0) {
        pollTimer->stop();
    }
}

void CurlHttpClient::cleanupRequest(RequestContext *ctx)
{
    curl_easy_cleanup(ctx->easy);
    if (ctx->requestHeaders) {
        curl_slist_free_all(ctx->requestHeaders);
    }
    delete ctx;
}

size_t CurlHttpClient::writeCallback(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    auto *ctx = static_cast<RequestContext *>(userdata);
    if (nmemb && size > SIZE_MAX / nmemb) {
        ctx->responseTooLarge = true;
        return 0;
    }
    size_t totalSize = size * nmemb;
    if (ctx->responseData.size() + static_cast<qsizetype>(totalSize) > CurlHttpClient::MAX_RESPONSE_SIZE) {
        obs_log(
            LOG_WARNING, "CurlHttpClient: Response exceeded %d bytes limit, aborting", CurlHttpClient::MAX_RESPONSE_SIZE
        );
        // Returning a short count makes libcurl fail the transfer with CURLE_WRITE_ERROR.
        // Flag it here so checkCompleted() can surface ResponseTooLarge instead of a
        // generic write/network error.
        ctx->responseTooLarge = true;
        return 0;
    }
    ctx->responseData.append(ptr, static_cast<qsizetype>(totalSize));
    return totalSize;
}

size_t CurlHttpClient::headerCallback(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    auto *ctx = static_cast<RequestContext *>(userdata);
    if (nmemb && size > SIZE_MAX / nmemb) {
        return 0;
    }
    size_t totalSize = size * nmemb;
    QByteArray line(ptr, static_cast<qsizetype>(totalSize));
    int colonIndex = line.indexOf(':');
    if (colonIndex > 0) {
        // RFC 7230: HTTP header names are case-insensitive — normalize to lowercase
        QByteArray key = line.left(colonIndex).trimmed().toLower();
        QByteArray value = line.mid(colonIndex + 1).trimmed();
        // FIXME: duplicate-name headers (e.g. Set-Cookie) are overwritten; switch to QMultiMap if needed.
        ctx->responseHeaders.insert(key, value);
    }
    return totalSize;
}
