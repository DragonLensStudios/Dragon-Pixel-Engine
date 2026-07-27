#include "ScriptEditorService.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QHash>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSaveFile>
#include <QStandardPaths>

#include <algorithm>
#include <utility>

namespace
{
QString normalized_absolute_path(const QString& path)
{
    return QDir::cleanPath(QFileInfo{path}.absoluteFilePath());
}

QString path_key(const QString& path)
{
    auto key = QDir::fromNativeSeparators(normalized_absolute_path(path)).normalized(
        QString::NormalizationForm_C);
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
    key = key.toCaseFolded();
#endif
    return key;
}

bool relative_is_contained(const QString& relative)
{
    const auto portable = QDir::fromNativeSeparators(QDir::cleanPath(relative));
    return !portable.isEmpty() && portable != QStringLiteral(".")
        && portable != QStringLiteral("..") && !portable.startsWith(QStringLiteral("../"))
        && !QDir::isAbsolutePath(portable);
}

bool is_link_or_junction(const QFileInfo& info)
{
    return info.isSymLink() || info.isJunction();
}

bool validate_root(const QString& requested_root, QString& root, QString& error)
{
    root = normalized_absolute_path(requested_root);
    const QFileInfo root_info{root};
    if (!root_info.exists() || !root_info.isDir() || is_link_or_junction(root_info))
    {
        error = QStringLiteral("The project root is unavailable or is a link/reparse point: %1")
                    .arg(root);
        return false;
    }
    const auto canonical = QDir::cleanPath(root_info.canonicalFilePath());
    if (canonical.isEmpty() || path_key(canonical) != path_key(root))
    {
        error = QStringLiteral("The project root did not resolve to one stable directory: %1")
                    .arg(root);
        return false;
    }
    root = canonical;
    return true;
}

bool validate_contained_file(
    const QString& root,
    const QString& requested_path,
    QString& validated,
    QString& error)
{
    const auto absolute = normalized_absolute_path(requested_path);
    const auto relative = QDir{root}.relativeFilePath(absolute);
    if (!relative_is_contained(relative))
    {
        error = QStringLiteral("The source path is outside the project root: %1").arg(absolute);
        return false;
    }

    auto current = root;
    const auto segments = QDir::fromNativeSeparators(relative).split(
        QLatin1Char('/'), Qt::SkipEmptyParts);
    for (const auto& segment : segments)
    {
        current = QDir{current}.filePath(segment);
        const QFileInfo info{current};
        if (!info.exists() || is_link_or_junction(info))
        {
            error = QStringLiteral("The source path is missing or contains a link/reparse point: %1")
                        .arg(current);
            return false;
        }
    }

    const QFileInfo file_info{absolute};
    const auto canonical = QDir::cleanPath(file_info.canonicalFilePath());
    if (!file_info.isFile() || canonical.isEmpty()
        || path_key(canonical) != path_key(absolute)
        || !relative_is_contained(QDir{root}.relativeFilePath(canonical)))
    {
        error = QStringLiteral("The source path is not one stable contained regular file: %1")
                    .arg(absolute);
        return false;
    }
    validated = canonical;
    return true;
}

bool ensure_contained_directory(
    const QString& root,
    const QString& relative,
    QString& directory,
    QString& error)
{
    if (!relative_is_contained(relative))
    {
        error = QStringLiteral("The IDE workspace path is not a portable contained path.");
        return false;
    }
    auto current = root;
    for (const auto& segment : QDir::fromNativeSeparators(relative).split(
             QLatin1Char('/'), Qt::SkipEmptyParts))
    {
        current = QDir{current}.filePath(segment);
        QFileInfo info{current};
        if (info.exists())
        {
            if (!info.isDir() || is_link_or_junction(info))
            {
                error = QStringLiteral("The IDE workspace path contains a non-directory or link: %1")
                            .arg(current);
                return false;
            }
        }
        else if (!QDir{}.mkdir(current))
        {
            error = QStringLiteral("Could not create the IDE workspace directory: %1").arg(current);
            return false;
        }
        info.refresh();
        if (!info.isDir() || is_link_or_junction(info))
        {
            error = QStringLiteral("The IDE workspace directory was not stable after creation: %1")
                        .arg(current);
            return false;
        }
    }
    directory = normalized_absolute_path(current);
    return true;
}

QString xml_escape(QString value)
{
    value.replace(QLatin1Char('&'), QStringLiteral("&amp;"));
    value.replace(QLatin1Char('<'), QStringLiteral("&lt;"));
    value.replace(QLatin1Char('>'), QStringLiteral("&gt;"));
    value.replace(QLatin1Char('"'), QStringLiteral("&quot;"));
    value.replace(QLatin1Char('\''), QStringLiteral("&apos;"));
    return value;
}

bool write_atomic(const QString& path, const QByteArray& bytes, QString& error)
{
    const QFileInfo existing{path};
    if (existing.exists() && (!existing.isFile() || is_link_or_junction(existing)))
    {
        error = QStringLiteral("Refusing to replace an unsafe IDE workspace entry: %1").arg(path);
        return false;
    }
    QSaveFile file{path};
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
    {
        error = QStringLiteral("Could not atomically write %1: %2").arg(path, file.errorString());
        return false;
    }
    return true;
}

QString deterministic_project_guid(const QString& root)
{
    auto bytes = QCryptographicHash::hash(
        QDir::fromNativeSeparators(root).toUtf8(), QCryptographicHash::Sha256).left(16);
    bytes[6] = static_cast<char>((static_cast<unsigned char>(bytes[6]) & 0x0fU) | 0x40U);
    bytes[8] = static_cast<char>((static_cast<unsigned char>(bytes[8]) & 0x3fU) | 0x80U);
    const auto hex = bytes.toHex().toUpper();
    return QStringLiteral("{%1-%2-%3-%4-%5}")
        .arg(QString::fromLatin1(hex.mid(0, 8)),
             QString::fromLatin1(hex.mid(8, 4)),
             QString::fromLatin1(hex.mid(12, 4)),
             QString::fromLatin1(hex.mid(16, 4)),
             QString::fromLatin1(hex.mid(20, 12)));
}

QStringList normalized_unique_files(
    const QString& root,
    const QStringList& paths,
    bool sources,
    QString& error)
{
    QStringList result;
    QHash<QString, QString> keys;
    for (const auto& requested : paths)
    {
        if (sources && !ScriptEditorService::is_component_source_path(requested))
        {
            error = QStringLiteral("Unsupported component source suffix: %1").arg(requested);
            return {};
        }
        QString validated;
        if (!validate_contained_file(root, requested, validated, error))
        {
            return {};
        }
        const auto key = path_key(validated);
        if (!keys.contains(key))
        {
            keys.insert(key, validated);
            result.push_back(validated);
        }
    }
    std::sort(result.begin(), result.end(), [](const auto& left, const auto& right) {
        const auto folded = QString::compare(left.toCaseFolded(), right.toCaseFolded());
        return folded == 0 ? left < right : folded < 0;
    });
    return result;
}

QByteArray project_contents(
    const QString& workspace,
    const QString& contracts,
    const QStringList& sources,
    const QStringList& manifests)
{
    QString text = QStringLiteral(
        "<Project Sdk=\"Microsoft.NET.Sdk\">\n"
        "  <PropertyGroup>\n"
        "    <TargetFramework>net10.0</TargetFramework>\n"
        "    <LangVersion>14.0</LangVersion>\n"
        "    <Nullable>enable</Nullable>\n"
        "    <ImplicitUsings>enable</ImplicitUsings>\n"
        "    <EnableDefaultCompileItems>false</EnableDefaultCompileItems>\n"
        "    <EnableDefaultNoneItems>false</EnableDefaultNoneItems>\n"
        "    <DragonPixelGeneratedIdeWorkspace>true</DragonPixelGeneratedIdeWorkspace>\n"
        "  </PropertyGroup>\n");

    const auto has_csharp = std::any_of(sources.cbegin(), sources.cend(), [](const auto& path) {
        return QFileInfo{path}.suffix().compare(QStringLiteral("cs"), Qt::CaseInsensitive) == 0;
    });
    if (has_csharp)
    {
        text += QStringLiteral(
            "  <ItemGroup>\n"
            "    <Reference Include=\"DragonPixel.Contracts\">\n"
            "      <HintPath>%1</HintPath>\n"
            "      <Private>false</Private>\n"
            "    </Reference>\n"
            "  </ItemGroup>\n")
                    .arg(xml_escape(QDir::fromNativeSeparators(contracts)));
    }

    text += QStringLiteral("  <ItemGroup>\n");
    for (const auto& source : sources)
    {
        const auto include = xml_escape(QDir::fromNativeSeparators(
            QDir{workspace}.relativeFilePath(source)));
        const auto item = QFileInfo{source}.suffix().compare(
                              QStringLiteral("cs"), Qt::CaseInsensitive) == 0
            ? QStringLiteral("Compile")
            : QStringLiteral("None");
        text += QStringLiteral("    <%1 Include=\"%2\" />\n").arg(item, include);
    }
    for (const auto& manifest : manifests)
    {
        text += QStringLiteral("    <None Include=\"%1\" />\n")
                    .arg(xml_escape(QDir::fromNativeSeparators(
                        QDir{workspace}.relativeFilePath(manifest))));
    }
    text += QStringLiteral("  </ItemGroup>\n</Project>\n");
    return text.toUtf8();
}

QByteArray solution_contents(const QString& project_guid)
{
    return QStringLiteral(
        "Microsoft Visual Studio Solution File, Format Version 12.00\n"
        "# Visual Studio Version 17\n"
        "VisualStudioVersion = 17.0.31903.59\n"
        "MinimumVisualStudioVersion = 10.0.40219.1\n"
        "Project(\"{FAE04EC0-301F-11D3-BF4B-00C04F79EFBC}\") = \"DragonPixel.ProjectComponents\", \"DragonPixel.ProjectComponents.csproj\", \"%1\"\n"
        "EndProject\n"
        "Global\n"
        "\tGlobalSection(SolutionConfigurationPlatforms) = preSolution\n"
        "\t\tDebug|Any CPU = Debug|Any CPU\n"
        "\t\tRelease|Any CPU = Release|Any CPU\n"
        "\tEndGlobalSection\n"
        "\tGlobalSection(ProjectConfigurationPlatforms) = postSolution\n"
        "\t\t%1.Debug|Any CPU.ActiveCfg = Debug|Any CPU\n"
        "\t\t%1.Debug|Any CPU.Build.0 = Debug|Any CPU\n"
        "\t\t%1.Release|Any CPU.ActiveCfg = Release|Any CPU\n"
        "\t\t%1.Release|Any CPU.Build.0 = Release|Any CPU\n"
        "\tEndGlobalSection\n"
        "EndGlobal\n")
        .arg(project_guid)
        .toUtf8();
}

QString first_existing_executable(const QStringList& paths)
{
    for (const auto& path : paths)
    {
        const QFileInfo info{path};
        if (info.exists() && info.isFile())
        {
            return normalized_absolute_path(path);
        }
    }
    return {};
}

QString newest_rider_below(const QString& root, const QString& file_name)
{
    if (!QDir{root}.exists()) return {};
    QStringList candidates;
    QDirIterator iterator{root, QStringList{file_name}, QDir::Files, QDirIterator::Subdirectories};
    while (iterator.hasNext()) candidates.push_back(iterator.next());
    std::sort(candidates.begin(), candidates.end(), std::greater<QString>{});
    return candidates.isEmpty() ? QString{} : normalized_absolute_path(candidates.front());
}
}

ScriptEditorService::ScriptEditorService(ProcessLauncher launcher)
    : launcher_{std::move(launcher)}
{
    if (!launcher_)
    {
        launcher_ = [](const QString& program, const QStringList& arguments,
                        const QString& working_directory, QString& error) {
            qint64 process_id{};
            if (QProcess::startDetached(program, arguments, working_directory, &process_id))
            {
                return true;
            }
            error = QStringLiteral("Could not start the detached Rider process: %1").arg(program);
            return false;
        };
    }
}

bool ScriptEditorService::is_component_source_path(const QString& path)
{
    static const QStringList suffixes{
        QStringLiteral("cs"), QStringLiteral("cpp"), QStringLiteral("cc"),
        QStringLiteral("cxx"), QStringLiteral("h"), QStringLiteral("hpp")};
    return suffixes.contains(QFileInfo{path}.suffix(), Qt::CaseInsensitive);
}

QString ScriptEditorService::find_rider_executable()
{
    const auto configured = QProcessEnvironment::systemEnvironment()
                                .value(QStringLiteral("DPE_RIDER_EXECUTABLE"))
                                .trimmed();
    if (!configured.isEmpty() && QFileInfo{configured}.isFile())
    {
        return normalized_absolute_path(configured);
    }

    for (const auto& command : {
#if defined(Q_OS_WIN)
             QStringLiteral("rider64.exe"), QStringLiteral("rider.exe"),
#else
             QStringLiteral("rider"), QStringLiteral("rider.sh"),
#endif
         })
    {
        const auto discovered = QStandardPaths::findExecutable(command);
        if (!discovered.isEmpty()) return normalized_absolute_path(discovered);
    }

#if defined(Q_OS_WIN)
    const auto local_app_data = qEnvironmentVariable("LOCALAPPDATA");
    const auto direct = first_existing_executable({
        QDir{local_app_data}.filePath(QStringLiteral("Programs/Rider/bin/rider64.exe")),
        QDir{qEnvironmentVariable("ProgramFiles")}.filePath(
            QStringLiteral("JetBrains/JetBrains Rider/bin/rider64.exe")),
    });
    if (!direct.isEmpty()) return direct;
    for (const auto& root : {
             QDir{local_app_data}.filePath(QStringLiteral("JetBrains/Toolbox/apps/Rider")),
             QDir{local_app_data}.filePath(QStringLiteral("JetBrains/Installations")),
             QDir{qEnvironmentVariable("ProgramFiles")}.filePath(QStringLiteral("JetBrains")),
         })
    {
        const auto discovered = newest_rider_below(root, QStringLiteral("rider64.exe"));
        if (!discovered.isEmpty()) return discovered;
    }
#elif defined(Q_OS_MACOS)
    const auto direct = first_existing_executable({
        QStringLiteral("/Applications/Rider.app/Contents/MacOS/rider"),
        QDir{QDir::homePath()}.filePath(QStringLiteral("Applications/Rider.app/Contents/MacOS/rider")),
    });
    if (!direct.isEmpty()) return direct;
    const auto discovered = newest_rider_below(
        QDir{QDir::homePath()}.filePath(QStringLiteral("Library/Application Support/JetBrains/Toolbox/apps/Rider")),
        QStringLiteral("rider"));
    if (!discovered.isEmpty()) return discovered;
#else
    const auto direct = first_existing_executable({
        QDir{QDir::homePath()}.filePath(QStringLiteral(".local/share/JetBrains/Toolbox/apps/Rider/bin/rider.sh")),
        QStringLiteral("/opt/jetbrains/rider/bin/rider.sh"),
        QStringLiteral("/opt/rider/bin/rider.sh"),
    });
    if (!direct.isEmpty()) return direct;
    const auto discovered = newest_rider_below(
        QDir{QDir::homePath()}.filePath(QStringLiteral(".local/share/JetBrains/Toolbox/apps/Rider")),
        QStringLiteral("rider.sh"));
    if (!discovered.isEmpty()) return discovered;
#endif
    return {};
}

ScriptEditorResult ScriptEditorService::open_in_rider(const ScriptEditorRequest& request) const
{
    ScriptEditorResult result;
    QString root;
    if (!validate_root(request.project_root, root, result.error)) return result;

    QString selected;
    if (!is_component_source_path(request.selected_source_path)
        || !validate_contained_file(root, request.selected_source_path, selected, result.error))
    {
        if (result.error.isEmpty())
        {
            result.error = QStringLiteral("Select a supported C# or C++ component source file.");
        }
        return result;
    }

    auto source_paths = request.component_source_paths;
    source_paths.push_back(selected);
    const auto sources = normalized_unique_files(root, source_paths, true, result.error);
    if (!result.error.isEmpty() || sources.isEmpty()) return result;
    const auto manifests = normalized_unique_files(
        root, request.component_manifest_paths, false, result.error);
    if (!result.error.isEmpty()) return result;

    const auto has_csharp = std::any_of(sources.cbegin(), sources.cend(), [](const auto& path) {
        return QFileInfo{path}.suffix().compare(QStringLiteral("cs"), Qt::CaseInsensitive) == 0;
    });
    auto contracts = normalized_absolute_path(request.contracts_assembly_path);
    if (has_csharp && (!QFileInfo{contracts}.exists() || !QFileInfo{contracts}.isFile()))
    {
        result.error = QStringLiteral("DragonPixel.Contracts is required to prepare the Rider solution: %1")
                           .arg(contracts);
        return result;
    }

    if (!ensure_contained_directory(
            root, QStringLiteral(".dragonpixel/Ide/Rider"), result.workspace_directory,
            result.error))
    {
        return result;
    }
    result.solution_path = QDir{result.workspace_directory}.filePath(
        QStringLiteral("DragonPixel.ProjectComponents.sln"));
    result.project_path = QDir{result.workspace_directory}.filePath(
        QStringLiteral("DragonPixel.ProjectComponents.csproj"));
    if (!write_atomic(
            result.project_path,
            project_contents(result.workspace_directory, contracts, sources, manifests),
            result.error)
        || !write_atomic(
            result.solution_path, solution_contents(deterministic_project_guid(root)), result.error))
    {
        return result;
    }

    result.rider_executable = request.rider_executable.trimmed().isEmpty()
        ? find_rider_executable()
        : normalized_absolute_path(request.rider_executable);
    if (result.rider_executable.isEmpty() || !QFileInfo{result.rider_executable}.isFile())
    {
        result.error = QStringLiteral(
            "JetBrains Rider was not found. Install Rider or set DPE_RIDER_EXECUTABLE to its launcher executable.");
        return result;
    }

    result.selected_source_path = selected;
    const auto line = std::max(1, request.line);
    result.invocations = {
        ScriptEditorInvocation{result.rider_executable, {result.solution_path}, result.workspace_directory},
        ScriptEditorInvocation{result.rider_executable,
            {QStringLiteral("--line"), QString::number(line), selected}, result.workspace_directory},
    };
    for (const auto& invocation : result.invocations)
    {
        if (!launcher_(invocation.program, invocation.arguments, invocation.working_directory,
                result.error))
        {
            return result;
        }
    }
    result.succeeded = true;
    return result;
}
