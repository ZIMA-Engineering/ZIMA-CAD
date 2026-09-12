#pragma once
#include <zima/document/thread_catalog.hpp>
#include <QString>
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

// Qt text adaptation only; parsing and immutable records belong to document_core.
inline std::vector<ThreadCatalogSize> load_thread_catalog(const QString& standard) {
    const auto& catalog=zima::document::thread_catalog(standard.toStdString());
    std::vector<ThreadCatalogSize> result;result.reserve(catalog.size());
    for(const auto& size:catalog)result.push_back({QString::fromStdString(size.designation),
        size.nominal_diameter,size.pitch,size.internal_root_diameter,size.external_root_diameter,size.preferred});
    return result;
}
} // namespace zima::app
