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

#include <memory>

#include <QCoreApplication>
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

// Equivalent of Qt::SingleShotConnection (Qt 6.6+) for the project's Qt floor (6.2).
// On the first invocation the lambda calls disconnect() before the user-supplied callback,
// so subsequent emissions of `signal` no longer reach this slot. disconnect() does not cancel
// slot calls already posted to the receiver's event queue; any in-flight queued events still
// run in order, but each runs at most once because only the first reaches this lambda.
// Requires Qt::QueuedConnection: the disconnect()-before-callback pattern relies on the slot
// running on the receiver's thread via the event loop, not synchronously inside emit.
namespace {
template<typename Sender, typename Signal, typename Receiver, typename Func>
QMetaObject::Connection connectQueuedOneShot(Sender *sender, Signal signal, Receiver *receiver, Func &&func)
{
    auto handle = std::make_shared<QMetaObject::Connection>();
    *handle = QObject::connect(
        sender, signal, receiver,
        [handle, func = std::forward<Func>(func)](auto &&...args) {
            QObject::disconnect(*handle);
            func(std::forward<decltype(args)>(args)...);
        },
        Qt::QueuedConnection
    );
    return *handle;
}
} // namespace

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
        // FIXME: Direct emit of finished() can dispatch to consumer slots that touch
        // already-destructed apiClient state on the standalone-sequencer-destruction path
        // (OBS shutdown is covered by removePostedEvents below). Resolve in a separate PR by
        // either routing finished through Qt::QueuedConnection at construction or disconnecting
        // outgoing finished connections before the emit.
        // Sever only incoming connections (the prev->next chain wired in queue()) so the
        // OperationCanceled emit below still reaches consumer-attached `finished` slots.
        for (auto *invoker : drained) {
            QObject::disconnect(nullptr, nullptr, invoker, nullptr);
        }
        for (auto *invoker : drained) {
            emit invoker->finished(HttpError::OperationCanceled, {});
            // Evict any QueuedConnection slot invocations targeting this invoker before
            // deleting it. Otherwise the next event-loop tick may dispatch a queued slot
            // to a freed object.
            QCoreApplication::removePostedEvents(invoker);
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
    // Sever connections in both directions and evict already-posted QueuedConnection events
    // targeting this invoker so the prev->next chain wired in queue() cannot dispatch into
    // a freed slot.
    disconnect(this);
    QObject::disconnect(nullptr, nullptr, this, nullptr);
    QCoreApplication::removePostedEvents(this);
    if (sequencer) {
        QMutexLocker locker(&sequencer->mutex);
        sequencer->requestQueue.removeAll(this);
    }
    TRACE("HttpRequestInvoker destroyed");
}

template<class Func> void HttpRequestInvoker::queue(Func invoker)
{
    // Chain lifetime invariant: the previous invoker's handleResponse() emits `finished`
    // then posts its own deleteLater(). The QueuedConnection slot below is posted before
    // that deleteLater(), so the next invoker's lambda runs on a fresh event-loop tick
    // while the previous invoker is still alive.
    // Invariant: invoker() must not synchronously emit `finished`. The empty-queue
    // branch relies on this — `requestQueue.append(this)` runs before `invoker()`
    // so a synchronous finished would still find `this` in the queue, but a future
    // curl_multi_socket_action migration could expose new sync paths that violate
    // this invariant.
    QMutexLocker locker(&sequencer->mutex);
    {
        if (sequencer->requestQueue.isEmpty()) {
            sequencer->requestQueue.append(this);
            locker.unlock(); // Must unlock before invoking
            invoker();
            return;
        } else {
            // Reserve next invocation. 5-arg connect with `this` as context: Qt severs the
            // connection if this invoker is destroyed, but already-posted QueuedConnection
            // events are evicted by ~HttpRequestInvoker via removePostedEvents(this).
            // QueuedConnection breaks the synchronous call stack from prev->handleResponse
            // into next->invoker().
            connect(sequencer->requestQueue.last(), &HttpRequestInvoker::finished, this, invoker, Qt::QueuedConnection);
            sequencer->requestQueue.append(this);
        }
        TRACE("Queue request: size=%d", sequencer->requestQueue.size());
    }
    locker.unlock();
}

std::optional<QMap<QByteArray, QByteArray>>
HttpRequestInvoker::mergeAuthHeaders(const QMap<QByteArray, QByteArray> &headers)
{
    QMap<QByteArray, QByteArray> merged = headers;
    // Only inject Bearer token if the caller did not explicitly provide an Authorization header.
    if (!merged.contains("Authorization") && sequencer->oauth2Client) {
        const QString token = sequencer->oauth2Client->accessToken();
        if (token.isEmpty()) {
            // api-client's CHECK_CLIENT_TOKEN gates only on refreshToken, so access token may
            // still be empty here. Caller short-circuits with AuthenticationRequired.
            return std::nullopt;
        }
        merged["Authorization"] = QString("Bearer %1").arg(token).toUtf8();
    }
    return merged;
}

void HttpRequestInvoker::emitAuthRequiredAndCleanup()
{
    QMutexLocker locker(&sequencer->mutex);
    sequencer->requestQueue.removeOne(this);
    locker.unlock();
    // FIXME: Synchronous `emit finished` from a callback path can re-enter this invoker via a
    // direct-connected slot after deleteLater() is posted. Defer the emit to the next event-loop
    // tick (or QueuedConnection-only finished signal) in a follow-up PR to break the re-entry.
    emit finished(HttpError::AuthenticationRequired, QByteArray());
    deleteLater();
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
        connectQueuedOneShot(
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
                    QMutexLocker locker(&self->sequencer->mutex);
                    self->sequencer->requestQueue.removeOne(self);
                    locker.unlock();
                    // FIXME: unlink() emits synchronously and can re-enter request scheduling
                    // before this invoker's `finished`/deleteLater pair runs, breaking the
                    // queue ordering. Defer unlink() to after deleteLater (or post via
                    // QueuedConnection) in a follow-up PR.
                    self->sequencer->oauth2Client->unlink();
                    emit self->finished(HttpError::AuthenticationRequired, data);
                    self->deleteLater();
                }
            }
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
    connectQueuedOneShot(sequencer->oauth2Client, &OAuth2Client::refreshFinished, this, [self](HttpError error) {
        if (!self) {
            return;
        }
        QMutexLocker locker(&self->sequencer->mutex);
        self->sequencer->requestQueue.removeOne(self);
        locker.unlock();

        emit self->finished(error, {});
        self->deleteLater();
    });

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
        if (!merged) {
            emitAuthRequiredAndCleanup();
            return;
        }
        sequencer->httpClient->get(
            url, *merged,
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
                        if (!merged) {
                            self->emitAuthRequiredAndCleanup();
                            return;
                        }
                        self->sequencer->httpClient->get(
                            url, *merged,
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
        if (!merged) {
            emitAuthRequiredAndCleanup();
            return;
        }
        sequencer->httpClient->post(
            url, *merged, data,
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
                        if (!merged) {
                            self->emitAuthRequiredAndCleanup();
                            return;
                        }
                        self->sequencer->httpClient->post(
                            url, *merged, data,
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
        if (!merged) {
            emitAuthRequiredAndCleanup();
            return;
        }
        sequencer->httpClient->put(
            url, *merged, data,
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
                        if (!merged) {
                            self->emitAuthRequiredAndCleanup();
                            return;
                        }
                        self->sequencer->httpClient->put(
                            url, *merged, data,
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
        if (!merged) {
            emitAuthRequiredAndCleanup();
            return;
        }
        sequencer->httpClient->del(
            url, *merged,
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
                        if (!merged) {
                            self->emitAuthRequiredAndCleanup();
                            return;
                        }
                        self->sequencer->httpClient->del(
                            url, *merged,
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
        if (!merged) {
            emitAuthRequiredAndCleanup();
            return;
        }
        sequencer->httpClient->head(
            url, *merged,
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
                        if (!merged) {
                            self->emitAuthRequiredAndCleanup();
                            return;
                        }
                        self->sequencer->httpClient->head(
                            url, *merged,
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
