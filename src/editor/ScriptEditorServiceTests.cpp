#include "ScriptEditorService.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest/QTest>

#include <filesystem>

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

std::filesystem::path filesystem_path(const QString& path)
{
#if defined(Q_OS_WIN)
    return std::filesystem::path{path.toStdWString()};
#else
    return std::filesystem::path{path.toUtf8().constData()};
#endif
}
}

class ScriptEditorServiceTests final : public QObject
{
    Q_OBJECT

private slots:
    void regenerates_complete_workspace_and_routes_documented_rider_arguments()
    {
        QTemporaryDir project;
        QVERIFY(project.isValid());
        const QDir root{project.path()};
        const auto managed = root.filePath(QStringLiteral("Components/CSharp/Player Move.cs"));
        const auto native = root.filePath(QStringLiteral("Components/Native/Spinner.cpp"));
        const auto header = root.filePath(QStringLiteral("Components/Native/Spinner.hpp"));
        const auto manifest = root.filePath(QStringLiteral("Components/Game.dpecomponents"));
        const auto contracts = root.filePath(QStringLiteral("Sdk/DragonPixel.Contracts.dll"));
        const auto rider = root.filePath(QStringLiteral("Tools/rider64.exe"));
        QVERIFY(write_file(managed, QByteArrayLiteral("public sealed class PlayerMove {} \n")));
        QVERIFY(write_file(native, QByteArrayLiteral("void update() {}\n")));
        QVERIFY(write_file(header, QByteArrayLiteral("#pragma once\n")));
        QVERIFY(write_file(manifest, QByteArrayLiteral("{}\n")));
        QVERIFY(write_file(contracts, QByteArrayLiteral("contracts")));
        QVERIFY(write_file(rider, QByteArrayLiteral("probe")));

        QVector<ScriptEditorInvocation> launched;
        ScriptEditorService service{
            [&launched](const QString& program, const QStringList& arguments,
                const QString& working_directory, QString&) {
                launched.push_back({program, arguments, working_directory});
                return true;
            }};
        ScriptEditorRequest request{
            project.path(), managed, contracts,
            {header, managed, native, managed}, {manifest}, rider, 17};
        const auto result = service.open_in_rider(request);
        QVERIFY2(result.succeeded, qPrintable(result.error));
        QCOMPARE(launched.size(), 2);
        QCOMPARE(launched.at(0).program, QFileInfo{rider}.absoluteFilePath());
        QCOMPARE(launched.at(0).arguments, QStringList{result.solution_path});
        QCOMPARE(
            launched.at(1).arguments,
            QStringList({QStringLiteral("--line"), QStringLiteral("17"),
                QFileInfo{managed}.canonicalFilePath()}));
        QCOMPARE(launched.at(0).working_directory, result.workspace_directory);
        QCOMPARE(launched.at(1).working_directory, result.workspace_directory);

        const auto project_bytes = read_file(result.project_path);
        QVERIFY(project_bytes.startsWith("<Project Sdk=\"Microsoft.NET.Sdk\">\n"));
        QVERIFY(project_bytes.contains("<Compile Include=\"../../../Components/CSharp/Player Move.cs\" />"));
        QVERIFY(project_bytes.contains("<None Include=\"../../../Components/Native/Spinner.cpp\" />"));
        QVERIFY(project_bytes.contains("<None Include=\"../../../Components/Native/Spinner.hpp\" />"));
        QVERIFY(project_bytes.contains("<None Include=\"../../../Components/Game.dpecomponents\" />"));
        QVERIFY(project_bytes.contains("DragonPixel.Contracts.dll"));
        QCOMPARE(project_bytes.count("Player Move.cs"), 1);
        QVERIFY(!project_bytes.contains('\r'));
        QVERIFY(!project_bytes.startsWith("\xEF\xBB\xBF"));
        QVERIFY(read_file(result.solution_path).contains(
            "DragonPixel.ProjectComponents.csproj"));

        const auto first_project = project_bytes;
        request.component_source_paths = {native, managed, header};
        launched.clear();
        const auto regenerated = service.open_in_rider(request);
        QVERIFY2(regenerated.succeeded, qPrintable(regenerated.error));
        QCOMPARE(read_file(regenerated.project_path), first_project);
    }

    void rejects_outside_missing_and_linked_sources_before_launch()
    {
        QTemporaryDir project;
        QTemporaryDir outside;
        QVERIFY(project.isValid() && outside.isValid());
        const auto inside = QDir{project.path()}.filePath(QStringLiteral("Components/Safe.cs"));
        const auto outside_source = QDir{outside.path()}.filePath(QStringLiteral("Outside.cs"));
        const auto contracts = QDir{project.path()}.filePath(QStringLiteral("Sdk/DragonPixel.Contracts.dll"));
        const auto rider = QDir{project.path()}.filePath(QStringLiteral("Tools/rider64.exe"));
        QVERIFY(write_file(inside, QByteArrayLiteral("class Safe {}\n")));
        QVERIFY(write_file(outside_source, QByteArrayLiteral("class Outside {}\n")));
        QVERIFY(write_file(contracts, QByteArrayLiteral("contracts")));
        QVERIFY(write_file(rider, QByteArrayLiteral("probe")));

        int launch_count{};
        ScriptEditorService service{
            [&launch_count](const QString&, const QStringList&, const QString&, QString&) {
                ++launch_count;
                return true;
            }};
        ScriptEditorRequest request{
            project.path(), outside_source, contracts, {inside}, {}, rider, 1};
        const auto escaped = service.open_in_rider(request);
        QVERIFY(!escaped.succeeded);
        QVERIFY(escaped.error.contains(QStringLiteral("outside"), Qt::CaseInsensitive));
        QCOMPARE(launch_count, 0);

        request.selected_source_path = QDir{project.path()}.filePath(
            QStringLiteral("Components/Missing.cs"));
        const auto missing = service.open_in_rider(request);
        QVERIFY(!missing.succeeded);
        QCOMPARE(launch_count, 0);

        const auto link = QDir{project.path()}.filePath(QStringLiteral("Components/Linked.cs"));
        std::error_code link_error;
        std::filesystem::create_symlink(
            filesystem_path(outside_source), filesystem_path(link), link_error);
        if (!link_error)
        {
            request.selected_source_path = link;
            request.component_source_paths = {link};
            const auto linked = service.open_in_rider(request);
            QVERIFY(!linked.succeeded);
            QVERIFY(linked.error.contains(QStringLiteral("link"), Qt::CaseInsensitive));
            QCOMPARE(launch_count, 0);
        }
    }

    void explicit_environment_configuration_wins_discovery()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const auto configured = QDir{temporary.path()}.filePath(QStringLiteral("rider64.exe"));
        QVERIFY(write_file(configured, QByteArrayLiteral("probe")));
        const auto previous = qgetenv("DPE_RIDER_EXECUTABLE");
        QVERIFY(qputenv("DPE_RIDER_EXECUTABLE", configured.toUtf8()));
        QCOMPARE(
            ScriptEditorService::find_rider_executable(),
            QFileInfo{configured}.absoluteFilePath());
        if (previous.isNull())
        {
            qunsetenv("DPE_RIDER_EXECUTABLE");
        }
        else
        {
            QVERIFY(qputenv("DPE_RIDER_EXECUTABLE", previous));
        }
    }
};

QTEST_GUILESS_MAIN(ScriptEditorServiceTests)

#include "ScriptEditorServiceTests.moc"
