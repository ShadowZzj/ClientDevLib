#include "TcpHalfClose.h"
#ifdef _WIN32
#include <winsock2.h>
#else
#include <sys/socket.h>
#endif
namespace zzj::network::qt {
bool shutdownWrite(QTcpSocket* socket, QString* error) {
    if (!socket || socket->socketDescriptor() == -1 || socket->bytesToWrite() != 0) {
        if (error)
            *error = "Socket is not ready for send shutdown";
        return false;
    }
#ifdef _WIN32
    const int result = ::shutdown(static_cast<SOCKET>(socket->socketDescriptor()), SD_SEND);
    const int nativeError = result == 0 ? 0 : WSAGetLastError();
#else
    const int result = ::shutdown(static_cast<int>(socket->socketDescriptor()), SHUT_WR);
    const int nativeError = result == 0 ? 0 : errno;
#endif
    if (result != 0 && error)
        *error = QString("Send shutdown failed: %1").arg(nativeError);
    return result == 0;
}
} // namespace zzj::network::qt
