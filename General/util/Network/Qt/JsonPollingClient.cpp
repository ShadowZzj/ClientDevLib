#include "JsonPollingClient.h"
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSslSocket>
#include <stdexcept>

namespace zzj::network::qt {
JsonPollingClient::JsonPollingClient(QObject* parent) : QObject(parent) {
    manager_.setProxy(QNetworkProxy::NoProxy);
    connect(&interval_, &QTimer::timeout, this, &JsonPollingClient::poll);
    deadline_.setSingleShot(true);
    connect(&deadline_, &QTimer::timeout, this, [this] {
        if (reply_) {
            failure_ = QStringLiteral("Controller request timed out");
            reply_->abort();
        }
    });
}
JsonPollingClient::~JsonPollingClient() { stop(); }
void JsonPollingClient::start(JsonPollingSettings settings, bool polling) {
    stop();
    if (!settings.url.isValid() || settings.url.host().isEmpty() ||
        (settings.url.scheme() != "http" && settings.url.scheme() != "https") ||
        !settings.url.userInfo().isEmpty() || settings.url.hasFragment() ||
        settings.intervalMs < 1000 || settings.intervalMs > 3600000 ||
        settings.timeoutMs < 100 || settings.timeoutMs > 60000 ||
        settings.maximumBytes < 1 || settings.maximumBytes > 1024 * 1024 ||
        settings.bearerToken.size() < 32 || settings.bearerToken.size() > 4096 ||
        settings.bearerToken.contains('\r') || settings.bearerToken.contains('\n'))
        throw std::runtime_error("Invalid JSON polling settings");
    for (const auto character : settings.bearerToken)
        if (static_cast<unsigned char>(character) <= 32 || static_cast<unsigned char>(character) >= 127)
            throw std::runtime_error("Invalid JSON polling token");
    settings_ = std::move(settings);
    settings_.tls.setProtocol(QSsl::TlsV1_2OrLater);
    settings_.tls.setPeerVerifyMode(QSslSocket::VerifyPeer);
    running_ = true;
    if (polling) {
        interval_.start(settings_.intervalMs);
        poll();
    }
}
void JsonPollingClient::stop() {
    running_ = false;
    interval_.stop();
    deadline_.stop();
    if (reply_) {
        auto* reply = reply_.data();
        reply_.clear();
        reply->disconnect(this);
        reply->abort();
        reply->deleteLater();
    }
    bytes_.clear();
    etag_.clear();
    settings_.bearerToken.clear();
}
void JsonPollingClient::acknowledge(const QByteArray& etag) {
    if (etag.size() <= 512 && !etag.contains('\r') && !etag.contains('\n'))
        etag_ = etag;
}
void JsonPollingClient::poll() {
    if (running_ && !reply_)
        request(settings_.url, {}, false);
}
bool JsonPollingClient::post(const QUrl& url, const zzj::json::Json& document) {
    if (!document.is_object())
        return false;
    const auto bytes = zzj::json::dumpBytes(document);
    if (bytes.size() > settings_.maximumBytes)
        return false;
    return request(url, bytes, true);
}
bool JsonPollingClient::request(const QUrl& url, const QByteArray& body, bool post) {
    if (!running_ || reply_ || url.scheme() != settings_.url.scheme() ||
        url.host() != settings_.url.host() || url.port() != settings_.url.port() ||
        !url.userInfo().isEmpty() || url.hasFragment())
        return false;
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
    request.setSslConfiguration(settings_.tls);
    request.setRawHeader("Authorization", "Bearer " + settings_.bearerToken);
    request.setRawHeader("Accept", "application/json");
    if (post)
        request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    else if (!etag_.isEmpty())
        request.setRawHeader("If-None-Match", etag_);
    bytes_.clear();
    failure_.clear();
    auto* reply = post ? manager_.post(request, body) : manager_.get(request);
    reply_ = reply;
    reply->setReadBufferSize(settings_.maximumBytes + 1);
    connect(reply, &QNetworkReply::readyRead, this, [this, reply] {
        bytes_ += reply->read(settings_.maximumBytes + 1 - bytes_.size());
        if (bytes_.size() > settings_.maximumBytes) {
            failure_ = QStringLiteral("Controller response exceeds size limit");
            reply->abort();
        }
    });
    connect(reply, &QNetworkReply::metaDataChanged, this, [this, reply] {
        const auto declared = reply->header(QNetworkRequest::ContentLengthHeader).toLongLong();
        if (declared > settings_.maximumBytes) {
            failure_ = QStringLiteral("Controller response exceeds size limit");
            reply->abort();
        }
    });
    connect(reply, &QNetworkReply::sslErrors, this, [this](const QList<QSslError>&) {
        failure_ = QStringLiteral("Controller TLS verification failed");
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, post] { finish(reply, post); });
    deadline_.start(settings_.timeoutMs);
    return true;
}
void JsonPollingClient::finish(QNetworkReply* reply, bool post) {
    if (reply_ != reply)
        return;
    deadline_.stop();
    bytes_ += reply->read(settings_.maximumBytes + 1 - bytes_.size());
    const auto bytes = bytes_;
    auto failure = failure_;
    const int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const auto networkError = reply->error();
    const auto etag = reply->rawHeader("ETag");
    reply_.clear();
    reply->deleteLater();
    if (bytes.size() > settings_.maximumBytes)
        failure = QStringLiteral("Controller response exceeds size limit");
    if (failure.isEmpty() && networkError != QNetworkReply::NoError)
        failure = QStringLiteral("Controller request failed (HTTP %1)").arg(code);
    if (post) {
        if (failure.isEmpty() && (code < 200 || code >= 300))
            failure = QStringLiteral("Controller returned HTTP %1").arg(code);
        zzj::json::Json document = zzj::json::Json::object();
        if (failure.isEmpty() && !bytes.isEmpty()) {
            try { document = zzj::json::parseObject(bytes, settings_.maximumBytes); }
            catch (const std::exception&) { failure = QStringLiteral("Controller returned invalid JSON"); }
        }
        const QPointer<JsonPollingClient> alive(this);
        if (failure.isEmpty()) emit postResponseReceived(document, code);
        else emit postFailed(failure, code);
        if (alive) emit postFinished(failure.isEmpty());
        return;
    }
    if (!failure.isEmpty()) {
        const QPointer<JsonPollingClient> alive(this);
        emit httpFailed(failure, code);
        if (!alive) return;
        emit failed(failure);
        return;
    }
    if (code == 304) {
        if (etag_.isEmpty()) {
            emit failed(QStringLiteral("Controller returned 304 without an acknowledged policy"));
            return;
        }
        emit notModified();
        return;
    }
    if (code != 200) {
        const QPointer<JsonPollingClient> alive(this);
        emit httpFailed(QStringLiteral("Controller returned HTTP %1").arg(code), code);
        if (!alive) return;
        emit failed(QStringLiteral("Controller returned HTTP %1").arg(code));
        return;
    }
    try {
        const auto document = zzj::json::parseObject(bytes, settings_.maximumBytes);
        emit documentReceived(document, etag);
    } catch (const std::exception&) {
        emit failed(QStringLiteral("Controller returned invalid JSON"));
    }
}
} // namespace zzj::network::qt
