#pragma once
#include <QLocalSocket>
#include <QObject>
#include <QString>
#include <Windows.h>
#include <QWinEventNotifier>

namespace zzj::ipc::qt {
// Secure native listener; accepted handles are handed to Qt for async I/O.
class NamedPipeServer : public QObject {
    Q_OBJECT
public:
    explicit NamedPipeServer(QObject* parent=nullptr);
    ~NamedPipeServer() override;
    bool listen(const QString& name);
    void close();
    QString errorString() const { return error_; }
signals:
    void newConnection(QLocalSocket* socket);
    void acceptError(QString error);
private:
    bool arm();
    void accepted();
    void closePending();
    QString name_, error_;
    HANDLE pipe_{INVALID_HANDLE_VALUE}, event_{};
    OVERLAPPED operation_{};
    QWinEventNotifier* notifier_{};
    bool first_{true}, listening_{}, connectedSynchronously_{};
};
}
