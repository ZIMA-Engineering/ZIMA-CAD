#pragma once

#include <QDir>
#include <QFileInfo>
#include <QObject>
#include <QSet>
#include <QSettings>
#include <QStringList>
#include <functional>

namespace zima::app {
inline QString include_directory_path(const QString& directory,const QString& path) {
    return QDir::cleanPath(QDir::isAbsolutePath(path)?path:QDir(directory).absoluteFilePath(path));
}
struct DirectoryIncludes {
    QStringList directories;
    QString error;
};
// One refresh owns the traversal. No persistent cache can conceal an edited
// include, missing target, or changed source document.
inline DirectoryIncludes directory_includes(const QString& directory,const QString& key,const QStringList* pending=nullptr) {
    DirectoryIncludes result;QSet<QString> visited,ancestors;
    std::function<bool(const QString&,bool)> visit=[&](const QString& path,bool root){
        const QFileInfo info(path);QString identity=info.canonicalFilePath();
#ifdef Q_OS_WIN
        identity=identity.toCaseFolded();
#endif
        if(identity.isEmpty()||!info.isDir()||!info.isReadable()){
            result.error=QObject::tr("Zdrojový adresář není dostupný: %1").arg(QDir::toNativeSeparators(path));return false;
        }
        if(ancestors.contains(identity)||ancestors.size()>=64){result.error=QObject::tr("Odkazy adresářů tvoří cyklus nebo jsou příliš hluboké: %1").arg(QDir::toNativeSeparators(path));return false;}
        if(visited.contains(identity))return true;
        ancestors.insert(identity);visited.insert(identity);result.directories.push_back(path);
        QSettings metadata(QDir(path).filePath("0000-index/metadata.ini"),QSettings::IniFormat);metadata.setFallbacksEnabled(false);
        const auto links=root&&pending?*pending:metadata.value(key).toStringList();
        for(const auto& link:links)if(!link.trimmed().isEmpty()&&!visit(include_directory_path(path,link.trimmed()),false))return false;
        ancestors.remove(identity);return true;
    };
    if(!visit(QFileInfo(directory).absoluteFilePath(),true))result.directories={QFileInfo(directory).absoluteFilePath()};
    return result;
}
} // namespace zima::app
