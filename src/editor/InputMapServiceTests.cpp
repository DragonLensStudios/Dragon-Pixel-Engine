#include "InputMapService.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest/QTest>

namespace
{
bool write_file(const QString& path, const QByteArray& bytes)
{
    if (!QDir{}.mkpath(QFileInfo{path}.absolutePath())) return false;
    QFile file{path};
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate)
        && file.write(bytes) == bytes.size();
}

QByteArray read_file(const QString& path)
{
    QFile file{path};
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}
}

class InputMapServiceTests final : public QObject
{
    Q_OBJECT

private slots:
    void round_trips_unknown_fields_and_evaluates_device_neutral_actions()
    {
        InputMapService service;
        auto document = service.compatibility_map();
        document.extensions.insert(QStringLiteral("futureRoot"), 17);
        document.control_maps.front().extensions.insert(QStringLiteral("futureMap"), true);
        document.control_maps.front().actions.front().extensions.insert(
            QStringLiteral("futureAction"), QStringLiteral("kept"));
        auto& move_x = document.control_maps.front().actions.front();
        move_x.bindings.push_back(InputBinding{
            QStringLiteral("eae595e1-1c80-448d-a508-a8f4c1fe9112"),
            QStringLiteral("gamepad/left-x"), 1.0, 0.2,
            QJsonObject{{QStringLiteral("futureBinding"), 3}}});

        const auto bytes = service.serialize(document);
        const auto parsed = service.parse(bytes);
        QVERIFY2(parsed.succeeded(), parsed.diagnostics.isEmpty()
            ? "parse failed" : qPrintable(parsed.diagnostics.front().message));
        QCOMPARE(service.serialize(*parsed.document), bytes);
        QCOMPARE(parsed.document->extensions.value(QStringLiteral("futureRoot")).toInt(), 17);
        QCOMPARE(parsed.document->control_maps.front().extensions
            .value(QStringLiteral("futureMap")).toBool(), true);
        QCOMPARE(parsed.document->control_maps.front().actions.front().bindings.back()
            .extensions.value(QStringLiteral("futureBinding")).toInt(), 3);

        const auto keyboard = service.evaluate(*parsed.document, {
            {QStringLiteral("keyboard/d"), 1.0},
            {QStringLiteral("keyboard/w"), 1.0},
        });
        QCOMPARE(keyboard.value(QStringLiteral("move.x")).value, 1.0);
        QCOMPARE(keyboard.value(QStringLiteral("move.y")).value, 1.0);

        const auto inside_dead_zone = service.evaluate(*parsed.document, {
            {QStringLiteral("gamepad/left-x"), 0.1},
        });
        QCOMPARE(inside_dead_zone.value(QStringLiteral("move.x")).value, 0.0);
        const auto analog = service.evaluate(*parsed.document, {
            {QStringLiteral("gamepad/left-x"), 0.6},
        });
        QCOMPARE(analog.value(QStringLiteral("move.x")).value, 0.5);
    }

    void rejects_duplicate_ids_unsupported_controls_and_invalid_numbers()
    {
        InputMapService service;
        auto document = service.compatibility_map();
        auto& action = document.control_maps.front().actions.front();
        action.bindings.front().id = action.id;
        action.bindings.front().path = QStringLiteral("framework/key-17");
        action.bindings.front().dead_zone = 1.0;
        const auto parsed = service.parse(service.serialize(document));
        QVERIFY(!parsed.succeeded());
        const auto codes = [&parsed] {
            QStringList result;
            for (const auto& diagnostic : parsed.diagnostics) result.push_back(diagnostic.code);
            return result;
        }();
        QVERIFY(codes.contains(QStringLiteral("invalid_binding_id")));
        QVERIFY(codes.contains(QStringLiteral("unsupported_control")));
        QVERIFY(codes.contains(QStringLiteral("invalid_dead_zone")));
    }

    void saves_atomically_with_compare_before_write_and_containment()
    {
        QTemporaryDir project;
        QVERIFY(project.isValid());
        InputMapService service;
        auto document = service.compatibility_map();
        const auto source = QDir{project.path()}.filePath(
            QStringLiteral("Assets/Input/Default.dpeinputmap"));
        const auto initial = service.serialize(document);
        QVERIFY(write_file(source, initial));

        const auto loaded = service.load(source, project.path());
        QVERIFY2(loaded.succeeded(), loaded.diagnostics.isEmpty()
            ? "load failed" : qPrintable(loaded.diagnostics.front().message));
        auto edited = *loaded.document;
        edited.name = QStringLiteral("Rebound Input");
        edited.control_maps.front().actions.front().bindings.front().path =
            QStringLiteral("keyboard/l");
        const auto saved = service.save(
            source, project.path(), edited, loaded.document->source_hash);
        QVERIFY2(saved.saved, saved.diagnostics.isEmpty()
            ? "save failed" : qPrintable(saved.diagnostics.front().message));
        QVERIFY(read_file(source).contains("keyboard/l"));

        auto stale = edited;
        stale.name = QStringLiteral("Stale overwrite");
        const auto rejected = service.save(
            source, project.path(), stale, loaded.document->source_hash);
        QVERIFY(!rejected.saved);
        QCOMPARE(rejected.diagnostics.front().code, QStringLiteral("source_changed"));
        QVERIFY(!read_file(source).contains("Stale overwrite"));

        QTemporaryDir outside;
        QVERIFY(outside.isValid());
        const auto outside_path = QDir{outside.path()}.filePath(QStringLiteral("map.dpeinputmap"));
        QVERIFY(write_file(outside_path, initial));
        const auto unsafe = service.load(outside_path, project.path());
        QVERIFY(!unsafe.succeeded());
        QCOMPARE(unsafe.diagnostics.front().code, QStringLiteral("unsafe_path"));
    }
};

QTEST_GUILESS_MAIN(InputMapServiceTests)
#include "InputMapServiceTests.moc"

