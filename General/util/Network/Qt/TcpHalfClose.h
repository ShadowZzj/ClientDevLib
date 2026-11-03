#pragma once
#include <QString>
#include <QTcpSocket>
namespace zzj::network::qt {
// Caller must wait for Qt's pending bytes before shutting down the native send side.
bool shutdownWrite(QTcpSocket* socket, QString* error = nullptr);
} // namespace zzj::network::qt
