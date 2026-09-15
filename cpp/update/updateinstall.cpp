// Adapted from the ZIMA-CAD-Parts updater (ZIMA-Engineering).
#include "updatecore.h"
#include <QtCore/private/qzipreader_p.h>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QRegularExpression>
#include <QCryptographicHash>
#include <QDirIterator>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocalServer>
#include <QLocalSocket>
#include <QLockFile>
#include <QProcess>
#include <QSaveFile>
#include <QSet>
#include <QSettings>
#include <QThread>
#include <QUuid>
#include <memory>
#ifdef Q_OS_WIN
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#endif

namespace CadUpdate {
namespace {
void makeDirectory(const QString &root, const QString &relative)
{
    if (!QDir().mkpath(child(root, relative))) throw QString("Cannot create installation directory");
}
void writeFile(const QString &path, const QByteArray &bytes)
{
    QSaveFile out(path);
    if (!out.open(QIODevice::WriteOnly) || out.write(bytes) != bytes.size() || !out.commit()) throw QString("Cannot write update file: ") + path;
}
QJsonObject filesIn(const QString &directory)
{
    QJsonObject files;
    QSet<QString> folded;
    QDirIterator entries(directory, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System, QDirIterator::Subdirectories);
    while (entries.hasNext()) {
        entries.next(); const auto info = entries.fileInfo();
        const auto name = QDir(directory).relativeFilePath(info.absoluteFilePath());
        if (!safeRelativePath(name) || info.isSymLink() || info.isJunction()) throw QString("Unexpected linked or unsafe file");
        if (info.isDir()) continue;
        if (folded.contains(name.toCaseFolded())) throw QString("Case-colliding files");
        folded.insert(name.toCaseFolded());
        files[name] = QJsonObject{{"sha256", hashFile(info.absoluteFilePath())}, {"size", info.size()},
            {"executable", name.endsWith(".sh")
#ifndef Q_OS_WIN
                || bool(info.permissions() & (QFile::ExeOwner | QFile::ExeGroup | QFile::ExeOther))
#endif
            }};
    }
    return files;
}
QJsonObject receipt(const QString &root, const QString &version)
{
    const auto file = child(root, ".updates/ready/" + platform() + '-' + version + ".json");
    const auto result = readJson(file, 32 * 1024 * 1024);
    if (!QRegularExpression("^[0-9a-f-]{36}$").match(result["transaction"].toString()).hasMatch()) throw QString("Invalid transaction ID");
    const auto verified = verifyManifest(canonical(result["manifest"].toObject()), canonical(result["signature"].toObject()));
    if (verified["minimumUpdaterVersion"].toInt() > Protocol || verified["launcherProtocol"].toInt() > Protocol
        || verified["version"] != version || !verified["platforms"].toObject().contains(platform())) throw QString("Receipt does not match update");
    return result;
}
QString selected(const QString &root)
{
    QSettings settings(child(root, "launcher.ini"), QSettings::IniFormat);
    if (settings.value("launcher/" + platformDirectory() + "_custom", false).toBool()) throw QString("Custom build selected");
    const auto result = settings.value("launcher/" + platformDirectory()).toString();
    if (!validVersion(result)) throw QString("Invalid selected version");
    return result;
}
void select(const QString &root, const QString &version)
{
    if (!validVersion(version)) throw QString("Invalid version selection");
    const auto path = child(root, "launcher.ini");
    QFile old(path);
    if (!old.open(QIODevice::ReadOnly) || old.size() > 65536) throw QString("Cannot read launcher settings");
    auto lines = QString::fromUtf8(old.readAll()).split('\n'); old.close();
    QString section;
    bool versionSet = false, customSet = false;
    for (auto &line : lines) {
        auto text = line.trimmed();
        if (text.startsWith('[')) section = text;
        if (section != "[launcher]") continue;
        if (text.startsWith(platformDirectory() + '=')) { line = platformDirectory() + '=' + version; versionSet = true; }
        if (text.startsWith(platformDirectory() + "_custom=")) { line = platformDirectory() + "_custom=false"; customSet = true; }
    }
    if (!versionSet || !customSet) throw QString("Missing launcher selection keys");
    writeFile(path, lines.join('\n').toUtf8());
}
QString journalPath(const QString &root) { return child(root, ".updates/" + platform() + "-journal.json"); }
QString stagedDirectory(const QString &base, const QString &transaction)
{
    // Match the ten-character version directory length. Adding a deep staging
    // prefix would invalidate the package's existing Windows path budget.
    return base + "/.u" + transaction.left(8);
}
QJsonObject installed(const QString &root, const QString &version)
{
    if (!validVersion(version)) throw QString("Invalid installed version");
    const auto managed = child(root, ".updates/installed/" + platform() + '-' + version + ".json");
    QJsonObject record;
    if (QFileInfo::exists(managed)) record = readJson(managed, 32 * 1024 * 1024);
    else record = readJson(child(root, "release-info/" + platform() + '-' + version + ".json"), 32 * 1024 * 1024);
    if (record.contains("attestation")) {
        const auto attestation = verifySignedObject(canonical(record["attestation"].toObject()), canonical(record["signature"].toObject()));
        if (attestation["kind"] != "installed-build" || attestation["schemaVersion"].toInt() != 1
            || attestation["product"] != "ZIMA-CAD" || attestation["version"] != version
            || attestation["platform"] != platform() || !attestation["runtimeFiles"].isObject() || !attestation["sourceFiles"].isObject())
            throw QString("Invalid bootstrap attestation");
        record["runtimeFiles"] = attestation["runtimeFiles"];
        record["sourceFiles"] = attestation["sourceFiles"];
    } else {
        const auto manifest = verifyManifest(canonical(record["manifest"].toObject()), canonical(record["signature"].toObject()));
        if (manifest["version"] != version) throw QString("Installed version receipt mismatch");
        // Receipts are local caches. Their inventories must be bound to the
        // publisher signature via the signed checksums stored at preparation.
        const auto inventory = record["checksums"].toObject();
        if (QString::fromLatin1(QCryptographicHash::hash(canonical(inventory), QCryptographicHash::Sha256).toHex()) != manifest["checksumsSha256"].toString())
            throw QString("Untrusted installed inventory");
        QJsonObject runtimeFiles, sourceFiles;
        const auto runtimePrefix = platformDirectory() + '/' + version + '/';
        const auto sourcePrefix = "source/" + version + '/';
        for (auto it = inventory.begin(); it != inventory.end(); ++it) {
            if (it.key().startsWith(runtimePrefix)) runtimeFiles[it.key().mid(runtimePrefix.size())] = it.value();
            if (it.key().startsWith(sourcePrefix)) sourceFiles[it.key().mid(sourcePrefix.size())] = it.value();
        }
        record["runtimeFiles"] = runtimeFiles; record["sourceFiles"] = sourceFiles;
    }
    return record;
}
void verifyTree(const QString &root, const QString &directory, const QJsonObject &expected, bool source = false)
{
    const auto actual = filesIn(child(root, directory));
    if (actual.keys() != expected.keys()) throw QString("Installed file inventory differs; local changes are preserved");
    for (auto it = expected.begin(); it != expected.end(); ++it) {
        auto a = actual[it.key()].toObject(), b = it.value().toObject();
#ifdef Q_OS_WIN
        Q_UNUSED(source);
        a["executable"] = b["executable"];
#endif
        if (a != b) throw QString("Modified installed file: ") + directory + '/' + it.key();
    }
}
void waitForInstances(const QString &root, int milliseconds)
{
#ifdef ZIMA_UPDATE_TESTING
    if (qEnvironmentVariableIsSet("ZIMA_UPDATE_TEST_WAIT")) milliseconds = qEnvironmentVariableIntValue("ZIMA_UPDATE_TEST_WAIT");
#endif
    QElapsedTimer timer; timer.start();
    while (true) {
        bool busy = false;
        const auto dir = child(root, ".updates/instances");
        for (const auto &name : QDir(dir).entryList({"*.lock"}, QDir::Files)) {
            QLockFile instance(child(root, ".updates/instances/" + name)); instance.setStaleLockTime(0);
            if (!instance.tryLock()) busy = true;
        }
        if (!busy) return;
        if (timer.elapsed() > milliseconds) throw QString("Another CAD instance is still running; update remains prepared");
        QThread::msleep(100);
    }
}
QProcessEnvironment environment(const QString &root, const QString &version)
{
    auto env = QProcessEnvironment::systemEnvironment();
    for (const auto &name : {"QT_PLUGIN_PATH", "QT_QPA_PLATFORM_PLUGIN_PATH", "QML_IMPORT_PATH", "QML2_IMPORT_PATH", "QTWEBENGINEPROCESS_PATH", "QTWEBENGINE_RESOURCES_PATH", "QTWEBENGINE_LOCALES_PATH", "ZIMA_UPDATE_SOCKET", "ZIMA_UPDATE_TOKEN"}) env.remove(name);
    const auto runtime = child(root, platformDirectory() + '/' + version);
#ifdef Q_OS_WIN
    env.insert("PATH", runtime + ';' + env.value("SystemRoot") + "/System32;" + env.value("SystemRoot"));
    const QMap<QString, QString> resources{{"CSF_ShadersDirectory", "Shaders"}, {"CSF_SHMessage", "SHMessage"},
        {"CSF_XSMessage", "XSMessage"}, {"CSF_STEPDefaults", "XSTEPResource"}, {"CSF_IGESDefaults", "XSTEPResource"},
        {"CSF_PluginDefaults", "StdResource"}, {"CSF_StandardDefaults", "StdResource"}, {"CSF_XCAFDefaults", "StdResource"},
        {"CSF_MDTVTexturesDirectory", "Textures"}, {"CSF_XmlOcafResource", "XmlOcafResource"}};
    for (auto it = resources.begin(); it != resources.end(); ++it) env.insert(it.key(), runtime + "/resources/occt/" + it.value());
#else
    env.insert("LD_LIBRARY_PATH", runtime + "/lib");
    env.insert("QT_PLUGIN_PATH", runtime + "/plugins");
    env.insert("CSF_ShadersDirectory", runtime + "/resources/occt/Shaders");
    env.insert("CSF_XSMessage", runtime + "/resources/occt/XSMessage");
    env.insert("CSF_SHMessage", runtime + "/resources/occt/SHMessage");
#endif
    env.insert("CSF_XSTEPDefaults", runtime + "/resources/occt/XSTEPResource");
    env.insert("ZIMA_INSTALL_ROOT", root);
    return env;
}
QString executable(const QString &root, const QString &version)
{
    return child(root, platformDirectory() + '/' + version +
#ifdef Q_OS_WIN
        "/zima-cad-cpp.exe"
#else
        "/bin/zima-cad-cpp"
#endif
    );
}
void chooseEngine(const QString &root, const QString &version)
{
    const auto path = child(root, ".updates/engine.ini");
    QSettings old(path, QSettings::IniFormat);
    auto windows = old.value("updater/windows").toString();
    auto linux = old.value("updater/linux").toString();
    (platformDirectory() == "windows" ? windows : linux) = version;
    writeFile(path, ("[updater]\nwindows=" + windows + "\nlinux=" + linux + "\n").toUtf8());
}
bool running(qint64 pid)
{
#ifdef Q_OS_WIN
    HANDLE handle = OpenProcess(SYNCHRONIZE, FALSE, DWORD(pid));
    if (!handle) return GetLastError() == ERROR_ACCESS_DENIED;
    const bool alive = WaitForSingleObject(handle, 0) == WAIT_TIMEOUT;
    CloseHandle(handle);
    return alive;
#else
    return ::kill(pid_t(pid), 0) == 0 || errno == EPERM;
#endif
}
void configureProcess(QProcess &process, const QString &root, const QString &version)
{
    process.setProcessEnvironment(environment(root, version));
    process.setProgram(executable(root, version));
    process.setWorkingDirectory(root);
#ifdef Q_OS_WIN
    process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) { args->flags |= CREATE_NO_WINDOW; });
#endif
}
bool startCandidate(const QString &root, const QString &version, bool &stillRunning)
{
    QLocalServer server;
    const auto nonce = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const auto name = "ZIMA-" + nonce;
    server.setSocketOptions(QLocalServer::UserAccessOption);
    if (!server.listen(name)) throw QString("Cannot create update startup channel");
    QProcess process;
    configureProcess(process, root, version);
    auto env = process.processEnvironment();
    env.insert("ZIMA_UPDATE_SOCKET", name); env.insert("ZIMA_UPDATE_TOKEN", nonce);
    process.setProcessEnvironment(env);
    qint64 pid = 0;
    stillRunning = false;
    if (!process.startDetached(&pid)) return false;
    QElapsedTimer timer; timer.start();
    while (timer.elapsed() < 60000) {
        if (!running(pid)) return false;
        if (!server.waitForNewConnection(200)) continue;
        std::unique_ptr<QLocalSocket> socket(server.nextPendingConnection());
        if (!socket) continue;
        QByteArray bytes;
        QElapsedTimer readTimer; readTimer.start();
        while (!bytes.contains('\n') && bytes.size() < 4096 && readTimer.elapsed() < 1000) {
            if (!socket->bytesAvailable()) socket->waitForReadyRead(100);
            bytes += socket->readAll();
        }
        const auto data = QJsonDocument::fromJson(bytes).object();
        if (data["token"] == nonce && data["version"] == version && data["pid"].toInteger() == pid) return true;
    }
    stillRunning = running(pid);
    return false;
}
QStringList cleanup(const QString &root, const QString &current, const QString &previous)
{
    QStringList retained;
    for (const auto &version : QDir(child(root, platformDirectory())).entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (!validVersion(version) || version == current || version == previous) continue;
        try {
            const auto record = installed(root, version);
            const auto directory = platformDirectory() + '/' + version;
            verifyTree(root, directory, record["runtimeFiles"].toObject());
            // Processes register their runtime version. Do not erase any
            // older version with a live instance (including a slow candidate).
            for (const auto &name : QDir(child(root, ".updates/instances")).entryList({version + "-*.lock"}, QDir::Files)) {
                QLockFile instance(child(root, ".updates/instances/" + name)); instance.setStaleLockTime(0);
                if (!instance.tryLock()) throw QString("Version is in use");
            }
            const auto target = child(root, directory);
            if (!QDir(target).removeRecursively()) throw QString("Old runtime is still in use");
            const auto source = "source/" + version;
            if (!QFileInfo::exists(child(root, "windows/" + version)) && !QFileInfo::exists(child(root, "linux/" + version))
                && QFileInfo::exists(child(root, source))) {
                verifyTree(root, source, record["sourceFiles"].toObject(), true);
                if (!QDir(child(root, source)).removeRecursively()) throw QString("Old sources could not be removed");
            }
        } catch (const QString &) { retained.append(version); }
    }
    return retained;
}
QJsonObject switchVersion(const QString &root, const QString &next, const QString &previous)
{
    const auto record = installed(root, next);
    verifyTree(root, platformDirectory() + '/' + next, record["runtimeFiles"].toObject());
    // The root launcher keeps using the previously working helper until the
    // new GUI acknowledges startup, even if power fails after selection.
    const auto priorRecord = installed(root, previous);
    verifyTree(root, platformDirectory() + '/' + previous, priorRecord["runtimeFiles"].toObject());
    chooseEngine(root, previous);
    writeJson(journalPath(root), {{"phase", "candidate"}, {"previous", previous}, {"candidate", next}});
    select(root, next);
    bool stillRunning = false;
    const bool healthy = startCandidate(root, next, stillRunning);
    if (!healthy) {
        select(root, previous);
        writeJson(journalPath(root), {{"phase", "failed"}, {"previous", previous}, {"candidate", next}});
        if (!stillRunning) {
            QProcess previousProcess;
            configureProcess(previousProcess, root, previous);
            previousProcess.startDetached();
        }
        throw QString("New version did not acknowledge startup; previous selection restored. Close any slow candidate before starting again.");
    }
    writeJson(journalPath(root), {{"phase", "committed"}, {"current", next}, {"previous", previous}});
    chooseEngine(root, next);
    const auto retained = cleanup(root, next, previous);
    return {{"status", "installed"}, {"version", next}, {"previous", previous}, {"cleanupPending", QJsonArray::fromStringList(retained)}};
}
}
QJsonObject prepare(const QString &root, const QString &archivePath, const QByteArray &payload, const QByteArray &signature, const Progress &progress)
{
    assertManaged(root);
    const auto manifest = verifyManifest(payload, signature);
    const auto archive = manifest["archive"].toObject();
    if (QFileInfo(archivePath).size() != archive["size"].toInteger() || hashFile(archivePath) != archive["sha256"].toString()) throw QString("Downloaded archive hash or size does not match signature");
    if (manifest["minimumUpdaterVersion"].toInt() > Protocol || manifest["launcherProtocol"].toInt() > Protocol || !manifest["platforms"].toObject().contains(platform())) throw QString("Incompatible update");
    const auto transaction = QUuid::createUuid().toString(QUuid::WithoutBraces);
    for (const auto &base : {platformDirectory(), QString("source")}) {
        makeDirectory(root, base);
        if (!QDir().mkdir(child(root, stagedDirectory(base, transaction))))
            throw QString("Cannot reserve a new update staging directory");
    }
    QZipReader reader(archivePath);
    const auto entries = reader.fileInfoList();
    if (reader.status() != QZipReader::NoError || entries.size() > 100000) throw QString("Invalid ZIP directory");
    QSet<QString> seen;
    qint64 unpacked = 0, count = 0;
    for (const auto &entry : entries) {
        auto name = entry.filePath;
        if (entry.isDir && name.endsWith('/')) name.chop(1);
        if (entry.isDir && name == "ZIMA-CAD") continue;
        if (!safeRelativePath(name) || name.size() > 180 || !name.startsWith("ZIMA-CAD/") || entry.isSymLink || (!entry.isDir && !entry.isFile)
            || entry.size < 0 || entry.size > 512 * 1024 * 1024 || seen.contains(name.toCaseFolded())) throw QString("Unsafe or unsupported ZIP entry");
        seen.insert(name.toCaseFolded());
        if (entry.isFile) { unpacked += entry.size; ++count; }
        if (unpacked > archive["unpackedSize"].toInteger() || count > archive["fileCount"].toInteger()) throw QString("ZIP exceeds signed extraction limits");
    }
    // A file may not also serve as a parent directory, including case aliases.
    QSet<QString> fileNames;
    for (const auto &entry : entries) if (entry.isFile) fileNames.insert(entry.filePath.toCaseFolded());
    for (const auto &name : seen) {
        auto slash = name.indexOf('/');
        while (slash >= 0) {
            if (fileNames.contains(name.left(slash))) throw QString("ZIP file/directory collision");
            slash = name.indexOf('/', slash + 1);
        }
    }
    if (count != archive["fileCount"].toInteger() || unpacked != archive["unpackedSize"].toInteger()) throw QString("ZIP inventory count/size mismatch");
    const auto checksumsBytes = reader.fileData("ZIMA-CAD/checksums.json");
    if (QString::fromLatin1(QCryptographicHash::hash(checksumsBytes, QCryptographicHash::Sha256).toHex()) != manifest["checksumsSha256"].toString()) throw QString("Invalid signed file inventory");
    const auto checksums = QJsonDocument::fromJson(checksumsBytes).object();
    if (canonical(checksums) != checksumsBytes || checksums.size() != count - 1) throw QString("Invalid canonical file inventory");
    const auto version = manifest["version"].toString();
    const auto runtimePrefix = platformDirectory() + '/' + version + '/';
    const auto sourcePrefix = "source/" + version + '/';
    QJsonObject runtimeFiles, sourceFiles;
    if (progress) progress({{"phase", "verifying"}});
    for (const auto &entry : entries) {
        if (entry.isDir) continue;
        const auto name = entry.filePath.mid(9);
        const auto data = reader.fileData(entry.filePath);
        if (data.size() != entry.size || reader.status() != QZipReader::NoError) throw QString("ZIP file read failed");
        if (name == "checksums.json") continue;
        const auto expected = checksums[name].toObject();
        if (expected["size"].toInteger(-1) != entry.size || expected["sha256"].toString() != QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex())) throw QString("ZIP file differs from signed inventory: ") + name;
        if (name.startsWith(runtimePrefix) || name.startsWith(sourcePrefix)) {
            const bool runtimeFile = name.startsWith(runtimePrefix);
            const auto relative = stagedDirectory(runtimeFile ? platformDirectory() : QString("source"), transaction)
                + '/' + name.mid(runtimeFile ? runtimePrefix.size() : sourcePrefix.size());
#ifdef Q_OS_WIN
            if (child(root, relative).size() > 259) throw QString("Installation path is too long; use a shorter distribution directory");
#endif
            makeDirectory(root, QFileInfo(relative).path());
            writeFile(child(root, relative), data);
#ifndef Q_OS_WIN
            QFile::setPermissions(child(root, relative), QFile::ReadOwner | QFile::WriteOwner | QFile::ReadGroup | QFile::ReadOther
                | (expected["executable"].toBool() ? QFile::ExeOwner | QFile::ExeGroup | QFile::ExeOther : QFile::Permissions()));
#endif
            if (name.startsWith(runtimePrefix)) runtimeFiles[name.mid(runtimePrefix.size())] = expected;
            else sourceFiles[name.mid(sourcePrefix.size())] = expected;
        }

    }
    if (runtimeFiles.isEmpty() || sourceFiles.isEmpty() || QString::fromLatin1(QCryptographicHash::hash(canonical(sourceFiles), QCryptographicHash::Sha256).toHex()) != manifest["source"].toObject()["treeSha256"].toString()) throw QString("Source inventory mismatch");
    const auto metadata = readJson(child(root, stagedDirectory(platformDirectory(), transaction) + "/version.json"));
    if (metadata["version"] != version || metadata["commit"] != manifest["commit"] || metadata["platform"] != platform()) throw QString("Runtime identity differs from manifest");
    QJsonObject record{{"transaction", transaction}, {"checksums", checksums}, {"manifest", manifest}, {"signature", QJsonDocument::fromJson(signature).object()}, {"runtimeFiles", runtimeFiles}, {"sourceFiles", sourceFiles}};
    makeDirectory(root, ".updates/ready");
    writeJson(child(root, ".updates/ready/" + platform() + '-' + version + ".json"), record);
    return {{"status", "prepared"}, {"version", version}, {"transaction", transaction}};
}
bool trustedInstallation(const QString &root)
{
    try { assertManaged(root); installed(root, selected(root)); return true; }
    catch (const QString &) { return false; }
}
QJsonObject status(const QString &root)
{
    assertManaged(root);
    QJsonObject result{{"status", "idle"}, {"installedVersion", selected(root)}, {"trusted", trustedInstallation(root)}};
    if (QFileInfo::exists(journalPath(root))) result["transaction"] = readJson(journalPath(root));
    return result;
}
QJsonObject activate(const QString &root, const QString &version, const Progress &progress)
{
    assertManaged(root);
    if (!trustedInstallation(root)) throw QString("Use a signed distribution package to install updates");
    if (!validVersion(version)) throw QString("Invalid update version");
    makeDirectory(root, ".updates");
    QLockFile lock(child(root, ".updates/install.lock")); lock.setStaleLockTime(0);
    if (!lock.tryLock()) throw QString("Another installer or launcher is active");
    if (progress) progress({{"phase", "waiting"}});
    waitForInstances(root, 30000);
    const auto previous = selected(root);
    if (version <= previous) throw QString("Use rollback to select a previous verified version");
    auto record = receipt(root, version);
    const auto checksums = record["checksums"].toObject();
    if (QString::fromLatin1(QCryptographicHash::hash(canonical(checksums), QCryptographicHash::Sha256).toHex()) != record["manifest"].toObject()["checksumsSha256"].toString())
        throw QString("Prepared inventory was modified");
    QJsonObject runtimeFiles, sourceFiles;
    const auto rp = platformDirectory() + '/' + version + '/', sp = "source/" + version + '/';
    for (auto it = checksums.begin(); it != checksums.end(); ++it) {
        if (it.key().startsWith(rp)) runtimeFiles[it.key().mid(rp.size())] = it.value();
        if (it.key().startsWith(sp)) sourceFiles[it.key().mid(sp.size())] = it.value();
    }
    record["runtimeFiles"] = runtimeFiles; record["sourceFiles"] = sourceFiles;
    for (const auto &base : {platformDirectory(), QString("source")}) {
        const auto stage = stagedDirectory(base, record["transaction"].toString());
        const auto relative = base + '/' + version;
        const auto destination = child(root, relative);
        // A crash may have imported one tree already. Revalidate whichever copy
        // exists instead of requiring the staging tree to remain complete.
        if (QFileInfo::exists(child(root, stage)))
            verifyTree(root, stage, record[base == "source" ? "sourceFiles" : "runtimeFiles"].toObject(), base == "source");
        if (QFileInfo::exists(destination)) {
            verifyTree(root, relative, record[base == "source" ? "sourceFiles" : "runtimeFiles"].toObject(), base == "source");
        } else {
            makeDirectory(root, base);
            if (!QDir().rename(child(root, stage), destination)) throw QString("Cannot import prepared version");
        }
    }
    makeDirectory(root, ".updates/installed");
    writeJson(child(root, ".updates/installed/" + platform() + '-' + version + ".json"), record);
    if (progress) progress({{"phase", "activating"}});
    return switchVersion(root, version, previous);
}
QJsonObject rollback(const QString &root, const Progress &progress)
{
    assertManaged(root);
    QLockFile lock(child(root, ".updates/install.lock")); lock.setStaleLockTime(0);
    if (!lock.tryLock()) throw QString("Another installer or launcher is active");
    if (progress) progress({{"phase", "waiting"}});
    waitForInstances(root, 30000);
    const auto history = readJson(journalPath(root));
    const auto previous = history["previous"].toString();
    if (!validVersion(previous) || previous == selected(root)) throw QString("No retained previous version");
    return switchVersion(root, previous, selected(root));
}
int launch(const QString &root, bool recoverOnly)
{
    assertManaged(root);
    makeDirectory(root, ".updates");
    QLockFile lock(child(root, ".updates/install.lock")); lock.setStaleLockTime(0);
    if (!lock.tryLock()) throw QString("An update is being activated; try again shortly");
    if (QFileInfo::exists(journalPath(root))) {
        const auto journal = readJson(journalPath(root));
        if (journal["phase"] == "candidate") {
            const auto previous = journal["previous"].toString();
            if (!validVersion(previous)) throw QString("Invalid recovery journal");
            select(root, previous);
            auto recovered = journal; recovered["phase"] = "recovered"; writeJson(journalPath(root), recovered);
        }
    }
    const auto version = selected(root);
    // Verify provenance on launch. Hashing whole runtimes belongs to install,
    // rollback and cleanup; it must not delay every ordinary application start.
    installed(root, version);
    if (recoverOnly) return 0;
    QProcess process;
    configureProcess(process, root, version);
    if (!process.startDetached()) throw QString("Cannot start selected version");
    return 0;
}
}
