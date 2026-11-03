#include <General/util/Network/Qt/ReverseRelay.h>
#include <General/util/Network/Qt/TlsConfiguration.h>
#include <Windows/util/Network/Qt/NativeTcpStream.h>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QNetworkInterface>
#include <QNetworkProxy>
#include <QSslCertificate>
#include <QSslKey>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTextStream>
#include <QTimer>
#include <array>
#include <functional>
#include <memory>
#include <stdexcept>

namespace {
using namespace zzj::network::qt;

bool await(const std::function<bool()>& predicate, int maximum = 5000) {
    QElapsedTimer elapsed;
    elapsed.start();
    while (!predicate() && elapsed.elapsed() < maximum) {
        QEventLoop loop;
        QTimer::singleShot(5, &loop, &QEventLoop::quit);
        loop.exec();
    }
    return predicate();
}
void require(bool result, const QString& message) {
    if (!result)
        throw std::runtime_error(message.toStdString());
    QTextStream(stdout) << "PASS " << message << '\n';
}

class Target final : public QTcpServer {
  public:
    enum Mode { Echo, RespondAfterFin, SendFinFirst };
    explicit Target(Mode selected) : mode(selected) {}
    Mode mode;
    int connections{}, receiveFins{};
    QByteArray received;
    QString error;

  protected:
    void incomingConnection(qintptr descriptor) override {
        ++connections;
        auto* stream = new NativeTcpStream(this);
        const auto bytes = std::make_shared<QByteArray>();
        QObject::connect(stream, &NativeTcpStream::errorOccurred, this,
                         [this](const QString& message) { error = message; });
        QObject::connect(stream, &NativeTcpStream::readyRead, stream, [this, stream, bytes] {
            while (stream->bytesAvailable()) {
                const QByteArray chunk = stream->read();
                *bytes += chunk;
                received += chunk;
                if (mode == Echo && stream->write(chunk) != chunk.size())
                    error = stream->errorString();
            }
        });
        QObject::connect(stream, &NativeTcpStream::readChannelFinished, stream, [this, stream, bytes] {
            ++receiveFins;
            if (mode == RespondAfterFin) {
                const QByteArray response = "reply-after-client-fin:" + *bytes;
                if (stream->write(response) != response.size())
                    error = stream->errorString();
            }
            stream->flushWriteClose();
        });
        QObject::connect(stream, &NativeTcpStream::disconnected, stream, &QObject::deleteLater);
        if (!stream->adopt(descriptor)) {
            error = stream->errorString();
            stream->deleteLater();
            return;
        }
        if (mode == SendFinFirst) {
            if (stream->write("target-before-fin") != 17)
                error = stream->errorString();
            stream->flushWriteClose();
        }
    }
};

QByteArray certificatePin(const QString& path) {
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), "Relay peer certificate can be read");
    const auto certificates = QSslCertificate::fromData(file.readAll());
    require(certificates.size() == 1, "Relay peer leaf certificate parsed");
    return certificates.front().digest(QCryptographicHash::Sha256);
}

void echoRoundTrip(quint16 port, const QByteArray& payload, const QString& label) {
    QTcpSocket socket;
    QByteArray received;
    QObject::connect(&socket, &QTcpSocket::readyRead, &socket, [&] { received += socket.readAll(); });
    socket.connectToHost(QHostAddress::LocalHost, port);
    require(await([&] { return socket.state() == QAbstractSocket::ConnectedState; }), label + " connected");
    require(socket.write(payload) == payload.size(), label + " queued");
    require(await([&] { return received.size() >= payload.size(); }) && received == payload,
            label + " preserved both directions");
    socket.disconnectFromHost();
    require(await([&] { return socket.state() == QAbstractSocket::UnconnectedState; }), label + " closed");
}

void expectIngressDenied(quint16 port, Target& target, const QString& label) {
    const int before = target.connections;
    QTcpSocket socket;
    bool finished = false;
    QObject::connect(&socket, &QTcpSocket::disconnected, &socket, [&] { finished = true; });
    QObject::connect(&socket, &QTcpSocket::errorOccurred, &socket,
                     [&](QAbstractSocket::SocketError) { finished = true; });
    socket.connectToHost(QHostAddress::LocalHost, port);
    socket.write("must-not-reach-fixed-target");
    require(await([&] { return finished; }) && target.connections == before,
            label + ": ingress closed without opening a target connection");
}

void expectPeerDenied(ReverseRelayServer& server, const ReverseRelayClientSettings& settings,
                      const QString& label, bool legitimatePeerPresent) {
    ReverseRelayClient denied;
    bool failed = false, serverFailed = false;
    const auto serverFailure = QObject::connect(&server, &ReverseRelayServer::failed, &denied,
                                               [&](const QString&) { serverFailed = true; });
    QObject::connect(&denied, &ReverseRelayClient::failed, &denied, [&](const QString&) { failed = true; });
    QString error;
    const bool started = denied.start(settings, &error);
    require(!started || (await([&] { return failed && (legitimatePeerPresent || serverFailed); }) && !denied.ready()), label);
    require(server.ready() == legitimatePeerPresent, label + " preserves existing peer state");
    denied.stop();
    QObject::disconnect(serverFailure);
}

void halfCloseRoundTrip(ReverseRelayServer& server, ReverseRelayClient& connector,
                        ReverseRelayClientSettings settings, Target& target, bool targetFinFirst) {
    connector.stop();
    require(await([&] { return !server.ready() && server.activeStreamCount() == 0; }),
            "Previous relay session fully retired before half-close test");
    settings.localTargetPort = target.serverPort();
    QString error;
    require(connector.start(settings, &error) && await([&] { return server.ready() && connector.ready(); }),
            "Connector reverse session established for half-close test: " + error);
    NativeTcpStream ingress;
    QByteArray received;
    QString failure;
    bool connected{}, receivedFin{}, disconnected{};
    QObject::connect(&ingress, &NativeTcpStream::connected, &ingress, [&] { connected = true; });
    QObject::connect(&ingress, &NativeTcpStream::readyRead, &ingress, [&] {
        while (ingress.bytesAvailable())
            received += ingress.read();
    });
    QObject::connect(&ingress, &NativeTcpStream::readChannelFinished, &ingress, [&] { receivedFin = true; });
    QObject::connect(&ingress, &NativeTcpStream::disconnected, &ingress, [&] { disconnected = true; });
    QObject::connect(&ingress, &NativeTcpStream::errorOccurred, &ingress,
                     [&](const QString& message) { failure = message; });
    ingress.connectToHost(QHostAddress::LocalHost, server.ingressPort());
    require(await([&] { return connected || !failure.isEmpty(); }) && connected,
            "Native ingress connected for half-close");
    const QByteArray payload = targetFinFirst ? QByteArray("client-data-after-target-fin") : QByteArray(128 * 1024, 'q');
    if (targetFinFirst)
        require(await([&] { return receivedFin; }) && received == "target-before-fin",
                "Target bytes and receive FIN cross the reverse relay before client send FIN");
    require(ingress.write(payload) == payload.size(), "Client can queue data with its send half open");
    ingress.flushWriteClose();
    require(await([&] { return disconnected || !failure.isEmpty(); }) && failure.isEmpty() && disconnected &&
                receivedFin && target.receiveFins == 1 && target.received == payload && target.error.isEmpty(),
            targetFinFirst ? "Client bytes after target FIN cross both relay hops; both halves finish"
                           : "Target receives client FIN then sends response across both relay hops");
    if (!targetFinFirst)
        require(received == "reply-after-client-fin:" + payload, "Large response after client FIN is byte exact");
    require(await([&] { return server.activeStreamCount() == 0 && connector.activeStreamCount() == 0; }),
            "Half-close retires all relay streams");
}

void stopFromAuthenticatedEvent(ReverseRelayServerSettings serverSettings,
                                ReverseRelayClientSettings clientSettings, bool stopServer) {
    serverSettings.ingressPort = 0;
    serverSettings.reversePort = 0;
    ReverseRelayServer server;
    ReverseRelayClient connector;
    bool stoppedFromEvent{}, oppositePeerDisconnected{}, staleReady{};
    QList<bool> notifications;
    QString error;
    require(server.start(serverSettings, &error), "Event reentrancy test POP started");
    const auto observeReadiness = [&](bool ready) {
        notifications.append(ready);
        if (stoppedFromEvent && ready) staleReady = true;
    };
    if (stopServer) {
        QObject::connect(&server, &ReverseRelayServer::readyChanged, &server, observeReadiness);
        QObject::connect(&server, &ReverseRelayServer::event, &server, [&](const QString&) {
            if (!stoppedFromEvent && server.ready()) {
                stoppedFromEvent = true;
                server.stop();
            }
        });
        QObject::connect(&connector, &ReverseRelayClient::failed, &connector,
                         [&](const QString&) { oppositePeerDisconnected = true; });
    } else {
        QObject::connect(&connector, &ReverseRelayClient::readyChanged, &connector, observeReadiness);
        QObject::connect(&connector, &ReverseRelayClient::event, &connector, [&](const QString&) {
            if (!stoppedFromEvent && connector.ready()) {
                stoppedFromEvent = true;
                connector.stop();
            }
        });
        QObject::connect(&server, &ReverseRelayServer::failed, &server,
                         [&](const QString&) { oppositePeerDisconnected = true; });
    }
    clientSettings.port = server.reversePort();
    require(connector.start(clientSettings, &error), "Event reentrancy test Connector started");
    require(await([&] { return stoppedFromEvent && oppositePeerDisconnected; }) &&
                !(stopServer ? server.ready() : connector.ready()) && !staleReady &&
                !notifications.isEmpty() && !notifications.back(),
            stopServer ? "Stopping POP synchronously from authenticated event never publishes stale ready=true"
                       : "Stopping Connector synchronously from authenticated event never publishes stale ready=true");
    connector.stop();
    server.stop();
    require(server.activeStreamCount() == 0 && connector.activeStreamCount() == 0,
            "Synchronous event stop leaves no relay streams");
}

void ignoresApplicationCachingProxy(ReverseRelayServerSettings serverSettings,
                                    ReverseRelayClientSettings clientSettings) {
    QHostAddress address;
    for (const auto& interface : QNetworkInterface::allInterfaces()) {
        const auto flags = interface.flags();
        if (!flags.testFlag(QNetworkInterface::IsUp) || !flags.testFlag(QNetworkInterface::IsRunning) ||
            flags.testFlag(QNetworkInterface::IsLoopBack))
            continue;
        for (const auto& entry : interface.addressEntries()) {
            const auto candidate = entry.ip();
            if (candidate.protocol() == QAbstractSocket::IPv4Protocol && !candidate.isLoopback() &&
                !candidate.isMulticast() && candidate != QHostAddress::AnyIPv4 &&
                candidate != QHostAddress::Broadcast) {
                address = candidate;
                break;
            }
        }
        if (!address.isNull())
            break;
    }
    if (address.isNull()) {
        QTextStream(stdout) << "SKIP application proxy bypass: no active non-loopback IPv4 interface; "
                               "loopback would not exercise Qt application proxy selection\n";
        return;
    }

    Target target(Target::Echo);
    require(target.listen(QHostAddress::LocalHost, 0), "Application proxy regression fixed target started");
    QTcpServer forbiddenProxy;
    int proxyConnections{};
    const bool observingProxy = forbiddenProxy.listen(QHostAddress::LocalHost, 1);
    if (observingProxy) {
        QObject::connect(&forbiddenProxy, &QTcpServer::newConnection, &forbiddenProxy, [&] {
            while (auto* socket = forbiddenProxy.nextPendingConnection()) {
                ++proxyConnections;
                socket->abort();
                socket->deleteLater();
            }
        });
    } else {
        QTextStream(stdout) << "NOTE application proxy port 1 is unavailable; "
                               "non-loopback mTLS and opaque echo remain required\n";
    }
    struct RestoreProxy {
        QNetworkProxy previous = QNetworkProxy::applicationProxy();
        bool systemConfiguration = QNetworkProxyFactory::usesSystemConfiguration();
        ~RestoreProxy() {
            QNetworkProxyFactory::setUseSystemConfiguration(systemConfiguration);
            if (!systemConfiguration)
                QNetworkProxy::setApplicationProxy(previous);
        }
    } restoreProxy;
    QNetworkProxy::setApplicationProxy(QNetworkProxy(QNetworkProxy::HttpCachingProxy, "127.0.0.1", 1));

    serverSettings.ingressPort = serverSettings.reversePort = 0;
    serverSettings.reverseHost = QHostAddress::AnyIPv4;
    ReverseRelayServer server;
    ReverseRelayClient connector;
    QString error;
    require(server.start(serverSettings, &error), "Application proxy regression POP listens on all IPv4 interfaces");
    clientSettings.host = address.toString();
    clientSettings.peerName = "localhost";
    clientSettings.port = server.reversePort();
    clientSettings.localTargetPort = target.serverPort();
    require(connector.start(clientSettings, &error) && await([&] { return server.ready() && connector.ready(); }),
            "Non-loopback Connector mTLS to " + address.toString() + " bypasses the inherited HTTP caching proxy");
    echoRoundTrip(server.ingressPort(), QByteArray("proxy-bypass\0opaque\xff", 20),
                  "Opaque TCP across a non-loopback reverse connection with an application proxy");
    require(target.connections == 1 && target.error.isEmpty(), "Application proxy regression reaches only its fixed target");
    if (observingProxy)
        require(proxyConnections == 0 && !forbiddenProxy.hasPendingConnections(),
                "Inherited HTTP caching proxy receives no connection from the reverse relay");
    connector.stop();
    server.stop();
    require(!server.ready() && !connector.ready() && server.activeStreamCount() == 0 &&
                connector.activeStreamCount() == 0,
            "Application proxy regression retires all reverse streams");
}
} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    try {
        require(app.arguments().size() == 2, "Development TLS fixture directory supplied");
        const QString directory = app.arguments().at(1);
        Target target(Target::Echo), clientFinTarget(Target::RespondAfterFin), targetFinTarget(Target::SendFinFirst);
        require(target.listen(QHostAddress::LocalHost, 0) && clientFinTarget.listen(QHostAddress::LocalHost, 0) &&
                    targetFinTarget.listen(QHostAddress::LocalHost, 0), "Private fixed TCP targets started");
        ReverseRelayServerSettings serverSettings;
        serverSettings.tls = loadMutualTls({directory + "/ca.pem", directory + "/server.pem", directory + "/server-key.pem"});
        serverSettings.peerId = "integration-connector";
        serverSettings.peerCertificateSha256 = certificatePin(directory + "/client.pem");
        ReverseRelayServer server;
        QString error;
        require(server.start(serverSettings, &error), "POP ingress and reverse TLS listeners started: " + error);
        require(!server.ready(), "POP is not ready until a Connector reverse connection authenticates");
        expectIngressDenied(server.ingressPort(), target, "Without Connector");
        require(server.activeStreamCount() == 0, "Denied ingress leaves no relay stream");

        ReverseRelayClientSettings settings;
        settings.host = "127.0.0.1";
        settings.peerName = "localhost";
        settings.port = server.reversePort();
        settings.peerId = serverSettings.peerId;
        settings.tls = loadMutualTls({directory + "/ca.pem", directory + "/client.pem", directory + "/client-key.pem"});
        settings.localTargetPort = target.serverPort();
        // With no reserved peer, these failures exercise TLS and authentication themselves,
        // rather than simply hitting the active-peer admission guard.
        auto invalid = settings;
        invalid.peerId = "unregistered-connector";
        expectPeerDenied(server, invalid, "Unregistered Connector identity rejected", false);
        invalid = settings;
        invalid.peerName = "invalid-relay-host.example";
        expectPeerDenied(server, invalid, "POP TLS hostname mismatch rejected", false);
        invalid = settings;
        invalid.tls.setLocalCertificateChain({});
        invalid.tls.setPrivateKey(QSslKey{});
        expectPeerDenied(server, invalid, "Connector without mTLS identity rejected", false);
        QTcpSocket unauthenticatedPeer;
        unauthenticatedPeer.connectToHost(QHostAddress::LocalHost, server.reversePort());
        require(await([&] { return unauthenticatedPeer.state() == QAbstractSocket::ConnectedState; }),
                "Unauthenticated raw reverse TCP peer holds a pending TLS handshake");
        ReverseRelayClient connector;
        require(connector.start(settings, &error) && await([&] { return connector.ready() && server.ready(); }),
                "Connector initiates and authenticates mTLS reverse connection to POP: " + error);
        require(await([&] { return unauthenticatedPeer.state() == QAbstractSocket::UnconnectedState; }),
                "Authenticated Connector activation releases the idle unauthenticated reverse candidate");
        echoRoundTrip(server.ingressPort(), QByteArray("binary\0relay\xff", 13), "Binary TCP relay");

        std::array<std::unique_ptr<QTcpSocket>, 4> sockets;
        std::array<QByteArray, 4> received, payloads;
        for (size_t index = 0; index < sockets.size(); ++index) {
            sockets[index] = std::make_unique<QTcpSocket>();
            payloads[index] = QByteArray(192 * 1024 + static_cast<int>(index), static_cast<char>('a' + index));
            payloads[index].replace(100, 5, QByteArray("\0\1\2\3\4", 5));
            QObject::connect(sockets[index].get(), &QTcpSocket::readyRead, &app,
                             [&, index] { received[index] += sockets[index]->readAll(); });
            sockets[index]->connectToHost(QHostAddress::LocalHost, server.ingressPort());
        }
        require(await([&] {
            for (const auto& socket : sockets)
                if (socket->state() != QAbstractSocket::ConnectedState) return false;
            return true;
        }), "Four concurrent relay streams connected");
        for (size_t index = 0; index < sockets.size(); ++index)
            require(sockets[index]->write(payloads[index]) == payloads[index].size(), "Concurrent stream queued");
        require(await([&] {
            for (size_t index = 0; index < sockets.size(); ++index)
                if (received[index].size() < payloads[index].size()) return false;
            return true;
        }) && received == payloads, "Concurrent streams preserve binary bytes and isolate session payloads");
        const auto counters = server.counters();
        require(counters.streamsOpened >= 5 && counters.bytesToConnector >= 4 * 192 * 1024 &&
                    counters.bytesFromConnector >= 4 * 192 * 1024,
                "POP counters prove request and response bytes traversed the reverse connection");

        expectPeerDenied(server, settings, "Duplicate valid Connector cannot replace an active reverse session", true);
        echoRoundTrip(server.ingressPort(), "legitimate-peer-survived", "Authorized peer after rejected connections");
        require(target.error.isEmpty(), "Successful echo transfers observed no native target socket errors");

        connector.stop();
        require(await([&] {
            for (const auto& socket : sockets)
                if (socket->state() != QAbstractSocket::UnconnectedState) return false;
            return !server.ready() && server.activeStreamCount() == 0;
        }), "Connector disconnect terminates every ingress stream without fallback");
        expectIngressDenied(server.ingressPort(), target, "After Connector disconnect");
        require(connector.activeStreamCount() == 0, "Stopped Connector has no remaining target streams");
        for (auto& socket : sockets) socket.reset();

        require(connector.start(settings, &error) && await([&] { return server.ready() && connector.ready(); }),
                "Explicit Connector restart establishes a new reverse session");
        const auto ingressPort = server.ingressPort(), reversePort = server.reversePort();
        server.stop();
        require(await([&] { return !connector.ready(); }), "Connector detects loss of POP");
        serverSettings.ingressPort = ingressPort;
        serverSettings.reversePort = reversePort;
        require(server.start(serverSettings, &error) && await([&] { return server.ready() && connector.ready(); }, 10000),
                "Connector automatically reconnects after POP restarts on the same endpoints");
        echoRoundTrip(server.ingressPort(), "after-pop-restart", "TCP relay after automatic reconnect");
        halfCloseRoundTrip(server, connector, settings, clientFinTarget, false);
        halfCloseRoundTrip(server, connector, settings, targetFinTarget, true);
        connector.stop();
        require(await([&] { return !server.ready() && server.activeStreamCount() == 0; }),
                "Final Connector stop removes readiness and every relay stream");
        server.stop();
        require(server.activeStreamCount() == 0 && connector.activeStreamCount() == 0, "POP and Connector stop cleanly");

        auto badPin = serverSettings;
        badPin.ingressPort = 0;
        badPin.reversePort = 0;
        badPin.peerCertificateSha256 = QByteArray(32, '\0');
        ReverseRelayServer pinServer;
        require(pinServer.start(badPin, &error), "Pinned-certificate rejection POP started");
        invalid = settings;
        invalid.port = pinServer.reversePort();
        expectPeerDenied(pinServer, invalid, "Connector certificate with incorrect pin rejected", false);
        expectIngressDenied(pinServer.ingressPort(), target, "After certificate pin rejection");
        pinServer.stop();
        stopFromAuthenticatedEvent(serverSettings, settings, true);
        stopFromAuthenticatedEvent(serverSettings, settings, false);
        ignoresApplicationCachingProxy(serverSettings, settings);
        QTextStream(stdout) << "All reverse relay integration assertions passed\n";
        return 0;
    } catch (const std::exception& exception) {
        QTextStream(stderr) << "FAIL " << exception.what() << '\n';
        return 1;
    }
}
