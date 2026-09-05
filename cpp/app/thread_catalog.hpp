#pragma once
#include <QFile>
#include <QStringList>
#include <vector>

namespace zima::app {
struct ThreadCatalogSize {
    QString designation;
    double nominal_diameter{};
    double pitch{};
    double internal_root_diameter{};
    double external_root_diameter{};
    bool preferred{};
};

inline double thread_catalog_number(QString text, bool* ok=nullptr) {
    text=text.trimmed();
    text.replace(',', '.');
    bool local_ok{};
    const double value=text.toDouble(&local_ok);
    if (ok) *ok=local_ok;
    return value;
}

inline std::vector<ThreadCatalogSize> load_thread_catalog(const QString& standard) {
    const QString resource=standard=="metric"
        ? ":/zima/data/threads/metric_iso.tsv"
        : standard=="whitworth"
        ? ":/zima/data/threads/whitworth_bsw.tsv"
        : ":/zima/data/threads/pipe_iso228.tsv";
    QFile file(resource);
    if (!file.open(QIODevice::ReadOnly|QIODevice::Text)) return {};
    std::vector<ThreadCatalogSize> result;
    while (!file.atEnd()) {
        const auto fields=QString::fromUtf8(file.readLine()).trimmed().split('\t');
        if (standard=="metric" && fields.size()>=6) {
            bool ok_d{},ok_p{},ok_d1{},ok_d3{};
            const double d=thread_catalog_number(fields[0],&ok_d);
            const double p=thread_catalog_number(fields[1],&ok_p);
            const double d1=thread_catalog_number(fields[3],&ok_d1);
            const double d3=thread_catalog_number(fields[4],&ok_d3);
            if (!(ok_d&&ok_p&&ok_d1&&ok_d3)) continue;
            QString designation=fields[5].trimmed();
            designation.replace("x",QStringLiteral("×"));
            designation.remove(' ');
            result.push_back({designation,d,p,d1,d3,
                !designation.contains(QStringLiteral("×"))});
        } else if (standard=="whitworth" && fields.size()>=8) {
            bool ok_p{},ok_d{},ok_root{};
            const double p=thread_catalog_number(fields[2],&ok_p);
            const double d=thread_catalog_number(fields[3],&ok_d);
            const double root=thread_catalog_number(fields[7],&ok_root);
            if (!(ok_p&&ok_d&&ok_root)) continue;
            const auto designation=fields[0].trimmed();
            const bool common=designation=="W 3/8" || designation=="W 1/2" ||
                designation=="W 5/8" || designation=="W 3/4" ||
                designation=="W 1";
            result.push_back({designation,d,p,root,root,common});
        } else if (standard=="pipe" && fields.size()>=4) {
            bool ok_d{},ok_pitch_diameter{},ok_root{};
            const double d=thread_catalog_number(fields[1],&ok_d);
            const double pitch_diameter=thread_catalog_number(fields[2],
                &ok_pitch_diameter);
            const double root=thread_catalog_number(fields[3],&ok_root);
            if (!(ok_d&&ok_pitch_diameter&&ok_root)) continue;
            const double pitch=(d-pitch_diameter)/0.640327;
            const auto designation=fields[0].trimmed();
            const bool common=designation=="G 1/4" || designation=="G 3/8" ||
                designation=="G 1/2" || designation=="G 3/4" ||
                designation=="G 1";
            result.push_back({designation,d,pitch,root,root,common});
        }
    }
    return result;
}

} // namespace zima::app
