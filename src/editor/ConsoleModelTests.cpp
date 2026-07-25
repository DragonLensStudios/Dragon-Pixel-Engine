#include "EditorModels.h"

#include <QColor>
#include <QSignalSpy>
#include <QtTest>

class ConsoleModelTests final : public QObject
{
    Q_OBJECT

private slots:
    void exposes_structured_columns_roles_and_navigation()
    {
        ConsoleModel model;
        // Original aggregate field order remains valid.
        model.append({
            QStringLiteral("10:00:00.000"),
            QStringLiteral("Info"),
            QStringLiteral("Editor"),
            QStringLiteral("legacy-context"),
            QStringLiteral("Legacy message"),
        });

        ConsoleEntry rich{
            QStringLiteral("10:00:01.250"),
            QStringLiteral("warning"),
            QStringLiteral("Runtime"),
            QStringLiteral("entity transform"),
            QStringLiteral("Value was clamped"),
        };
        rich.worker = QStringLiteral("preview-worker");
        rich.session = QStringLiteral("session-7");
        rich.correlation_id = QStringLiteral("corr-42");
        rich.entity_id = QStringLiteral("11111111-1111-4111-8111-111111111111");
        rich.asset_id = QStringLiteral("22222222-2222-4222-8222-222222222222");
        rich.navigation_path = QStringLiteral("Assets/Dragon.sprite.dpeasset");
        model.append(rich);

        QCOMPARE(model.rowCount(), 2);
        QCOMPARE(model.columnCount(), static_cast<int>(ConsoleColumn::count));
        const QStringList expected_headers{
            QStringLiteral("Time"),
            QStringLiteral("Severity"),
            QStringLiteral("Subsystem"),
            QStringLiteral("Worker"),
            QStringLiteral("Session"),
            QStringLiteral("Correlation ID"),
            QStringLiteral("Context"),
            QStringLiteral("Message"),
        };
        for (auto column = 0; column < expected_headers.size(); ++column)
        {
            QCOMPARE(
                model.headerData(column, Qt::Horizontal, Qt::DisplayRole).toString(),
                expected_headers.at(column));
        }

        const auto index = model.index(1, static_cast<int>(ConsoleColumn::message));
        QCOMPARE(index.data().toString(), QStringLiteral("Value was clamped"));
        QCOMPARE(index.data(EditorRoles::console_timestamp).toString(), QStringLiteral("10:00:01.250"));
        QCOMPARE(index.data(EditorRoles::console_severity).toString(), QStringLiteral("warning"));
        QCOMPARE(index.data(EditorRoles::console_subsystem).toString(), QStringLiteral("Runtime"));
        QCOMPARE(index.data(EditorRoles::console_worker).toString(), QStringLiteral("preview-worker"));
        QCOMPARE(index.data(EditorRoles::console_session).toString(), QStringLiteral("session-7"));
        QCOMPARE(index.data(EditorRoles::console_correlation_id).toString(), QStringLiteral("corr-42"));
        QCOMPARE(index.data(EditorRoles::console_context).toString(), QStringLiteral("entity transform"));
        QCOMPARE(index.data(EditorRoles::console_message).toString(), QStringLiteral("Value was clamped"));
        QCOMPARE(index.data(EditorRoles::console_entity_id).toString(), rich.entity_id);
        QCOMPARE(index.data(EditorRoles::console_asset_id).toString(), rich.asset_id);
        QCOMPARE(index.data(EditorRoles::console_navigation_path).toString(), rich.navigation_path);
        QVERIFY(index.data(EditorRoles::console_filter_text).toString().contains(rich.asset_id));
        const auto tooltip = index.data(Qt::ToolTipRole).toString();
        QVERIFY(tooltip.contains(QStringLiteral("Correlation ID: corr-42")));
        QVERIFY(tooltip.contains(QStringLiteral("Entity: %1").arg(rich.entity_id)));
        QVERIFY(tooltip.contains(QStringLiteral("Path: %1").arg(rich.navigation_path)));
        QCOMPARE(index.data(Qt::ForegroundRole).value<QColor>(), QColor(220, 160, 40));

        RecursiveFilterProxyModel proxy;
        proxy.setSourceModel(&model);
        proxy.setFilterKeyColumn(-1);
        proxy.setFilterFixedString(QStringLiteral("session-7"));
        QCOMPARE(proxy.rowCount(), 1);
        proxy.setFilterFixedString(QStringLiteral("legacy-context"));
        QCOMPARE(proxy.rowCount(), 1);
    }

    void append_retention_and_clear_emit_model_changes()
    {
        ConsoleModel model;
        QSignalSpy inserted{&model, &QAbstractItemModel::rowsInserted};
        for (auto index = 0; index <= 2000; ++index)
        {
            model.append({
                QString::number(index),
                QStringLiteral("Info"),
                QStringLiteral("Test"),
                {},
                QStringLiteral("message"),
            });
        }
        QCOMPARE(model.rowCount(), 2000);
        QCOMPARE(model.entry_at(0)->timestamp, QStringLiteral("1"));
        QCOMPARE(inserted.count(), 2001);
        QVERIFY(model.entry_at(-1) == nullptr);
        QVERIFY(model.entry_at(2000) == nullptr);

        QSignalSpy reset{&model, &QAbstractItemModel::modelReset};
        model.clear();
        QCOMPARE(model.rowCount(), 0);
        QCOMPARE(reset.count(), 1);
        model.clear();
        QCOMPARE(reset.count(), 1);
    }

    void exports_csv_and_plain_text_with_safe_quoting()
    {
        ConsoleModel model;
        ConsoleEntry entry{
            QStringLiteral("12:00:00"),
            QStringLiteral("Info"),
            QStringLiteral("Importer, \"Mesh\""),
            QStringLiteral("C:/Game\tAssets"),
            QStringLiteral("line 1\n\"quoted\", line 2"),
        };
        entry.worker = QStringLiteral("preview\tworker");
        entry.session = QStringLiteral("session-1");
        entry.correlation_id = QStringLiteral("corr\nline");
        model.append(entry);

        const auto plain = model.copy_plain_text();
        QVERIFY(plain.startsWith(QStringLiteral("\"12:00:00\"\t\"Info\"")));
        QVERIFY(plain.contains(QStringLiteral("\"Importer, \\\"Mesh\\\"\"")));
        QVERIFY(plain.contains(QStringLiteral("preview\\tworker")));
        QVERIFY(plain.contains(QStringLiteral("corr\\nline")));
        QVERIFY(plain.contains(QStringLiteral("line 1\\n\\\"quoted\\\", line 2")));
        QCOMPARE(plain.count(QLatin1Char('\n')), 0);

        const auto csv = model.export_csv();
        QVERIFY(csv.startsWith(
            QStringLiteral("\"Time\",\"Severity\",\"Subsystem\",\"Worker\",")));
        QVERIFY(csv.contains(QStringLiteral("\"Importer, \"\"Mesh\"\"\"")));
        QVERIFY(csv.contains(QStringLiteral("\"line 1\n\"\"quoted\"\", line 2\"")));
        QCOMPARE(model.export_csv({0, 0, -1, 50}, false), model.export_csv({0}, false));

        ConsoleModel empty;
        QCOMPARE(empty.copy_plain_text(), QString{});
        QVERIFY(empty.export_csv().startsWith(QStringLiteral("\"Time\"")));
        QCOMPARE(empty.export_csv({}, false), QString{});
    }
};

QTEST_APPLESS_MAIN(ConsoleModelTests)
#include "ConsoleModelTests.moc"
