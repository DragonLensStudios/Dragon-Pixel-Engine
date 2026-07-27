#include "EditorWindow.h"
#include "EditorRuntimePaths.h"

#include <QApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QTimer>

#include <memory>
#include <iostream>

namespace
{
QByteArray file_hash(const QString& path)
{
    QFile file{path};
    if (!file.open(QIODevice::ReadOnly))
    {
        return {};
    }
    QCryptographicHash hash{QCryptographicHash::Sha256};
    if (!hash.addData(&file))
    {
        return {};
    }
    return hash.result();
}
}

int main(int argc, char* argv[])
{
    const auto startup_trace = qEnvironmentVariableIsSet("DPE_STARTUP_TRACE");
    const auto trace = [startup_trace](const char* message) {
        if (startup_trace)
        {
            std::cerr << "DPE startup: " << message << '\n' << std::flush;
        }
    };
    trace("creating QApplication");
    QApplication application(argc, argv);
    trace("QApplication created");
    application.setApplicationName(QStringLiteral("Dragon Pixel Engine Editor"));
    application.setOrganizationName(QStringLiteral("Dragon Pixel Engine"));
    const auto arguments = application.arguments();
    QString initial_document;
    const auto self_test_requested = arguments.contains(QStringLiteral("--self-test"))
        || arguments.contains(QStringLiteral("--self-test-crash"));
    if (self_test_requested || qEnvironmentVariableIsSet("DPE_DEFAULT_SAMPLE_PROJECT"))
    {
        initial_document = dragonpixel::editor::runtime_paths::file(
            "DPE_DEFAULT_SAMPLE_PROJECT",
            QStringLiteral("samples/Slice1Sample/DragonPixelProject.json"),
            QString::fromUtf8(DPE_DEFAULT_SAMPLE_PROJECT));
    }
    const auto project_index = arguments.indexOf(QStringLiteral("--project"));
    if (project_index >= 0 && project_index + 1 < arguments.size())
    {
        initial_document = arguments[project_index + 1];
    }
    const auto scene_index = arguments.indexOf(QStringLiteral("--scene"));
    if (scene_index >= 0 && scene_index + 1 < arguments.size())
    {
        initial_document = arguments[scene_index + 1];
    }
    trace("constructing EditorWindow");
    EditorWindow window{initial_document};
    trace("EditorWindow constructed");
    const auto initial_hash = file_hash(initial_document);
    const auto scene_path = window.authoring_scene_path();
    const auto scene_hash = file_hash(scene_path);
    const auto sources_unchanged = [&] {
        return !initial_hash.isEmpty() && !scene_hash.isEmpty()
            && file_hash(initial_document) == initial_hash
            && file_hash(scene_path) == scene_hash;
    };

    const auto self_test_index = arguments.indexOf(QStringLiteral("--self-test"));
    if (self_test_index >= 0 && self_test_index + 1 < arguments.size())
    {
        trace("starting self-test");
        window.start_self_test(arguments[self_test_index + 1]);
        trace("self-test started");
        auto* poll = new QTimer(&application);
        QObject::connect(poll, &QTimer::timeout, &application, [&application, &window, &sources_unchanged] {
            if (window.self_test_ready() && sources_unchanged())
            {
                application.exit(0);
            }
        });
        poll->start(50);
        QTimer::singleShot(12000, &application, [&application, &window, &sources_unchanged] {
            const auto passed = window.self_test_ready() && sources_unchanged();
            if (!passed)
            {
                std::cerr << window.self_test_diagnostics().toStdString() << '\n';
            }
            application.exit(passed ? 0 : 1);
        });
        trace("entering self-test event loop");
        return application.exec();
    }

    const auto crash_test_index = arguments.indexOf(QStringLiteral("--self-test-crash"));
    if (crash_test_index >= 0 && crash_test_index + 1 < arguments.size())
    {
        window.start_self_test(arguments[crash_test_index + 1]);
        auto* poll = new QTimer(&application);
        const auto crash_sent = std::make_shared<bool>(false);
        QObject::connect(poll, &QTimer::timeout, &application,
            [&application, &window, &sources_unchanged, crash_sent] {
                if (!*crash_sent && window.self_test_ready())
                {
                    *crash_sent = true;
                    window.crash_self_test_worker();
                }
                else if (*crash_sent && window.self_test_recovered()
                         && sources_unchanged())
                {
                    application.exit(0);
                }
            });
        poll->start(50);
        QTimer::singleShot(15000, &application, [&application, &window] {
            std::cerr << window.self_test_diagnostics().toStdString() << '\n';
            application.exit(1);
        });
        return application.exec();
    }

    trace("showing editor");
    window.show();
    trace("entering editor event loop");
    return application.exec();
}
