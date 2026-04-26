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

#include <QPointer>

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

HttpRequestSequencer::HttpRequestSequencer(CurlHttpClient *_httpClient, OAuth2Client *_oauth2Client, QObject *parent)
    : QObject(parent),
      httpClient(_httpClient),
      oauth2Client(_oauth2Client)
{
    TRACE("HttpRequestSequencer created");
}

HttpRequestSequencer::~HttpRequestSequencer()
{
    // Move the queue to a local under mutex. ~HttpRequestInvoker also removes itself from
    // requestQueue under the same mutex, so iterating over the live queue while deleting
    // its elements would mutate the container mid-iteration. Draining a local copy avoids
    // that and lets each ~HttpRequestInvoker's removeAll() become a no-op safely (C-1).
    QList<HttpRequestInvoker *> drained;
    {
        QMutexLocker locker(&mutex);
        drained.swap(requestQueue);
    }

    if (!drained.isEmpty()) {
        obs_log(LOG_WARNING, "Draining %d remaining requests in queue.", drained.size());
        // Disconnect inter-invoker chain connections first to prevent
        // cascading lambda invocations during the drain loop (R4-2).
        for (auto *invoker : drained) {
            invoker->disconnect();
        }
        for (auto *invoker : drained) {
            emit invoker->finished(HttpError::OperationCanceled, {});
            delete invoker; // Direct delete: deleteLater() won't run if event loop is stopped (OBS shutdown)
        }
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

// Parallel-mode invariant: the caller must give this invoker a `parent` whose lifetime exceeds
// `httpClient` / `oauth2Client`. The owned sequencer borrows both pointers without ownership,
// so outliving them would leave a dangling reference inside the queued request callback.
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
    // Remove self from sequencer's queue under mutex so that ~HttpRequestSequencer's
    // drain loop never observes a dangling invoker pointer (C-1).
    if (sequencer) {
        QMutexLocker locker(&sequencer->mutex);
        sequencer->requestQueue.removeAll(this);
    }
    TRACE("HttpRequestInvoker destroyed");
}

template<class Func> void HttpRequestInvoker::queue(Func invoker)
{
    // Chain lifetime invariant: the previous invoker's handleResponse() emits `finished`
    // and only then schedules its own deleteLater(). The slot connected below runs
    // synchronously during that emit, so the previous invoker is still alive while this
    // one's request is dispatched -- the deleteLater() fires on a later event-loop tick.
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
        // Use QPointer to guard against dangling this in QueuedConnection lambda.
        // If the invoker is destroyed (e.g., via cancelAll/sequencer dtor) before the
        // queued lambda executes, the QPointer becomes null and we skip the callback.
        QPointer<HttpRequestInvoker> self = this;
        connect(
            sequencer->oauth2Client, &OAuth2Client::refreshFinished, this,
            [self, retryFunc, data](HttpError refreshError) {
                if (!self) {
                    return;
                }
                if (refreshError == HttpError::NoError) {
                    // Token refreshed, retry the request
                    retryFunc();
                } else {
                    // Refresh failed: clear the stored tokens so the user is forced
                    // back through the OAuth2 authorization flow on the next request.
                    self->sequencer->oauth2Client->unlink();
                    QMutexLocker locker(&self->sequencer->mutex);
                    self->sequencer->requestQueue.removeOne(self);
                    locker.unlock();
                    emit self->finished(HttpError::AuthenticationRequired, data);
                    self->deleteLater();
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

    // Use QPointer to guard against dangling this in QueuedConnection lambda,
    // consistent with handleResponse() (R3-CPP-7).
    QPointer<HttpRequestInvoker> self = this;
    connect(
        sequencer->oauth2Client, &OAuth2Client::refreshFinished, this,
        [self](HttpError error) {
            if (!self) {
                return;
            }
            QMutexLocker locker(&self->sequencer->mutex);
            self->sequencer->requestQueue.removeOne(self);
            locker.unlock();

            emit self->finished(error, {});
            self->deleteLater();
        },
        static_cast<Qt::ConnectionType>(Qt::QueuedConnection | Qt::SingleShotConnection)
    );

    queue([this]() { sequencer->oauth2Client->refresh(); });
}

// FIXME: get/post/put/deleteResource/head share the same QPointer-guarded callback structure.
// Extract a templated helper (parameterized by the verb closure) so the lifetime guards live in
// one place. Out of scope for this PR; tracked as a follow-up refactor.
void HttpRequestInvoker::get(const QUrl &url, const QMap<QByteArray, QByteArray> &headers, int timeout)
{
    int timeoutMs = timeout;
    QPointer<HttpRequestInvoker> self = this;

    queue([this, self, url, headers, timeoutMs]() {
        if (!self) {
            return;
        }
        auto merged = mergeAuthHeaders(headers);
        sequencer->httpClient->get(
            url, merged,
            [self, url, headers, timeoutMs](HttpResponse response) {
                if (!self) {
                    return;
                }
                self->handleResponse(
                    response.statusCode, response.error, response.data,
                    [self, url, headers, timeoutMs]() {
                        if (!self) {
                            return;
                        }
                        // Retry with refreshed token
                        auto merged = self->mergeAuthHeaders(headers);
                        self->sequencer->httpClient->get(
                            url, merged,
                            [self](HttpResponse response) {
                                if (!self) {
                                    return;
                                }
                                QMutexLocker locker(&self->sequencer->mutex);
                                self->sequencer->requestQueue.removeOne(self);
                                locker.unlock();
                                emit self->finished(response.error, response.data);
                                self->deleteLater();
                            },
                            timeoutMs
                        );
                    }
                );
            },
            timeoutMs
        );
    });
}

void HttpRequestInvoker::post(
    const QUrl &url, const QMap<QByteArray, QByteArray> &headers, const QByteArray &data, int timeout
)
{
    int timeoutMs = timeout;
    QPointer<HttpRequestInvoker> self = this;

    queue([this, self, url, headers, data, timeoutMs]() {
        if (!self) {
            return;
        }
        auto merged = mergeAuthHeaders(headers);
        sequencer->httpClient->post(
            url, merged, data,
            [self, url, headers, data, timeoutMs](HttpResponse response) {
                if (!self) {
                    return;
                }
                self->handleResponse(
                    response.statusCode, response.error, response.data,
                    [self, url, headers, data, timeoutMs]() {
                        if (!self) {
                            return;
                        }
                        auto merged = self->mergeAuthHeaders(headers);
                        self->sequencer->httpClient->post(
                            url, merged, data,
                            [self](HttpResponse response) {
                                if (!self) {
                                    return;
                                }
                                QMutexLocker locker(&self->sequencer->mutex);
                                self->sequencer->requestQueue.removeOne(self);
                                locker.unlock();
                                emit self->finished(response.error, response.data);
                                self->deleteLater();
                            },
                            timeoutMs
                        );
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
    QPointer<HttpRequestInvoker> self = this;

    queue([this, self, url, headers, data, timeoutMs]() {
        if (!self) {
            return;
        }
        auto merged = mergeAuthHeaders(headers);
        sequencer->httpClient->put(
            url, merged, data,
            [self, url, headers, data, timeoutMs](HttpResponse response) {
                if (!self) {
                    return;
                }
                self->handleResponse(
                    response.statusCode, response.error, response.data,
                    [self, url, headers, data, timeoutMs]() {
                        if (!self) {
                            return;
                        }
                        auto merged = self->mergeAuthHeaders(headers);
                        self->sequencer->httpClient->put(
                            url, merged, data,
                            [self](HttpResponse response) {
                                if (!self) {
                                    return;
                                }
                                QMutexLocker locker(&self->sequencer->mutex);
                                self->sequencer->requestQueue.removeOne(self);
                                locker.unlock();
                                emit self->finished(response.error, response.data);
                                self->deleteLater();
                            },
                            timeoutMs
                        );
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
    QPointer<HttpRequestInvoker> self = this;

    queue([this, self, url, headers, timeoutMs]() {
        if (!self) {
            return;
        }
        auto merged = mergeAuthHeaders(headers);
        sequencer->httpClient->del(
            url, merged,
            [self, url, headers, timeoutMs](HttpResponse response) {
                if (!self) {
                    return;
                }
                self->handleResponse(
                    response.statusCode, response.error, response.data,
                    [self, url, headers, timeoutMs]() {
                        if (!self) {
                            return;
                        }
                        auto merged = self->mergeAuthHeaders(headers);
                        self->sequencer->httpClient->del(
                            url, merged,
                            [self](HttpResponse response) {
                                if (!self) {
                                    return;
                                }
                                QMutexLocker locker(&self->sequencer->mutex);
                                self->sequencer->requestQueue.removeOne(self);
                                locker.unlock();
                                emit self->finished(response.error, response.data);
                                self->deleteLater();
                            },
                            timeoutMs
                        );
                    }
                );
            },
            timeoutMs
        );
    });
}

void HttpRequestInvoker::head(const QUrl &url, const QMap<QByteArray, QByteArray> &headers, int timeout)
{
    int timeoutMs = timeout;
    QPointer<HttpRequestInvoker> self = this;

    queue([this, self, url, headers, timeoutMs]() {
        if (!self) {
            return;
        }
        auto merged = mergeAuthHeaders(headers);
        sequencer->httpClient->head(
            url, merged,
            [self, url, headers, timeoutMs](HttpResponse response) {
                if (!self) {
                    return;
                }
                self->handleResponse(
                    response.statusCode, response.error, response.data,
                    [self, url, headers, timeoutMs]() {
                        if (!self) {
                            return;
                        }
                        auto merged = self->mergeAuthHeaders(headers);
                        self->sequencer->httpClient->head(
                            url, merged,
                            [self](HttpResponse response) {
                                if (!self) {
                                    return;
                                }
                                QMutexLocker locker(&self->sequencer->mutex);
                                self->sequencer->requestQueue.removeOne(self);
                                locker.unlock();
                                emit self->finished(response.error, response.data);
                                self->deleteLater();
                            },
                            timeoutMs
                        );
                    }
                );
            },
            timeoutMs
        );
    });
}
