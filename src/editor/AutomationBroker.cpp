#include "AutomationBroker.h"

#include "LocalEndpoint.h"

#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocalSocket>
#include <QRandomGenerator>
#include <QtEndian>

#include <cstdint>
#include <cstring>
#include <exception>
#include <utility>

namespace
{
constexpr std::uint32_t maximum_message_length = 1024U * 1024U;

QString random_token()
{
    QString result;
    result.reserve(48);
    for (int index = 0; index < 3; ++index)
    {
        result += QStringLiteral("%1").arg(
            QRandomGenerator::system()->generate64(), 16, 16, QLatin1Char{'0'});
    }
    return result;
}
}

AutomationBroker::AutomationBroker(RequestHandler handler, QObject* parent)
    : QObject(parent), handler_(std::move(handler))
{
    connect(&server_, &QLocalServer::newConnection, this, &AutomationBroker::accept_connections);
}

AutomationBroker::~AutomationBroker()
{
    server_.close();
    if (!endpoint_.isEmpty())
    {
        QLocalServer::removeServer(endpoint_);
    }
}

bool AutomationBroker::start(const QString& audit_path)
{
    const auto nonce = QString::number(QRandomGenerator::system()->generate64(), 16);
    const auto endpoint_name = QStringLiteral("dpe-automation-%1-%2")
        .arg(QApplication::applicationPid())
        .arg(nonce);
    endpoint_ = dpe::editor::local_socket_endpoint(endpoint_name);
    if (endpoint_.isEmpty())
    {
        emit status_message(QStringLiteral("Automation endpoint exceeds the portable path limit"));
        return false;
    }
    audit_path_ = audit_path;
    capability_token_ = random_token();
    QLocalServer::removeServer(endpoint_);
    server_.setSocketOptions(QLocalServer::UserAccessOption);
    if (!server_.listen(endpoint_))
    {
        emit status_message(QStringLiteral("Automation broker failed to listen: %1").arg(server_.errorString()));
        return false;
    }
    emit status_message(QStringLiteral("Automation broker listening on a user-restricted local endpoint"));
    return true;
}

void AutomationBroker::accept_connections()
{
    while (server_.hasPendingConnections())
    {
        auto* socket = server_.nextPendingConnection();
        incoming_.insert(socket, {});
        connect(socket, &QLocalSocket::readyRead, this, [this, socket] { consume_messages(socket); });
        connect(socket, &QLocalSocket::disconnected, this, [this, socket] {
            incoming_.remove(socket);
            socket->deleteLater();
        });
    }
}

void AutomationBroker::consume_messages(QLocalSocket* socket)
{
    auto& incoming = incoming_[socket];
    incoming.append(socket->readAll());
    while (incoming.size() >= static_cast<qsizetype>(sizeof(std::uint32_t)))
    {
        const auto length = qFromLittleEndian<std::uint32_t>(incoming.constData());
        if (length == 0 || length > maximum_message_length)
        {
            send_error(socket, {}, -32600, QStringLiteral("Invalid framed message length."));
            socket->disconnectFromServer();
            return;
        }
        const auto framed_size = static_cast<qsizetype>(sizeof(std::uint32_t) + length);
        if (incoming.size() < framed_size)
        {
            return;
        }
        const auto payload = incoming.mid(sizeof(std::uint32_t), length);
        incoming.remove(0, framed_size);
        QJsonParseError parse_error;
        const auto document = QJsonDocument::fromJson(payload, &parse_error);
        if (!document.isObject())
        {
            send_error(socket, {}, -32700, QStringLiteral("Request JSON was invalid."));
            continue;
        }
        handle_request(socket, document.object());
    }
}

void AutomationBroker::handle_request(QLocalSocket* socket, const QJsonObject& request)
{
    const auto id = request.value(QStringLiteral("id"));
    const auto method = request.value(QStringLiteral("method")).toString();
    if (request.value(QStringLiteral("jsonrpc")).toString() != QStringLiteral("2.0") || method.isEmpty())
    {
        send_error(socket, id, -32600, QStringLiteral("A JSON-RPC 2.0 method is required."));
        append_audit(method, request, false, QStringLiteral("invalid request"));
        return;
    }
    if (request.value(QStringLiteral("capabilityToken")).toString() != capability_token_)
    {
        send_error(socket, id, -32001, QStringLiteral("A valid inherited capability token is required."));
        append_audit(method, request, false, QStringLiteral("capability rejected"));
        return;
    }

    const auto parameters = request.value(QStringLiteral("params")).toObject();
    if (method == QStringLiteral("handshake"))
    {
        if (parameters.value(QStringLiteral("protocolVersion")).toInt() != 1)
        {
            send_error(socket, id, -32010, QStringLiteral("Only automation protocol version 1 is supported."));
            append_audit(method, request, false, QStringLiteral("protocol rejected"));
            return;
        }
        send_response(socket, id, {
            {QStringLiteral("protocolVersion"), 1},
            {QStringLiteral("capabilities"), QJsonArray{
                QStringLiteral("inspect-scene"),
                QStringLiteral("dry-run-command"),
                QStringLiteral("execute-approved-command"),
                QStringLiteral("cancel"),
            }},
        });
        append_audit(method, request, true);
        return;
    }

    try
    {
        const auto response = handler_(method, parameters);
        if (response.succeeded)
        {
            send_response(socket, id, response.result);
            append_audit(method, request, true);
        }
        else
        {
            send_error(socket, id, response.error_code, response.error_message);
            append_audit(method, request, false, response.error_message);
        }
    }
    catch (const std::exception& exception)
    {
        const auto message = QString::fromUtf8(exception.what());
        send_error(socket, id, -32603, message);
        append_audit(method, request, false, message);
    }
}

void AutomationBroker::send_response(QLocalSocket* socket, const QJsonValue& id, const QJsonObject& result)
{
    send_message(socket, {
        {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
        {QStringLiteral("id"), id},
        {QStringLiteral("result"), result},
    });
}

void AutomationBroker::send_error(
    QLocalSocket* socket,
    const QJsonValue& id,
    int code,
    const QString& message)
{
    send_message(socket, {
        {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
        {QStringLiteral("id"), id},
        {QStringLiteral("error"), QJsonObject{
            {QStringLiteral("code"), code},
            {QStringLiteral("message"), message},
        }},
    });
}

void AutomationBroker::send_message(QLocalSocket* socket, const QJsonObject& message)
{
    const auto payload = QJsonDocument(message).toJson(QJsonDocument::Compact);
    QByteArray framed(static_cast<qsizetype>(sizeof(std::uint32_t)) + payload.size(), Qt::Uninitialized);
    qToLittleEndian<std::uint32_t>(static_cast<std::uint32_t>(payload.size()), framed.data());
    std::memcpy(framed.data() + sizeof(std::uint32_t), payload.constData(), static_cast<std::size_t>(payload.size()));
    socket->write(framed);
    socket->flush();
}

void AutomationBroker::append_audit(
    const QString& method,
    const QJsonObject& request,
    bool succeeded,
    const QString& error)
{
    QFile audit{audit_path_};
    if (!audit.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
    {
        emit status_message(QStringLiteral("Automation audit append failed: %1").arg(audit.errorString()));
        return;
    }
    const auto parameters = request.value(QStringLiteral("params")).toObject();
    QString capability = QStringLiteral("inspect-scene");
    if (method == QStringLiteral("handshake"))
    {
        capability = QStringLiteral("negotiate-protocol");
    }
    else if (method == QStringLiteral("applyCommand"))
    {
        capability = parameters.value(QStringLiteral("dryRun")).toBool(false)
            ? QStringLiteral("dry-run-command")
            : QStringLiteral("execute-approved-command");
    }
    else if (method == QStringLiteral("cancel"))
    {
        capability = QStringLiteral("cancel");
    }
    const QJsonObject event{
        {QStringLiteral("timestampUtc"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
        {QStringLiteral("correlationId"), parameters.value(QStringLiteral("correlationId")).toString()},
        {QStringLiteral("actor"), parameters.value(QStringLiteral("actor")).toString(QStringLiteral("external-tool"))},
        {QStringLiteral("method"), method},
        {QStringLiteral("dryRun"), parameters.value(QStringLiteral("dryRun")).toBool(false)},
        {QStringLiteral("capability"), capability},
        {QStringLiteral("succeeded"), succeeded},
        {QStringLiteral("error"), error},
    };
    audit.write(QJsonDocument(event).toJson(QJsonDocument::Compact));
    audit.write("\n");
    audit.flush();
}
