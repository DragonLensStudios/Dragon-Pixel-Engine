#include "ProjectLifecycleService.h"

#include "ProjectIndexService.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QtTest/QTest>

namespace
{
QJsonObject read_object(const QString& path)
{
    QFile input{path};
    if (!input.open(QIODevice::ReadOnly))
    {
        return {};
    }
    return QJsonDocument::fromJson(input.readAll()).object();
}

ProjectLifecycleService deterministic_service(
    const QString& recent_store,
    int* sequence = nullptr)
{
    if (sequence)
    {
        return ProjectLifecycleService{recent_store, [sequence]() {
            ++(*sequence);
            return QStringLiteral("00000000-0000-4000-8000-%1")
                .arg(*sequence, 12, 10, QLatin1Char{'0'});
        }};
    }
    auto owned_sequence = std::make_shared<int>();
    return ProjectLifecycleService{recent_store, [owned_sequence]() {
        ++(*owned_sequence);
        return QStringLiteral("00000000-0000-4000-8000-%1")
            .arg(*owned_sequence, 12, 10, QLatin1Char{'0'});
    }};
}

QString template_path(const QString& name)
{
    return QDir{QString::fromUtf8(DPE_PROJECT_TEMPLATES_ROOT)}
        .filePath(name + QStringLiteral("/DragonPixelTemplate.json"));
}
} // namespace

class ProjectLifecycleServiceTests final : public QObject
{
    Q_OBJECT

private slots:
    void discovers_declarative_templates()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        auto service = deterministic_service(QDir{temp.path()}.filePath(QStringLiteral("recent.ini")));
        QVector<ProjectLifecycleDiagnostic> diagnostics;
        const auto templates = service.discover_templates(
            QString::fromUtf8(DPE_PROJECT_TEMPLATES_ROOT), &diagnostics);
        QCOMPARE(diagnostics.size(), 0);
        QCOMPARE(templates.size(), 2);
        QCOMPARE(templates.at(0).name, QStringLiteral("Minimal 2D"));
        QCOMPARE(templates.at(0).kind, ProjectTemplateKind::two_d);
        QCOMPARE(templates.at(1).name, QStringLiteral("Minimal 3D"));
        QCOMPARE(templates.at(1).kind, ProjectTemplateKind::three_d);
    }

    void creates_minimal_2d_project_and_indexes_v4()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        int sequence = 0;
        const auto recent = QDir{temp.path()}.filePath(QStringLiteral("recent.ini"));
        auto service = deterministic_service(recent, &sequence);
        const auto destination = QDir{temp.path()}.filePath(QStringLiteral("Clean2D"));
        const auto result = service.create_project({
            template_path(QStringLiteral("Minimal2D")), QStringLiteral("Clean 2D"), destination});
        QVERIFY2(result.succeeded, result.diagnostics.isEmpty()
            ? "unknown lifecycle failure"
            : qPrintable(result.diagnostics.constFirst().message));
        QVERIFY(result.staging_path.isEmpty());
        QVERIFY(QFileInfo{result.project_manifest_path}.isFile());
        QVERIFY(!QFileInfo{QDir{destination}.filePath(
            QStringLiteral(".dpe-project-creation-recovery.json"))}.exists());

        ProjectIndexService index;
        const auto candidate = index.build_candidate(result.project_manifest_path);
        QVERIFY(candidate.succeeded());
        QCOMPARE(candidate.candidate->source_format_version, 4);
        QCOMPARE(candidate.candidate->format_version, 4);
        QCOMPARE(candidate.candidate->name, QStringLiteral("Clean 2D"));

        const auto scene = read_object(result.scene_path);
        QCOMPARE(scene.value(QStringLiteral("entities")).toArray().size(), 1);
        const auto camera = scene.value(QStringLiteral("entities")).toArray().at(0).toObject();
        QCOMPARE(camera.value(QStringLiteral("name")).toString(), QStringLiteral("Main Camera"));
        const auto components = camera.value(QStringLiteral("components")).toArray();
        QCOMPARE(components.size(), 2);
        QCOMPARE(components.at(1).toObject().value(QStringLiteral("properties")).toObject()
                     .value(QStringLiteral("dpe.camera.projection")).toString(),
            QStringLiteral("orthographic"));
        QCOMPARE(service.recent_projects(), QStringList{result.project_manifest_path});
    }

    void creates_minimal_3d_project_with_light()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        int sequence = 100;
        auto service = deterministic_service(
            QDir{temp.path()}.filePath(QStringLiteral("recent.ini")), &sequence);
        const auto result = service.create_project({
            template_path(QStringLiteral("Minimal3D")),
            QStringLiteral("Clean 3D"),
            QDir{temp.path()}.filePath(QStringLiteral("Clean3D"))});
        QVERIFY(result.succeeded);
        const auto entities = read_object(result.scene_path)
                                  .value(QStringLiteral("entities")).toArray();
        QCOMPARE(entities.size(), 2);
        QCOMPARE(entities.at(0).toObject().value(QStringLiteral("name")).toString(),
            QStringLiteral("Main Camera"));
        QCOMPARE(entities.at(1).toObject().value(QStringLiteral("name")).toString(),
            QStringLiteral("Directional Light"));
        QCOMPARE(entities.at(1).toObject().value(QStringLiteral("components")).toArray().size(), 2);
    }

    void deterministic_ids_produce_identical_content()
    {
        QTemporaryDir left;
        QTemporaryDir right;
        QVERIFY(left.isValid());
        QVERIFY(right.isValid());
        int left_sequence = 0;
        int right_sequence = 0;
        auto left_service = deterministic_service(
            QDir{left.path()}.filePath(QStringLiteral("recent.ini")), &left_sequence);
        auto right_service = deterministic_service(
            QDir{right.path()}.filePath(QStringLiteral("recent.ini")), &right_sequence);
        const auto left_result = left_service.create_project({
            template_path(QStringLiteral("Minimal2D")), QStringLiteral("Same"),
            QDir{left.path()}.filePath(QStringLiteral("Project"))});
        const auto right_result = right_service.create_project({
            template_path(QStringLiteral("Minimal2D")), QStringLiteral("Same"),
            QDir{right.path()}.filePath(QStringLiteral("Project"))});
        QVERIFY(left_result.succeeded);
        QVERIFY(right_result.succeeded);
        QFile left_manifest{left_result.project_manifest_path};
        QFile right_manifest{right_result.project_manifest_path};
        QVERIFY(left_manifest.open(QIODevice::ReadOnly));
        QVERIFY(right_manifest.open(QIODevice::ReadOnly));
        QCOMPARE(left_manifest.readAll(), right_manifest.readAll());
        QFile left_scene{left_result.scene_path};
        QFile right_scene{right_result.scene_path};
        QVERIFY(left_scene.open(QIODevice::ReadOnly));
        QVERIFY(right_scene.open(QIODevice::ReadOnly));
        QCOMPARE(left_scene.readAll(), right_scene.readAll());
    }

    void rejects_existing_destination_without_writes()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const auto destination = QDir{temp.path()}.filePath(QStringLiteral("Existing"));
        QVERIFY(QDir{}.mkdir(destination));
        QFile sentinel{QDir{destination}.filePath(QStringLiteral("keep.txt"))};
        QVERIFY(sentinel.open(QIODevice::WriteOnly));
        QCOMPARE(sentinel.write("unchanged"), 9);
        sentinel.close();
        auto service = deterministic_service(QDir{temp.path()}.filePath(QStringLiteral("recent.ini")));
        const auto result = service.create_project({
            template_path(QStringLiteral("Minimal2D")), QStringLiteral("Nope"), destination});
        QVERIFY(!result.succeeded);
        QVERIFY(!result.diagnostics.isEmpty());
        QVERIFY(sentinel.open(QIODevice::ReadOnly));
        QCOMPARE(sentinel.readAll(), QByteArray{"unchanged"});
    }

    void cancellation_leaves_no_destination_or_staging()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        auto service = deterministic_service(QDir{temp.path()}.filePath(QStringLiteral("recent.ini")));
        auto cancellation = std::make_shared<ProjectLifecycleCancellation>();
        cancellation->cancel();
        const auto destination = QDir{temp.path()}.filePath(QStringLiteral("Cancelled"));
        const auto result = service.create_project({
            template_path(QStringLiteral("Minimal2D")), QStringLiteral("Cancelled"), destination},
            cancellation);
        QVERIFY(!result.succeeded);
        QVERIFY(result.cancelled);
        QVERIFY(!QFileInfo::exists(destination));
        QVERIFY(service.recoverable_staging_paths(temp.path()).isEmpty());
    }

    void injected_failure_is_recoverable_and_discardable()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        auto service = deterministic_service(QDir{temp.path()}.filePath(QStringLiteral("recent.ini")));
        const auto destination = QDir{temp.path()}.filePath(QStringLiteral("Faulted"));
        const auto result = service.create_project({
            template_path(QStringLiteral("Minimal3D")), QStringLiteral("Faulted"), destination},
            {}, ProjectLifecycleFault::after_generation);
        QVERIFY(!result.succeeded);
        QVERIFY(!QFileInfo::exists(destination));
        QVERIFY(QFileInfo{result.staging_path}.isDir());
        QCOMPARE(service.recoverable_staging_paths(temp.path()), QStringList{result.staging_path});
        QVERIFY(service.discard_recoverable_staging(result.staging_path));
        QVERIFY(!QFileInfo::exists(result.staging_path));
    }

    void creates_clean_2d_and_3d_scenes_beneath_scene_root()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        auto service = deterministic_service(QDir{temp.path()}.filePath(QStringLiteral("recent.ini")));
        const auto project = service.create_project({
            template_path(QStringLiteral("Minimal2D")), QStringLiteral("Scenes"),
            QDir{temp.path()}.filePath(QStringLiteral("Project"))});
        QVERIFY(project.succeeded);
        const auto two_d = service.create_clean_scene({
            project.project_manifest_path, QStringLiteral("Empty 2D"),
            QStringLiteral("Scenes/Empty2D.dpescene"), ProjectTemplateKind::two_d});
        QVERIFY(two_d.succeeded);
        QCOMPARE(read_object(two_d.scene_path).value(QStringLiteral("entities")).toArray().size(), 1);
        const auto three_d = service.create_clean_scene({
            project.project_manifest_path, QStringLiteral("Empty 3D"),
            QStringLiteral("Scenes/Empty3D.dpescene"), ProjectTemplateKind::three_d});
        QVERIFY(three_d.succeeded);
        QCOMPARE(read_object(three_d.scene_path).value(QStringLiteral("entities")).toArray().size(), 2);

        const auto collision = service.create_clean_scene({
            project.project_manifest_path, QStringLiteral("Again"),
            QStringLiteral("Scenes/Empty2D.dpescene"), ProjectTemplateKind::two_d});
        QVERIFY(!collision.succeeded);
        const auto escape = service.create_clean_scene({
            project.project_manifest_path, QStringLiteral("Escape"),
            QStringLiteral("Elsewhere/Escape.dpescene"), ProjectTemplateKind::two_d});
        QVERIFY(!escape.succeeded);
    }
};

QTEST_APPLESS_MAIN(ProjectLifecycleServiceTests)

#include "ProjectLifecycleServiceTests.moc"
