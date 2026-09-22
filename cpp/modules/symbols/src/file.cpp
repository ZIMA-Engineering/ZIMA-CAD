#include <zima/symbols/definition.hpp>
#include <QFile>
#include <QSaveFile>
namespace zima::symbols {
namespace {
QString qpath(const std::filesystem::path& path) {
    const auto bytes=path.u8string();return QString::fromUtf8(reinterpret_cast<const char*>(bytes.data()),static_cast<qsizetype>(bytes.size()));
}
}
Definition Definition::load(const std::filesystem::path& path) {
    QFile file(qpath(path));if(!file.open(QIODevice::ReadOnly))throw std::runtime_error("Cannot read symbol definition");
    return from_serialized(file.readAll().toStdString());
}
void Definition::save(const std::filesystem::path& path) const {
    const auto bytes=serialized();QSaveFile file(qpath(path));
    if(!file.open(QIODevice::WriteOnly)||file.write(bytes.data(),static_cast<qint64>(bytes.size()))!=static_cast<qint64>(bytes.size())||!file.commit())
        throw std::runtime_error("Cannot save symbol definition");
}
}
