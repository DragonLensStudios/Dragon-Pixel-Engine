#pragma once

#include <QByteArray>
#include <QHash>
#include <QJsonObject>
#include <QLocalServer>
#include <QObject>
#include <QString>

#include <functional>

class QLocalSocket;

struct AutomationResponse final
{
    bool succeeded{};
    QJsonObject result;
    int error_code{-32000};
    QString error_message;
};

class AutomationBroker final : public QObject
{
    Q_OBJECT

public:
    using RequestHandler = std::function<AutomationResponse(const QString&, const QJsonObject&)>;

    explicit AutomationBroker(RequestHandler handler, QObject* parent = nullptr);
    ~AutomationBroker() override;

    [[nodiscard]] bool start(const QString& audit_path);
    [[nodiscard]] const QString& endpoint() const noexcept { return endpoint_; }
    [[nodiscard]] const QString& capability_token() const noexcept { return capability_token_; }
    [[nodiscard]] const QString& audit_path() const noexcept { return audit_path_; }

signals:
    void status_message(const QString& message);

private:
    void accept_connections();
    void consume_messages(QLocalSocket* socket);
    void handle_request(QLocalSocket* socket, const QJsonObject& request);
    void send_response(QLocalSocket* socket, const QJsonValue& id, const QJsonObject& result);
    void send_error(QLocalSocket* socket, const QJsonValue& id, int code, const QString& message);
    void send_message(QLocalSocket* socket, const QJsonObject& message);
    void append_audit(const QString& method, const QJsonObject& request, bool succeeded, const QString& error = {});

    QLocalServer server_;
    QHash<QLocalSocket*, QByteArray> incoming_;
    RequestHandler handler_;
    QString endpoint_;
    QString capability_token_;
    QString audit_path_;
};
