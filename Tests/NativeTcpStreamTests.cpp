#include "Windows/util/Network/Qt/NativeTcpStream.h"
#include "Windows/util/Network/Qt/NativeTcpListener.h"
#include <QCoreApplication>
#include <QTcpServer>
#include <QEventLoop>
#include <QTimer>
#include <functional>
#include <cstdio>
#include <stdexcept>

namespace {
using zzj::network::qt::NativeTcpStream;
class NativeListener final : public zzj::network::qt::NativeTcpListener {
public:
    std::function<void(qintptr)> accepted;
protected:
    void incomingConnection(qintptr descriptor) override { if (accepted) accepted(descriptor); else closesocket(static_cast<SOCKET>(descriptor)); }
};
void Require(bool result, const char* message) { if (!result) throw std::runtime_error(message); }

void HalfCloseRoundTrip(bool dualStack) {
    NativeListener listener;
    Require(listener.listenExclusive(dualStack ? QHostAddress::AnyIPv6 : QHostAddress::LocalHost, 0), "listen native half-close server");
    zzj::network::qt::NativeTcpListener competing;
    Require(!competing.listenExclusive(QHostAddress::LocalHost, listener.serverPort()), "exclusive listener prevents same-port takeover");
    QEventLoop loop;
    QTimer timeout; timeout.setSingleShot(true);
    QString failure;
    bool clientClosed{}, serverClosed{}, serverSawFin{}, clientSawFin{}, serverWriteAfterFin{};
    QByteArray request(128 * 1024, 'q');
    QByteArray response(512 * 1024, 'r');
    QByteArray serverBytes, clientBytes;
    auto fail = [&](const QString& message) { failure = message; loop.quit(); };
    auto done = [&] { if (clientClosed && serverClosed) loop.quit(); };
    QObject::connect(&timeout, &QTimer::timeout, &loop, [&] { fail("half-close timed out"); });
    listener.accepted = [&](qintptr descriptor) {
        auto* stream = new NativeTcpStream(&listener);
        QObject::connect(stream, &NativeTcpStream::errorOccurred, &loop, fail);
        QObject::connect(stream, &NativeTcpStream::readyRead, &loop, [&, stream] {
            while (stream->bytesAvailable()) serverBytes += stream->read();
        });
        QObject::connect(stream, &NativeTcpStream::readChannelFinished, &loop, [&, stream] {
            serverSawFin = true;
            if (serverBytes != request) { fail("request lost before server FIN"); return; }
            serverWriteAfterFin = stream->write(response) == response.size();
            if (!serverWriteAfterFin) { fail("cannot write server response after client FIN"); return; }
            stream->flushWriteClose();
        });
        QObject::connect(stream, &NativeTcpStream::disconnected, &loop, [&] { serverClosed = true; done(); });
        if (!stream->adopt(descriptor)) fail(stream->errorString());
    };
    NativeTcpStream client;
    QObject::connect(&client, &NativeTcpStream::errorOccurred, &loop, fail);
    QObject::connect(&client, &NativeTcpStream::connected, &loop, [&] {
        if (client.peerPort() != listener.serverPort() || client.peerAddress() != QHostAddress(QHostAddress::LocalHost)) {
            fail("actual TCP peer not cached"); return;
        }
        if (client.write(request) != request.size()) { fail("client request queue failed"); return; }
        client.flushWriteClose();
    });
    QObject::connect(&client, &NativeTcpStream::readyRead, &loop, [&] {
        Require(client.bytesAvailable() <= NativeTcpStream::MaxReadQueue, "read queue bound");
        while (client.bytesAvailable()) clientBytes += client.read(16384);
    });
    QObject::connect(&client, &NativeTcpStream::readChannelFinished, &loop, [&] { clientSawFin = true; });
    QObject::connect(&client, &NativeTcpStream::disconnected, &loop, [&] { clientClosed = true; done(); });
    timeout.start(5000);
    client.connectToHost(QHostAddress::LocalHost, listener.serverPort());
    loop.exec();
    Require(failure.isEmpty(), failure.toLocal8Bit().constData());
    Require(serverSawFin && clientSawFin && serverWriteAfterFin && serverClosed && clientClosed, "both native TCP halves closed in order");
    Require(clientBytes == response && serverBytes == request, "large half-close transfer preserves every byte");
}

void ConnectionRefused() {
    QTcpServer reservation;
    Require(reservation.listen(QHostAddress::LocalHost, 0), "reserve refused port");
    const quint16 port = reservation.serverPort(); reservation.close();
    NativeTcpStream client;
    QEventLoop loop;
    QTimer timeout; timeout.setSingleShot(true);
    int errors{}, closes{};
    QObject::connect(&client, &NativeTcpStream::errorOccurred, &loop, [&](const QString&) { ++errors; });
    QObject::connect(&client, &NativeTcpStream::disconnected, &loop, [&] { ++closes; loop.quit(); });
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    timeout.start(3000); client.connectToHost(QHostAddress::LocalHost, port); loop.exec();
    Require(errors == 1 && closes == 1 && client.socketDescriptor() == -1 && client.errorString().contains("10061"),
            "connection refused closes once and reports native error");
}

void WriteQueueBound() {
    NativeListener listener;
    Require(listener.listenExclusive(QHostAddress::LocalHost, 0), "listen queue limit server");
    listener.accepted = [&](qintptr descriptor) { auto* stream = new NativeTcpStream(&listener); stream->adopt(descriptor); };
    NativeTcpStream client;
    QEventLoop loop;
    QTimer timeout; timeout.setSingleShot(true);
    bool rejected{};
    QObject::connect(&client, &NativeTcpStream::connected, &loop, [&] {
        rejected = client.write(QByteArray(NativeTcpStream::MaxWriteQueue + 1, 'x')) == -1;
        loop.quit();
    });
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    timeout.start(3000); client.connectToHost(QHostAddress::LocalHost, listener.serverPort()); loop.exec();
    Require(rejected && client.socketDescriptor() == -1 && client.bytesToWrite() == 0 && client.errorString().contains("10055"),
            "write overflow fails closed without an unbounded queue");
}
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    try {
        HalfCloseRoundTrip(false);
        HalfCloseRoundTrip(true);
        ConnectionRefused();
        WriteQueueBound();
        std::puts("Native TCP: IPv4 + dual-stack half-close 128 KiB / 512 KiB, exclusive listener, peer, refusal, queue bound passed");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "Native TCP test failed: %s\n", error.what()); return 1;
    }
}
