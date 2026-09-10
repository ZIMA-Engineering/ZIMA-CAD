#include "application_instance.hpp"
#include "startup_arguments.hpp"
#include <zima/document/part_document.hpp>
#include <zima/assembly/assembly_document.hpp>
#include <zima/drawing/drawing_document.hpp>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include <QThread>
#include <iostream>
#include <stdexcept>
#include <functional>
#include <set>

namespace {
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
bool until(const std::function<bool()>& ready, int timeout = 20000) {
    QElapsedTimer elapsed;
    elapsed.start();
    while (elapsed.elapsed() < timeout) {
        if (ready()) return true;
        QCoreApplication::processEvents();
        QThread::msleep(25);
    }
    return false;
}
QJsonObject report(const QString& root, qint64 pid) {
    QFile file(QDir(root).filePath(QString::number(pid) + ".json"));
    if (!file.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}
void command(const QString& root, qint64 pid, const QString& name) {
    QFile file(QDir(root).filePath(QString::number(pid) + "." + name));
    require(file.open(QIODevice::WriteOnly), "Cannot send instance command");
}
struct Cleanup {
    QString root;
    ~Cleanup() {
        for (const auto& name : QDir(root).entryList({"*.json"}, QDir::Files)) {
            QFile file(QDir(root).filePath(QFileInfo(name).completeBaseName() + ".quit"));
            if (file.open(QIODevice::WriteOnly)) file.close();
        }
    }
};
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName("zima-instance-contract");
    try {
        QTemporaryDir temp;
        require(temp.isValid(), "No test directory");
        const auto root = temp.path();
        const auto lock_root = QDir(root).filePath("locks");
        {
            zima::app::ApplicationInstance first(lock_root);
            auto second = std::make_unique<zima::app::ApplicationInstance>(lock_root);
            zima::app::ApplicationInstance third(lock_root);
            require(first.number() == 1 && second->number() == 2 && third.number() == 3,
                "Live instance numbers collide");
            second.reset();
            zima::app::ApplicationInstance reused(lock_root);
            require(reused.number() == 2 && third.number() == 3,
                "Releasing a number renumbered another running instance");
        }
        QFile blocked(QDir(root).filePath("not-a-directory"));
        require(blocked.open(QIODevice::WriteOnly), "Cannot create fallback fixture");
        blocked.close();
        zima::app::ApplicationInstance fallback(blocked.fileName());
        require(fallback.number() == 0 && fallback.label().contains(QString::number(app.applicationPid())),
            "Unwritable number reservation must allow a PID-labelled instance");

        const auto part_dir = QDir(root).filePath("Project one");
        const auto assembly_dir = QDir(root).filePath("Project two");
        require(QDir().mkpath(part_dir) && QDir().mkpath(assembly_dir), "No project folders");
        const auto part_path = QDir(part_dir).filePath("compare part.prtz");
        const auto assembly_path = QDir(assembly_dir).filePath("compare assembly.asmz");
        const auto drawing_path = QDir(assembly_dir).filePath("compare drawing.drwz");
        zima::document::PartDocument::create_default().save(std::filesystem::path(part_path.toStdString()));
        zima::assembly::AssemblyDocument::create_default().save(std::filesystem::path(assembly_path.toStdString()));
        zima::drawing::DrawingDocument::create_default().save(std::filesystem::path(drawing_path.toStdString()));

        const auto parsed = zima::app::parse_startup_arguments(
            {"ZIMA-CAD", "--working-directory", root, part_path});
        require(parsed.documents == QStringList{part_path} && parsed.working_directory == part_dir,
            "A shell document must select its own project folder");
        const auto option_dir = QDir(root).filePath("folder.prtz");
        QDir().mkpath(option_dir);
        require(zima::app::parse_startup_arguments({"ZIMA-CAD", "-w", option_dir}).documents.isEmpty(),
            "Working directory was reopened as a document");
        require(zima::app::parse_startup_arguments({"ZIMA-CAD", "--working-directory=" + option_dir})
                .working_directory == option_dir, "Equals-form working directory failed");
        for (const QString extension : {"PRTZ", "ASMZ", "DRWZ", "FRMZ", "TBLZ"}) {
            const auto path = QDir(root).filePath(QString::fromUtf8("výkres s mezerou.") + extension);
            require(zima::app::parse_startup_arguments({"ZIMA-CAD", "--", path}).documents == QStringList{path},
                "Case-insensitive/Unicode external path was not preserved");
        }

        require(app.arguments().size() == 2, "Expected path to GUI executable");
        const auto executable = app.arguments().at(1);
        auto environment = QProcessEnvironment::systemEnvironment();
        environment.insert("ZIMA_VERIFY_INSTANCE_DIRECTORY", root);
        QProcess first, second, third;
        Cleanup cleanup{root};
        for (auto* process : {&first, &second, &third}) {
            process->setProgram(executable);
            process->setProcessEnvironment(environment);
            process->setProcessChannelMode(QProcess::MergedChannels);
        }
        first.setArguments({"--working-directory", root, part_path});
        second.setArguments({assembly_path});
        third.setArguments({drawing_path});
        first.start(); second.start(); third.start();
        require(first.waitForStarted() && second.waitForStarted() && third.waitForStarted(),
            "Parallel document launches failed");
        const auto first_pid = first.processId(), second_pid = second.processId(), third_pid = third.processId();
        require(until([&] { return !report(root, first_pid).isEmpty() &&
            !report(root, second_pid).isEmpty() && !report(root, third_pid).isEmpty(); }),
            "Real GUI instances did not open their documents");
        const auto a = report(root, first_pid), b = report(root, second_pid), c = report(root, third_pid);
        const std::set<int> numbers{a["instance"].toInt(), b["instance"].toInt(), c["instance"].toInt()};
        require(numbers.size() == 3 && *numbers.begin() > 0, "Parallel process numbers collide");
        require(a["documents"].toArray() == QJsonArray{"compare part.prtz"} &&
            b["documents"].toArray() == QJsonArray{"compare assembly.asmz"} &&
            c["documents"].toArray() == QJsonArray{"compare drawing.drwz"},
            "Shell launch mixed documents between instances");
        require(a["directory"].toString() == part_dir && b["directory"].toString() == assembly_dir,
            "Independent startup project directories were lost");
        for (const auto& data : {a, b, c})
            require(data["title"].toString().contains(QString("Instance %1").arg(data["instance"].toInt())) &&
                data["title"].toString().endsWith(data["documents"].toArray().first().toString()),
                "Title does not identify the stable instance and displayed document");

        command(root, second_pid, "new-window");
        qint64 fourth_pid{};
        require(until([&] {
            for (const auto& file : QDir(root).entryList({"*.json"}, QDir::Files)) {
                const auto pid = QFileInfo(file).completeBaseName().toLongLong();
                if (pid != first_pid && pid != second_pid && pid != third_pid) { fourth_pid = pid; return true; }
            }
            return false;
        }), "New Window did not start an independent process");
        const auto fourth = report(root, fourth_pid);
        require(fourth["documents"].toArray().isEmpty() && !numbers.contains(fourth["instance"].toInt()) &&
            fourth["directory"].toString() == assembly_dir, "New Window inherited documents or lost project context");

        command(root, first_pid, "close-document");
        require(until([&] { return report(root, first_pid)["documents"].toArray().isEmpty(); }),
            "Cannot close a document in its own instance");
        const auto cleared = report(root, first_pid);
        require(cleared["instance"] == a["instance"] && cleared["title"].toString().contains(
                QString("Instance %1").arg(a["instance"].toInt())) &&
            report(root, second_pid)["documents"] == b["documents"],
            "Closing a document changed another instance or lost its number");
        command(root, first_pid, "quit");
        require(first.waitForFinished(10000) && first.exitCode() == 0, "First instance did not close cleanly");
        require(second.state() == QProcess::Running && third.state() == QProcess::Running,
            "Closing one instance closed another");
        command(root, fourth_pid, "quit");
        command(root, second_pid, "quit");
        command(root, third_pid, "quit");
        require(second.waitForFinished(10000) && third.waitForFinished(10000) &&
            second.exitCode() == 0 && third.exitCode() == 0, "Remaining instances did not close cleanly");
        std::cout << "Independent Part/Assembly/Drawing launch, New Window, stable numbering, and argument contracts passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Instance contract: " << error.what() << '\n';
        return 1;
    }
}
