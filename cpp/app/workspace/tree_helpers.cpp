#include "workspace_internal.hpp"

namespace zima::app::workspace_detail {


QTreeWidgetItem* add_construction_origin_tree_item(QTreeWidgetItem* parent,
    const zima::document::ContainerOrigin& container_origin,
    const std::string& container_name,
    const zima::assembly::InstancePath& instance_path,
    bool point_kind_container) {
    auto* origin = new QTreeWidgetItem(parent, {QObject::tr("Počátek kontejneru")});
    origin->setIcon(0, resource_icon("origin"));
    origin->setData(0, Qt::UserRole, QString::fromStdString(container_origin.id));
    origin->setData(0, Qt::UserRole + 1,
        QString::fromStdString(instance_path.encoded()));
    origin->setData(0, Qt::UserRole + 3, "construction-origin");
    for (const auto& origin_child : container_origin.children) {
        const auto label = origin_child.kind == zima::document::OriginChildKind::Point
            ? QObject::tr("Point (%1)").arg(QString::fromStdString(container_name))
            : QString::fromStdString(origin_child.name);
        auto* child = new QTreeWidgetItem(origin, {label});
        child->setIcon(0, resource_icon(
            origin_child.kind == zima::document::OriginChildKind::Point ? "point" :
            origin_child.kind == zima::document::OriginChildKind::Axis ? "axis" : "plane"));
        child->setData(0, Qt::UserRole, QString::fromStdString(origin_child.id));
        child->setData(0, Qt::UserRole + 1,
            QString::fromStdString(instance_path.encoded()));
        child->setData(0, Qt::UserRole + 3, "origin-reference");
        // A Point-kind container's ":point" origin child IS the container's
        // own committed point (construction_viewer_mesh()'s Point branch
        // publishes it into the reference geometry as owner=container_origin
        // .id, semantic_key="point" -- no "origin:" prefix, unlike the
        // document's own root origin point or an Axis/Plane container's
        // local frame markers). Keeping the "origin:" prefix here for a
        // Point container made this node's semantic_key never match that
        // geometry entry, so picking it as a 2nd/3rd reference (the classic
        // "2 points define an axis"/"3 points define a plane" shortcut)
        // silently contributed zero constraint and never satisfied the
        // shortcut-completion check.
        const bool matches_own_point =
            point_kind_container &&
            origin_child.kind == zima::document::OriginChildKind::Point;
        child->setData(0, Qt::UserRole + 5, matches_own_point
            ? QStringLiteral("point")
            : QString::fromStdString(std::string("origin:") + origin_child.key));
        child->setData(0, Qt::UserRole + 6,
            QString::fromStdString(container_origin.id));
    }
    return origin;
}

QString feature_icon_name(zima::document::FeatureKind kind) {
    using zima::document::FeatureKind;
    switch (kind) {
        case FeatureKind::Sketch: return QStringLiteral("sketch");
        case FeatureKind::Box: return QStringLiteral("box");
        case FeatureKind::Cylinder: return QStringLiteral("cylinder");
        case FeatureKind::Sphere: return QStringLiteral("sphere");
        case FeatureKind::Cone: return QStringLiteral("cone");
        case FeatureKind::Pyramid: return QStringLiteral("pyramid");
        case FeatureKind::Wedge: return QStringLiteral("wedge");
        case FeatureKind::Extrusion: return QStringLiteral("protrusion");
        case FeatureKind::Revolution: return QStringLiteral("revolve");
        case FeatureKind::Sweep2D: return QStringLiteral("sweep2d");
        case FeatureKind::HelicalSweep: return QStringLiteral("helical-sweep");
        case FeatureKind::Sweep3D: return QStringLiteral("sweep");
        case FeatureKind::ImportedStep: return QStringLiteral("import-step");
        case FeatureKind::Fillet: return QStringLiteral("fillet");
        case FeatureKind::Chamfer: return QStringLiteral("chamfer");
        case FeatureKind::Shell: return QStringLiteral("shell");
        case FeatureKind::Hole: return QStringLiteral("cylinder");
        case FeatureKind::Thread: return QStringLiteral("cylinder");
        case FeatureKind::ShaftThread: return QStringLiteral("thread");
        case FeatureKind::DrillPoint: return QStringLiteral("drill-point");
    }
    return {};
}

void add_history_container_tree_children(QTreeWidgetItem* parent,
    const zima::document::HistoryContainer& container,
    const zima::assembly::InstancePath& instance_path,
    const zima::sketcher::Sketch* owned_sketch,
    bool assembly_owned) {
    if (container.feature_kind != zima::document::FeatureKind::Thread &&
        container.feature_kind != zima::document::FeatureKind::DrillPoint) {
        add_construction_origin_tree_item(
            parent, container.container_origin, container.name, instance_path);
    }
    if (container.feature_kind == zima::document::FeatureKind::Sweep3D) {
        auto* path = new QTreeWidgetItem(parent, {QObject::tr("Dráha")});
        path->setData(0, Qt::UserRole, QString::fromStdString(container.sweep3d.path.id));
        path->setData(0, Qt::UserRole + 1, QString::fromStdString(instance_path.encoded()));
        path->setData(0, Qt::UserRole + 3, "sweep3d-path");
        path->setIcon(0, resource_icon("sketch-3d"));
        add_construction_tree_children(path, container.sweep3d.path, instance_path);
        path->setExpanded(true);
        for (const auto& profile : container.sweep3d.profiles) {
            const auto point = std::ranges::find(container.sweep3d.path.curve_points,
                profile.point_id, &zima::document::ConstructionObject::id);
            const auto number = std::distance(container.sweep3d.path.curve_points.begin(), point) + 1;
            const auto station = number == 1 ? QStringLiteral("1")
                : QStringLiteral("%1.%2").arg(number).arg(profile.incoming ? 1 : 2);
            auto* sketch = new QTreeWidgetItem(parent, {QObject::tr("Skica %1").arg(station)});
            sketch->setIcon(0, resource_icon("sketch"));
            sketch->setData(0, Qt::UserRole, QString::fromStdString(profile.id));
            sketch->setData(0, Qt::UserRole + 1, QString::fromStdString(instance_path.encoded()));
            sketch->setData(0, Qt::UserRole + 3, "sweep3d-profile");
        }
        return;
    }
    if(container.feature_kind==zima::document::FeatureKind::Sweep2D){
        for(std::size_t i=0;i<=container.sweep2d.profiles.size();++i){
            const auto label=i==0?QObject::tr("Dráha"):QObject::tr("Průřez %1").arg(i);
            auto* child=new QTreeWidgetItem(parent,{label});child->setIcon(0,resource_icon("sketch"));
            child->setData(0,Qt::UserRole,QString::fromStdString(container.id));child->setData(0,Qt::UserRole+1,QString::fromStdString(instance_path.encoded()));
            child->setData(0,Qt::UserRole+3,"part-sweep2d-sketch");child->setData(0,Qt::UserRole+6,static_cast<int>(i));}
        return;
    }
    if(container.feature_kind==zima::document::FeatureKind::HelicalSweep){
        const std::array<QString,3> names{QObject::tr("Základní kružnice"),QObject::tr("Radiální dráha"),QObject::tr("Průřez")};
        for(unsigned i=0;i<3;++i){auto* child=new QTreeWidgetItem(parent,{names[i]});child->setIcon(0,resource_icon("sketch"));
            child->setData(0,Qt::UserRole,QString::fromStdString(container.id));child->setData(0,Qt::UserRole+1,QString::fromStdString(instance_path.encoded()));
            child->setData(0,Qt::UserRole+3,"part-helical-sketch");child->setData(0,Qt::UserRole+6,static_cast<int>(i));}
        return;
    }
    if (container.feature_kind==zima::document::FeatureKind::Fillet ||
        container.feature_kind==zima::document::FeatureKind::Chamfer) {
        const auto configure=[&](QTreeWidgetItem* item,std::size_t route,
                std::optional<std::size_t> segment) {
            item->setData(0,Qt::UserRole,QString::fromStdString(container.id));
            item->setData(0,Qt::UserRole+1,QString::fromStdString(instance_path.encoded()));
            item->setData(0,Qt::UserRole+3,"part-treatment-component");
            item->setData(0,Qt::UserRole+5,QString("route:%1").arg(route)+
                (segment ? QString(":segment:%1").arg(*segment) : QString{}));
            item->setData(0,Qt::UserRole+6,static_cast<int>(route));
            item->setData(0,Qt::UserRole+7,segment ? static_cast<int>(*segment) : -1);
        };
        for (std::size_t route=0;route<container.edge_treatment.routes.size();++route) {
            auto* item=new QTreeWidgetItem(parent,{QObject::tr("Trasa %1").arg(route+1)});
            item->setIcon(0,resource_icon("sketch-polyline"));
            configure(item,route,std::nullopt);
            for (std::size_t segment=0;segment<container.edge_treatment.routes[route].size();++segment) {
                auto* child=new QTreeWidgetItem(item,{QObject::tr("Segment %1").arg(segment+1)});
                child->setIcon(0,resource_icon("sketch-segment"));
                configure(child,route,segment);
            }
        }
        return;
    }
    if (container.feature_kind == zima::document::FeatureKind::Thread) {
        const auto add_component=[&](const QString& label,const char* icon,const char* role) {
            auto* child=new QTreeWidgetItem(parent,{label});
            child->setIcon(0,resource_icon(icon));
            child->setData(0,Qt::UserRole,QString::fromStdString(container.id));
            child->setData(0,Qt::UserRole+1,QString::fromStdString(instance_path.encoded()));
            child->setData(0,Qt::UserRole+3,"part-opening-component");
            child->setData(0,Qt::UserRole+5,role);
        };
        add_component(QObject::tr("Otvor"),"cylinder","bore");
        if (container.thread.enabled)
            add_component(QObject::tr("Závit %1").arg(QString::fromStdString(container.thread.designation)),"thread","thread");
        if (container.thread.chamfer_enabled)
            add_component(QObject::tr("Sražení"),"chamfer","chamfer");
        if (container.hole.drill_point_enabled &&
            container.thread.end_condition_forward==zima::document::EndCondition::Length)
            add_component(QObject::tr("Špička"),"drill-point","tip");
        return;
    }
    if (container.feature_kind == zima::document::FeatureKind::Hole) {
        const auto add_hole_part = [&](const QString& label,
                const char* icon, const char* role) {
            auto* child = new QTreeWidgetItem(parent, {label});
            child->setIcon(0, resource_icon(icon));
            child->setData(0, Qt::UserRole,
                QString::fromStdString(container.id));
            child->setData(0, Qt::UserRole + 3, "part-hole-component");
            child->setData(0, Qt::UserRole + 5, role);
        };
        add_hole_part(QObject::tr("Rovina XY – otvor"), "plane", "bore-plane");
        add_hole_part(QObject::tr("Skica otvoru – kružnice"), "sketch", "bore-sketch");
        add_hole_part(QObject::tr("Válcové odebrání"), "protrusion", "bore");
        if (container.hole.entrance_chamfer > 0.0) {
            add_hole_part(QObject::tr("Rovina XZ – sražení"), "plane",
                "chamfer-plane");
            add_hole_part(QObject::tr("Skica sražení"), "sketch",
                "chamfer-sketch");
            add_hole_part(QObject::tr("Rotační sražení"), "revolve", "chamfer");
        }
        if (container.hole.drill_point_enabled) {
            add_hole_part(QObject::tr("Rovina XZ – špička"), "plane",
                "tip-plane");
            add_hole_part(QObject::tr("Skica špičky"), "sketch", "tip-sketch");
            add_hole_part(QObject::tr("Rotační špička"), "revolve", "tip");
        } else if (container.hole.exit_chamfer_enabled) {
            add_hole_part(QObject::tr("Rovina XZ – konec"), "plane",
                "tip-plane");
            add_hole_part(QObject::tr("Skica sražení konce"), "sketch",
                "tip-sketch");
            add_hole_part(QObject::tr("Rotační sražení konce"), "revolve",
                "tip");
        }
        if (container.hole.thread_enabled) {
            add_hole_part(QObject::tr("Závitový drát"), "cylinder", "thread");
        }
    }
    if (owned_sketch != nullptr) {
        auto* plane = new QTreeWidgetItem(parent, {QObject::tr("Rovina")});
        plane->setIcon(0, resource_icon("plane"));
        plane->setData(0, Qt::UserRole,
            QString::fromStdString(owned_sketch->id));
        plane->setData(0, Qt::UserRole + 3,
            assembly_owned ? "assembly-sketch-plane" : "part-sketch-plane");
        auto* sketch = new QTreeWidgetItem(parent,
            {QString::fromStdString(owned_sketch->name)});
        sketch->setIcon(0, resource_icon("sketch"));
        sketch->setData(0, Qt::UserRole,
            QString::fromStdString(owned_sketch->id));
        sketch->setData(0, Qt::UserRole + 3,
            assembly_owned ? "assembly-sketch" : "part-sketch");
    }
    if (container.feature_kind == zima::document::FeatureKind::Sketch) return;
    const QString operation = container.feature_kind ==
            zima::document::FeatureKind::Thread
        ? QString{} : container.combine_mode ==
            zima::document::CombineMode::Subtract
        ? QStringLiteral("− ") : QStringLiteral("+ ");
    const QString feature_label = container.feature_kind ==
            zima::document::FeatureKind::Thread
        ? QObject::tr("Plochy závitu")
        : operation + QString::fromStdString(container.name);
    auto* feature = new QTreeWidgetItem(parent, {feature_label});
    feature->setIcon(0, resource_icon(container.feature_kind==zima::document::FeatureKind::Thread &&
            !container.thread.enabled ? QStringLiteral("cylinder") : feature_icon_name(container.feature_kind)));
    feature->setData(0, Qt::UserRole, QString::fromStdString(
        assembly_owned ? container.id : container.feature_id));
    feature->setData(0, Qt::UserRole + 1,
        QString::fromStdString(instance_path.encoded()));
    feature->setData(0, Qt::UserRole + 3,
        assembly_owned ? "assembly-cut" : "part-container-entity");
    feature->setData(0, Qt::UserRole + 6,
        QString::fromStdString(container.id));
    const bool primitive_primary_axis =
        container.feature_kind == zima::document::FeatureKind::Cylinder ||
        container.feature_kind == zima::document::FeatureKind::Cone ||
        container.feature_kind == zima::document::FeatureKind::Hole;
    if (primitive_primary_axis ||
        (owned_sketch != nullptr &&
         (container.feature_kind == zima::document::FeatureKind::Extrusion ||
          container.feature_kind == zima::document::FeatureKind::Revolution))) {
        std::vector<std::array<double, 2>> centers;
        const auto append_center = [&](const std::string& point_id) {
            const auto* point = owned_sketch->find_point(point_id);
            if (point == nullptr || std::any_of(centers.begin(), centers.end(),
                    [&](const auto& center) {
                        return std::hypot(center[0]-point->x,
                                         center[1]-point->y) <= 1.0e-7;
                    })) return;
            centers.push_back({point->x, point->y});
        };
        if (primitive_primary_axis ||
            container.feature_kind == zima::document::FeatureKind::Revolution) {
            centers.push_back({});
        } else {
            for (const auto& circle : owned_sketch->circles)
                append_center(circle.center_point_id);
            for (const auto& ellipse : owned_sketch->ellipses)
                append_center(ellipse.center_point_id);
        }
        for (std::size_t index = 0; index < centers.size(); ++index) {
            const std::string semantic = index == 0 ? "axis:primary"
                : "axis:profile:" + std::to_string(index + 1);
            auto* axis = new QTreeWidgetItem(parent, {
                index == 0 ? QObject::tr("Osa")
                           : QObject::tr("Osa %1").arg(index + 1)});
            axis->setIcon(0, resource_icon("axis"));
            axis->setData(0, Qt::UserRole,
                QString::fromStdString(container.id));
            axis->setData(0, Qt::UserRole + 1,
                QString::fromStdString(instance_path.encoded()));
            axis->setData(0, Qt::UserRole + 3, "origin-reference");
            axis->setData(0, Qt::UserRole + 5,
                QString::fromStdString(semantic));
            axis->setData(0, Qt::UserRole + 6,
                QString::fromStdString(container.id));
        }
    }
}

void add_construction_tree_children(QTreeWidgetItem* parent,
    const zima::document::ConstructionObject& object,
    const zima::assembly::InstancePath& instance_path) {
    add_construction_origin_tree_item(
        parent, object.container_origin, object.name, instance_path,
        object.kind == zima::document::ConstructionKind::Point);
    if (object.kind == zima::document::ConstructionKind::Point) {
        return;
    }
    if (object.kind == zima::document::ConstructionKind::Curve3D) {
        for (const auto& point : object.curve_points) {
            auto* point_item = new QTreeWidgetItem(
                parent, {QString::fromStdString(point.name)});
            point_item->setIcon(0, resource_icon("point"));
            point_item->setData(0, Qt::UserRole,
                QString::fromStdString(point.id));
            point_item->setData(0, Qt::UserRole + 1,
                QString::fromStdString(instance_path.encoded()));
            point_item->setData(0, Qt::UserRole + 3, "curve3d-point");
            point_item->setData(0, Qt::UserRole + 4,
                QString::fromStdString(object.id));
            add_construction_tree_children(point_item, point, instance_path);
        }
        auto* entity = new QTreeWidgetItem(
            parent, {QString::fromStdString(object.name)});
        entity->setIcon(0, resource_icon("sketch-3d"));
        entity->setData(0, Qt::UserRole,
            QString::fromStdString(object.entity_id));
        entity->setData(0, Qt::UserRole + 1,
            QString::fromStdString(instance_path.encoded()));
        entity->setData(0, Qt::UserRole + 3, "construction-entity");
        entity->setData(0, Qt::UserRole + 5,
            "curve3d");
        return;
    }
    const bool axis = object.kind == zima::document::ConstructionKind::Axis;
    auto* entity = new QTreeWidgetItem(
        parent, {QString::fromStdString(object.name)});
    entity->setIcon(0, resource_icon(axis ? "axis" : "plane"));
    entity->setData(0, Qt::UserRole, QString::fromStdString(object.entity_id));
    entity->setData(0, Qt::UserRole + 1,
        QString::fromStdString(instance_path.encoded()));
    entity->setData(0, Qt::UserRole + 3, "construction-entity");
    entity->setData(0, Qt::UserRole + 5, axis ? "axis" : "plane");
}

} // namespace zima::app::workspace_detail
