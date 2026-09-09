#pragma once
#include <zima/kernel/geometry_kernel.hpp>
#include <QString>
#include <QObject>
#include <optional>
#include <set>
namespace zima::app {
// Display numbering is local to an owner/occurrence. Persisted semantic IDs
// remain untouched; no kernel topology traversal is performed by the UI.
template<class Reference>
std::optional<QString> reference_display_label(const Reference& wanted,
    const zima::kernel::ViewerReferenceGeometry& geometry) {
    const std::string wanted_path = [&] {
        if constexpr (requires { wanted.instance_path.encoded(); }) return wanted.instance_path.encoded();
        else return wanted.instance_path;
    }();
    const auto number = [&](const auto& entries, const auto& descriptor) {
        std::set<std::string> seen;
        for (const auto& entry : entries) {
            const auto& ref = descriptor(entry);
            if (ref.owner_id != wanted.owner_id || ref.instance_path != wanted_path) continue;
            seen.insert(ref.semantic_key);
            if (ref.semantic_key == wanted.semantic_key) return static_cast<int>(seen.size());
        }
        return 0;
    };
    const auto identity=[](const auto& ref)->const auto& {return ref;};
    const auto descriptor=[](const auto& item)->const auto& {return item.reference;};
    const int face=number(geometry.triangle_references,identity);
    const int edge=number(geometry.edges,descriptor);
    const int point=number(geometry.points,descriptor);
    const int axis=number(geometry.axes,descriptor);
    if (!(face||edge||point||axis)) return std::nullopt;
    const auto key=QString::fromStdString(wanted.semantic_key);
    if(key.startsWith("origin:plane:"))return QObject::tr("Rovina %1").arg(key.sliced(13).toUpper());
    if(key.startsWith("origin:axis:"))return QObject::tr("Osa %1").arg(key.sliced(12).toUpper());
    if(key=="origin:point")return QObject::tr("Počátek");
    if(key=="plane")return QObject::tr("Rovina");
    if(face)return QObject::tr("Plocha %1").arg(face);
    if(axis)return QObject::tr("Osa %1").arg(axis);
    if(edge)return QObject::tr("Hrana %1").arg(edge);
    return QObject::tr("Bod %1").arg(point);
}
}
