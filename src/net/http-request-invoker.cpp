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

#include "http-request-invoker.hpp"
#include "curl-http-client.hpp"
#include "oauth2-client.hpp"
#include "http-response.hpp"
#include "../plugin-support.h"

//#define LOG_REQUEST_QUEUE_TRACE

#ifdef LOG_REQUEST_QUEUE_TRACE
#define TRACE(...) obs_log(LOG_DEBUG, __VA_ARGS__)
#else
#define TRACE(...)
#endif

//--- HttpRequestSequencer class ---//

HttpRequestSequencer::HttpRequestSequencer(
    CurlHttpClient *_httpClient, OAuth2Client *_oauth2Client, QObject *parent
)
    : QObject(parent),
      httpClient(_httpClient),
      oauth2Client(_oauth2Client)
{
    TRACE("HttpRequestSequencer created");
}

HttpRequestSequencer::~HttpRequestSequencer()
{
    if (!requestQueue.isEmpty()) {
        obs_log(LOG_WARNING, "Draining %d remaining requests in queue.", requestQueue.size());
        // Disconnect inter-invoker chain connections first to prevent
        // cascading lambda invocations during the drain loop (R4-2).
        for (auto *invoker : requestQueue) {
            invoker->disconnect();
        }
        for (auto *invoker : requestQueue) {
            emit invoker->finished(HttpError::OperationCanceled, {});
            delete invoker; // Direct delete: deleteLater() won't run if event loop is stopped (OBS shutdown)
        }
        requestQueue.clear();
    }

    TRACE("HttpRequestSequencer destroyed");
}

//--- HttpRequestInvoker class ---//

HttpRequestInvoker::HttpRequestInvoker(HttpRequestSequencer *_sequencer, QObject *parent)
    : QObject(parent),
      sequencer(_sequencer),
      retried(false)
{
    TRACE("HttpRequestInvoker created (Sequential)");
}

HttpRequestInvoker::HttpRequestInvoker(CurlHttpClient *httpClient, OAuth2Client *oauth2Client, QObject *parent)
    : QObject(parent),
      sequencer(nullptr),
      retried(false)
{
    sequencer = new HttpRequestSequencer(httpClient, oauth2Client, this);
    TRACE("HttpRequestInvoker created (Parallel)");
}

HttpRequestInvoker::~HttpRequestInvoker()
{
    disconnect(this);
    TRACE("HttpRequestInvoker destroyed");
}

template<class Func> void HttpRequestInvoker::queue(Func invoker)
{
    QMutexLocker locker(&sequencer->mutex);
    {
        if (sequencer->requestQueue.isEmpty()) {
            sequencer->requestQueue.append(this);
            locker.unlock(); // Must unlock before invoking
            invoker();
            return;
        } else {
            // Reserve next invocation. Use 4-arg connect with `this` as context so that
            // Qt auto-disconnects if this invoker is destroyed before the previous one finishes.
            connect(sequencer->requestQueue.last(), &HttpRequestInvoker::finished, this, invoker);
            sequencer->requestQueue.append(this);
        }
        TRACE("Queue request: size=%d", sequencer->requestQueue.size());
    }
    locker.unlock();
}

QMap<QByteArray, QByteArray> HttpRequestInvoker::mergeAuthHeaders(const QMap<QByteArray, QByteArray> &headers)
{
    QMap<QByteArray, QByteArray> merged = headers;
    // Only inject Bearer token if the caller did not explicitly provide an Authorization header
    if (!merged.contains("Authorization") && sequencer->oauth2Client &&
        !sequencer->oauth2Client->accessToken().isEmpty()) {
        merged["Authorization"] = QString("Bearer %1").arg(sequencer->oauth2Client->accessToken()).toUtf8();
    }
    return merged;
}

void HttpRequestInvoker::handleResponse(
    int statusCode, HttpError error, const QByteArray &data, std::function<void()> retryFunc
)
{
    // 401 auto-retry: refresh token and retry once
    if (statusCode == 401 && !retried && sequencer->oauth2Client) {
        retried = true;
        connect(
            sequencer->oauth2Client, &OAuth2Client::refreshFinished, this,
            [this, retryFunc, data](HttpError refreshError) {
                if (refreshError == HttpError::NoError) {
                    // Token refreshed, retry the request
                    retryFunc();
                } else {
                    // Refresh failed, propagate the original error
                    QMutexLocker locker(&sequencer->mutex);
                    sequencer->requestQueue.removeOne(this);
                    locker.unlock();
                    emit finished(HttpError::AuthenticationRequired, data);
                    deleteLater();
                }
            },
            static_cast<Qt::ConnectionType>(Qt::QueuedConnection | Qt::SingleShotConnection)
        );
        sequencer->oauth2Client->refresh();
        return;
    }

    QMutexLocker locker(&sequencer->mutex);
    sequencer->requestQueue.removeOne(this);
    locker.unlock();

    emit finished(error, data);
    deleteLater();
}

void HttpRequestInvoker::refresh()
{
    if (!sequencer->oauth2Client) {
        emit finished(HttpError::NetworkError, {});
        deleteLater();
        return;
    }

    connect(
        sequencer->oauth2Client, &OAuth2Client::refreshFinished, this,
        [this](HttpError error) {
            QMutexLocker locker(&sequencer->mutex);
            sequencer->requestQueue.removeOne(this);
            locker.unlock();

            emit finished(error, {});
            deleteLater();
        },
        static_cast<Qt::ConnectionType>(Qt::QueuedConnection | Qt::SingleShotConnection)
    );

    queue([this]() { sequencer->oauth2Client->refresh(); });
}

void HttpRequestInvoker::get(const QUrl &url, const QMap<QByteArray, QByteArray> &headers, int timeout)
{
    int timeoutMs = timeout;

    queue([this, url, headers, timeoutMs]() {
        auto merged = mergeAuthHeaders(headers);
        sequencer->httpClient->get(url, merged, [this, url, headers, timeoutMs](HttpResponse response) {
            handleResponse(response.statusCode, response.error, response.data, [this, url, headers, timeoutMs]() {
                // Retry with refreshed token
                auto merged = mergeAuthHeaders(headers);
                sequencer->httpClient->get(url, merged, [this](HttpResponse response) {
                    QMutexLocker locker(&sequencer->mutex);
                    sequencer->requestQueue.removeOne(this);
                    locker.unlock();
                    emit finished(response.error, response.data);
                    deleteLater();
                }, timeoutMs);
            });
        }, timeoutMs);
    });
}

void HttpRequestInvoker::post(
    const QUrl &url, const QMap<QByteArray, QByteArray> &headers, const QByteArray &data, int timeout
)
{
    int timeoutMs = timeout;

    queue([this, url, headers, data, timeoutMs]() {
        auto merged = mergeAuthHeaders(headers);
        sequencer->httpClient->post(
            url, merged, data,
            [this, url, headers, data, timeoutMs](HttpResponse response) {
                handleResponse(
                    response.statusCode, response.error, response.data,
                    [this, url, headers, data, timeoutMs]() {
                        auto merged = mergeAuthHeaders(headers);
                        sequencer->httpClient->post(url, merged, data, [this](HttpResponse response) {
                            QMutexLocker locker(&sequencer->mutex);
                            sequencer->requestQueue.removeOne(this);
                            locker.unlock();
                            emit finished(response.error, response.data);
                            deleteLater();
                        }, timeoutMs);
                    }
                );
            },
            timeoutMs
        );
    });
}

void HttpRequestInvoker::put(
    const QUrl &url, const QMap<QByteArray, QByteArray> &headers, const QByteArray &data, int timeout
)
{
    int timeoutMs = timeout;

    queue([this, url, headers, data, timeoutMs]() {
        auto merged = mergeAuthHeaders(headers);
        sequencer->httpClient->put(
            url, merged, data,
            [this, url, headers, data, timeoutMs](HttpResponse response) {
                handleResponse(
                    response.statusCode, response.error, response.data,
                    [this, url, headers, data, timeoutMs]() {
                        auto merged = mergeAuthHeaders(headers);
                        sequencer->httpClient->put(url, merged, data, [this](HttpResponse response) {
                            QMutexLocker locker(&sequencer->mutex);
                            sequencer->requestQueue.removeOne(this);
                            locker.unlock();
                            emit finished(response.error, response.data);
                            deleteLater();
                        }, timeoutMs);
                    }
                );
            },
            timeoutMs
        );
    });
}

void HttpRequestInvoker::deleteResource(const QUrl &url, const QMap<QByteArray, QByteArray> &headers, int timeout)
{
    int timeoutMs = timeout;

    queue([this, url, headers, timeoutMs]() {
        auto merged = mergeAuthHeaders(headers);
        sequencer->httpClient->del(url, merged, [this, url, headers, timeoutMs](HttpResponse response) {
            handleResponse(response.statusCode, response.error, response.data, [this, url, headers, timeoutMs]() {
                auto merged = mergeAuthHeaders(headers);
                sequencer->httpClient->del(url, merged, [this](HttpResponse response) {
                    QMutexLocker locker(&sequencer->mutex);
                    sequencer->requestQueue.removeOne(this);
                    locker.unlock();
                    emit finished(response.error, response.data);
                    deleteLater();
                }, timeoutMs);
            });
        }, timeoutMs);
    });
}

void HttpRequestInvoker::head(const QUrl &url, const QMap<QByteArray, QByteArray> &headers, int timeout)
{
    int timeoutMs = timeout;

    queue([this, url, headers, timeoutMs]() {
        auto merged = mergeAuthHeaders(headers);
        sequencer->httpClient->head(url, merged, [this, url, headers, timeoutMs](HttpResponse response) {
            handleResponse(response.statusCode, response.error, response.data, [this, url, headers, timeoutMs]() {
                auto merged = mergeAuthHeaders(headers);
                sequencer->httpClient->head(url, merged, [this](HttpResponse response) {
                    QMutexLocker locker(&sequencer->mutex);
                    sequencer->requestQueue.removeOne(this);
                    locker.unlock();
                    emit finished(response.error, response.data);
                    deleteLater();
                }, timeoutMs);
            });
        }, timeoutMs);
    });
}
