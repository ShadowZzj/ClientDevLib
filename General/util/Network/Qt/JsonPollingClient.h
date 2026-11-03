#pragma once
#include <General/util/Json/JsonQtTypes.h>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QSslConfiguration>
#include <QTimer>
#include <QUrl>

namespace zzj::network::qt {
struct JsonPollingSettings {
    QUrl url;
    QByteArray bearerToken;
    QSslConfiguration tls{QSslConfiguration::defaultConfiguration()};
    int intervalMs{5000}, timeoutMs{10000};
    qsizetype maximumBytes{1024 * 1024};
};
// One bounded asynchronous request at a time. Consumers acknowledge an ETag
// only after validating and applying the corresponding document.
class JsonPollingClient final : public QObject {
    Q_OBJECT
  public:
    explicit JsonPollingClient(QObject* parent = nullptr);
    ~JsonPollingClient() override;
    void start(JsonPollingSettings settings, bool polling = true);
    void stop();
    void acknowledge(const QByteArray& etag);
    bool post(const QUrl& url, const zzj::json::Json& document);
    bool inFlight() const { return !reply_.isNull(); }
  signals:
    void documentReceived(const zzj::json::Json& document, const QByteArray& etag);
    void notModified();
    void failed(const QString& reason);
    void postFinished(bool success);
    void postResponseReceived(const zzj::json::Json& document, int httpStatus);
    void postFailed(const QString& reason, int httpStatus);
    void httpFailed(const QString& reason, int httpStatus);
  private:
    void poll();
    bool request(const QUrl&, const QByteArray&, bool post);
    void finish(QNetworkReply*, bool post);
    QNetworkAccessManager manager_;
    QPointer<QNetworkReply> reply_;
    QTimer interval_, deadline_;
    JsonPollingSettings settings_;
    QByteArray bytes_, etag_;
    QString failure_;
    bool running_{};
};
} // namespace zzj::network::qt
