#include "AssetPreviewService.h"

#include "ProjectIndexService.h"

#include <QByteArrayView>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest/QTest>

#include <algorithm>

namespace
{
bool write_bytes(const QString& path, const QByteArray& bytes)
{
    if (!QDir{}.mkpath(QFileInfo{path}.absolutePath()))
    {
        return false;
    }
    QFile file{path};
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate)
        && file.write(bytes) == bytes.size();
}

bool write_image(const QString& path, const QColor& color)
{
    if (!QDir{}.mkpath(QFileInfo{path}.absolutePath()))
    {
        return false;
    }
    QImage image{32, 24, QImage::Format_ARGB32};
    image.fill(color);
    return image.save(path, "PNG");
}

QByteArray file_fingerprint(const QStringList& paths)
{
    QCryptographicHash hash{QCryptographicHash::Sha256};
    auto sorted = paths;
    sorted.sort();
    for (const auto& path : sorted)
    {
        QFile file{path};
        if (file.open(QIODevice::ReadOnly))
        {
            const auto bytes = file.readAll();
            hash.addData(QByteArrayView{path.toUtf8()});
            hash.addData(QByteArrayView{"\0", 1});
            hash.addData(QByteArrayView{bytes});
            hash.addData(QByteArrayView{"\0", 1});
        }
    }
    return hash.result();
}

QByteArray image_fingerprint(const QImage& image)
{
    QCryptographicHash hash{QCryptographicHash::Sha256};
    hash.addData(QByteArrayView{
        reinterpret_cast<const char*>(image.constBits()),
        image.sizeInBytes()});
    return hash.result();
}

ProjectIndexEntry entry_for(
    const QString& metadata_path,
    const QString& asset_id,
    const QString& type,
    const QString& source,
    const QString& resolved_source = {})
{
    ProjectIndexEntry entry;
    entry.kind = ProjectIndexEntryKind::asset;
    entry.id = asset_id;
    entry.absolute_path = metadata_path;
    entry.logical_path = QFileInfo{metadata_path}.fileName();
    entry.asset_type = type;
    entry.source = source;
    entry.resolved_source_path = resolved_source;
    entry.structurally_valid = true;
    return entry;
}

AssetPreviewResult take_preview(QSignalSpy& spy)
{
    const auto arguments = spy.takeFirst();
    return qvariant_cast<AssetPreviewResult>(arguments.constFirst());
}

AssetPreviewDiagnostic take_diagnostic(QSignalSpy& spy)
{
    const auto arguments = spy.takeFirst();
    return qvariant_cast<AssetPreviewDiagnostic>(arguments.constFirst());
}
}

class AssetPreviewServiceTests final : public QObject
{
    Q_OBJECT

private slots:
    void asynchronous_sprite_mesh_and_material_previews_are_distinct()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QDir root{directory.path()};
        const auto source_path = root.filePath(QStringLiteral("sprite.png"));
        QVERIFY(write_image(source_path, QColor{220, 50, 90}));

        const auto sprite_path = root.filePath(QStringLiteral("sprite.dpeasset"));
        const auto mesh_path = root.filePath(QStringLiteral("mesh.dpeasset"));
        const auto material_path = root.filePath(QStringLiteral("material.dpeasset"));
        QVERIFY(write_bytes(sprite_path, QByteArrayLiteral("{\"format\":\"dpe.asset\",\"kind\":\"sprite\"}")));
        QVERIFY(write_bytes(mesh_path, QByteArrayLiteral("{\"format\":\"dpe.asset\",\"kind\":\"mesh\"}")));
        QVERIFY(write_bytes(material_path, QByteArrayLiteral("{\"format\":\"dpe.asset\",\"kind\":\"material\"}")));

        AssetPreviewService service;
        service.set_project_generation(7);
        QSignalSpy ready{&service, &AssetPreviewService::previewReady};
        QSignalSpy diagnostics{&service, &AssetPreviewService::previewDiagnostic};
        QVERIFY(ready.isValid());
        QVERIFY(diagnostics.isValid());

        static_cast<void>(service.request_preview(entry_for(
            sprite_path,
            QStringLiteral("sprite-id"),
            QStringLiteral("sprite"),
            QStringLiteral("sprite.png"),
            source_path)));
        static_cast<void>(service.request_preview(entry_for(
            mesh_path,
            QStringLiteral("mesh-id"),
            QStringLiteral("static-mesh"),
            QStringLiteral("builtin://unit-cube"))));
        static_cast<void>(service.request_preview(entry_for(
            material_path,
            QStringLiteral("material-id"),
            QStringLiteral("material"),
            QStringLiteral("builtin://default-material"))));

        QCOMPARE(ready.count(), 0);
        QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 3, 5000);
        QCOMPARE(diagnostics.count(), 0);

        QHash<QString, AssetPreviewResult> results;
        while (!ready.isEmpty())
        {
            const auto result = take_preview(ready);
            results.insert(result.asset_id, result);
        }
        QCOMPARE(results.size(), 3);
        const auto sprite = results.value(QStringLiteral("sprite-id"));
        const auto mesh = results.value(QStringLiteral("mesh-id"));
        const auto material = results.value(QStringLiteral("material-id"));
        QCOMPARE(sprite.project_generation, quint64{7});
        QCOMPARE(sprite.kind, AssetPreviewKind::sprite);
        QCOMPARE(mesh.kind, AssetPreviewKind::mesh);
        QCOMPARE(material.kind, AssetPreviewKind::material);
        QVERIFY(!sprite.image.isNull());
        QVERIFY(!mesh.image.isNull());
        QVERIFY(!material.image.isNull());
        QVERIFY(!sprite.icon.isNull());
        QVERIFY(!mesh.icon.isNull());
        QVERIFY(!material.icon.isNull());
        QVERIFY(image_fingerprint(sprite.image) != image_fingerprint(mesh.image));
        QVERIFY(image_fingerprint(mesh.image) != image_fingerprint(material.image));
        QVERIFY(image_fingerprint(sprite.image) != image_fingerprint(material.image));
    }

    void invalid_and_missing_assets_emit_structured_diagnostics()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QDir root{directory.path()};
        const auto metadata = root.filePath(QStringLiteral("missing-source.dpeasset"));
        QVERIFY(write_bytes(metadata, QByteArrayLiteral("{}")));

        AssetPreviewService service;
        service.set_project_generation(3);
        QSignalSpy ready{&service, &AssetPreviewService::previewReady};
        QSignalSpy diagnostics{&service, &AssetPreviewService::previewDiagnostic};
        static_cast<void>(service.request_preview(AssetPreviewRequest{
            QStringLiteral("missing-metadata"),
            root.filePath(QStringLiteral("does-not-exist.dpeasset")),
            QStringLiteral("sprite"),
            QStringLiteral("builtin://sprite"),
            QString{},
        }));
        static_cast<void>(service.request_preview(entry_for(
            metadata,
            QStringLiteral("missing-source"),
            QStringLiteral("sprite"),
            QStringLiteral("does-not-exist.png"))));
        static_cast<void>(service.request_preview(entry_for(
            metadata,
            QStringLiteral("unsupported"),
            QStringLiteral("audio"),
            QStringLiteral("builtin://tone"))));

        QTRY_COMPARE_WITH_TIMEOUT(diagnostics.count(), 3, 5000);
        QCOMPARE(ready.count(), 0);
        QList<AssetPreviewDiagnosticCode> codes;
        while (!diagnostics.isEmpty())
        {
            const auto diagnostic = take_diagnostic(diagnostics);
            QCOMPARE(diagnostic.project_generation, quint64{3});
            QVERIFY(!diagnostic.message.isEmpty());
            codes.push_back(diagnostic.code);
        }
        QVERIFY(codes.contains(AssetPreviewDiagnosticCode::missing_metadata));
        QVERIFY(codes.contains(AssetPreviewDiagnosticCode::missing_source));
        QVERIFY(codes.contains(AssetPreviewDiagnosticCode::unsupported_type));
    }

    void cache_uses_content_identity_and_invalidates_after_source_change()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QDir root{directory.path()};
        const auto metadata = root.filePath(QStringLiteral("sprite.dpeasset"));
        const auto source = root.filePath(QStringLiteral("sprite.png"));
        QVERIFY(write_bytes(metadata, QByteArrayLiteral("{\"asset\":\"cache-test\"}")));
        QVERIFY(write_image(source, QColor{255, 0, 0}));
        const auto original_time = QFileInfo{source}.lastModified();
        const auto entry = entry_for(
            metadata,
            QStringLiteral("cache-sprite"),
            QStringLiteral("sprite"),
            QStringLiteral("sprite.png"),
            source);

        AssetPreviewService service;
        service.set_project_generation(1);
        QSignalSpy ready{&service, &AssetPreviewService::previewReady};
        static_cast<void>(service.request_preview(entry));
        QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 5000);
        const auto first = take_preview(ready);
        QVERIFY(!first.cache_hit);

        static_cast<void>(service.request_preview(entry));
        QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 5000);
        const auto cached = take_preview(ready);
        QVERIFY(cached.cache_hit);
        QCOMPARE(cached.content_identity, first.content_identity);
        QCOMPARE(image_fingerprint(cached.image), image_fingerprint(first.image));

        QVERIFY(write_image(source, QColor{0, 80, 255}));
        QFile source_file{source};
        QVERIFY(source_file.open(QIODevice::ReadWrite));
        QVERIFY(source_file.setFileTime(original_time, QFileDevice::FileModificationTime));
        source_file.close();

        static_cast<void>(service.request_preview(entry));
        QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 5000);
        const auto changed = take_preview(ready);
        QVERIFY(!changed.cache_hit);
        QVERIFY(changed.content_identity != first.content_identity);
        QVERIFY(image_fingerprint(changed.image) != image_fingerprint(first.image));
        QCOMPARE(service.cache_entry_count(), qsizetype{2});
    }

    void stale_project_generation_suppresses_results_and_diagnostics()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QDir root{directory.path()};
        const auto metadata = root.filePath(QStringLiteral("mesh.dpeasset"));
        QVERIFY(write_bytes(metadata, QByteArrayLiteral("{}")));
        const auto entry = entry_for(
            metadata,
            QStringLiteral("mesh"),
            QStringLiteral("static-mesh"),
            QStringLiteral("builtin://unit-cube"));

        AssetPreviewService service{nullptr, 1};
        QSignalSpy ready{&service, &AssetPreviewService::previewReady};
        QSignalSpy diagnostics{&service, &AssetPreviewService::previewDiagnostic};
        service.set_project_generation(10);
        static_cast<void>(service.request_preview(entry));
        static_cast<void>(service.request_preview(AssetPreviewRequest{
            QStringLiteral("invalid"),
            root.filePath(QStringLiteral("missing.dpeasset")),
            QStringLiteral("sprite"),
            QStringLiteral("builtin://sprite"),
            QString{},
        }));
        service.set_project_generation(11);

        QTest::qWait(250);
        QCOMPARE(ready.count(), 0);
        QCOMPARE(diagnostics.count(), 0);

        static_cast<void>(service.request_preview(entry));
        QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 5000);
        const auto current = take_preview(ready);
        QCOMPARE(current.project_generation, quint64{11});
    }

    void preview_work_is_read_only()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QDir root{directory.path()};
        const auto metadata = root.filePath(QStringLiteral("sprite.dpeasset"));
        const auto source = root.filePath(QStringLiteral("sprite.png"));
        QVERIFY(write_bytes(metadata, QByteArrayLiteral("{\"readOnly\":true}")));
        QVERIFY(write_image(source, QColor{40, 200, 120}));
        const QStringList inputs{metadata, source};
        const auto before = file_fingerprint(inputs);

        AssetPreviewService service;
        QSignalSpy ready{&service, &AssetPreviewService::previewReady};
        static_cast<void>(service.request_preview(entry_for(
            metadata,
            QStringLiteral("read-only"),
            QStringLiteral("sprite"),
            QStringLiteral("sprite.png"),
            source)));
        QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 5000);

        QCOMPARE(file_fingerprint(inputs), before);
    }
};

QTEST_MAIN(AssetPreviewServiceTests)

#include "AssetPreviewServiceTests.moc"
