#include "QtNamedPipeServer.h"
#include "NamedPipeSecurity.h"
#include <QTimer>

namespace zzj::ipc::qt {
NamedPipeServer::NamedPipeServer(QObject* parent):QObject(parent) {}
NamedPipeServer::~NamedPipeServer() { close(); }
void NamedPipeServer::closePending() {
    if (notifier_) { notifier_->setEnabled(false); notifier_->deleteLater(); notifier_=nullptr; }
    if (pipe_!=INVALID_HANDLE_VALUE) { CancelIoEx(pipe_,&operation_); CloseHandle(pipe_); pipe_=INVALID_HANDLE_VALUE; }
    if (event_) { CloseHandle(event_); event_=nullptr; }
    operation_={};connectedSynchronously_=false;
}
void NamedPipeServer::close() { listening_=false; closePending(); }
bool NamedPipeServer::listen(const QString& name) {
    close(); name_=name; first_=true;
    if (name.isEmpty() || name.size()>128 || name.contains(QChar(0)) || name.contains('\\') || name.contains('/')) {
        error_="Invalid local pipe name"; return false;
    }
    listening_=true;
    if (!arm()) { listening_=false; return false; }
    return true;
}
bool NamedPipeServer::arm() {
    try {
        auto security=zzj::ipc::NamedPipeSecurity::ForLocalUsers();
        if(!security){error_=QString("Cannot create pipe security: %1").arg(security.Error());return false;}
        const auto path=(QStringLiteral("\\\\.\\pipe\\")+name_).toStdWString();
        pipe_=CreateNamedPipeW(path.c_str(),PIPE_ACCESS_DUPLEX|FILE_FLAG_OVERLAPPED|
            (first_?FILE_FLAG_FIRST_PIPE_INSTANCE:0),PIPE_TYPE_BYTE|PIPE_READMODE_BYTE|PIPE_WAIT|PIPE_REJECT_REMOTE_CLIENTS,
            16,65536,65536,1000,security.Attributes());
        if (pipe_==INVALID_HANDLE_VALUE) { error_=QString("CreateNamedPipe failed: %1").arg(GetLastError()); return false; }
        first_=false;
        event_=CreateEventW(nullptr,TRUE,FALSE,nullptr);
        if (!event_) { error_="Cannot create pipe connect event"; closePending(); return false; }
        operation_.hEvent=event_;
        notifier_=new QWinEventNotifier(event_,this);
        connect(notifier_,&QWinEventNotifier::activated,this,[this](HANDLE){accepted();});
        const bool completed=ConnectNamedPipe(pipe_,&operation_)!=FALSE;
        const DWORD status=completed?ERROR_SUCCESS:GetLastError();
        connectedSynchronously_=status==ERROR_PIPE_CONNECTED || completed;
        if (connectedSynchronously_) SetEvent(event_);
        else if (status!=ERROR_IO_PENDING) { error_=QString("ConnectNamedPipe failed: %1").arg(status); closePending(); return false; }
        return true;
    } catch (const std::exception& e) { error_=QString::fromUtf8(e.what()); closePending(); return false; }
}
void NamedPipeServer::accepted() {
    if (!listening_ || pipe_==INVALID_HANDLE_VALUE) return;
    notifier_->setEnabled(false);
    DWORD transferred=0;
    const auto success=connectedSynchronously_ || GetOverlappedResult(pipe_,&operation_,&transferred,FALSE);
    const auto status=success?ERROR_SUCCESS:GetLastError();
    if (!success && status!=ERROR_PIPE_CONNECTED && status!=ERROR_SUCCESS) {
        closePending(); if (!arm()) emit acceptError(error_); return;
    }
    auto* socket=new QLocalSocket(this);
    HANDLE connected=pipe_; pipe_=INVALID_HANDLE_VALUE;
    closePending();
    DWORD error=0;
    if (!zzj::ipc::EnsureLocalClient(connected,&error) ||
        !socket->setSocketDescriptor(reinterpret_cast<qintptr>(connected),QLocalSocket::ConnectedState,QIODevice::ReadWrite)) {
        CloseHandle(connected); socket->deleteLater();
    } else emit newConnection(socket);
    if (listening_ && !arm()) emit acceptError(error_);
}
}
