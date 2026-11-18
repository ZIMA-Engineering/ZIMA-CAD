#include "../app/part_reference_index.hpp"
#include "../app/reference_tree_policy.hpp"
#include "../app/tree_reference_state.hpp"
#include <QApplication>
#include <iostream>
#include <stdexcept>

using namespace zima;
void require(bool value,const char* message) { if(!value)throw std::runtime_error(message); }
template<class Document,class Index> void verify(Document document,Index index) {
    auto curve=document::PartDocument::create_construction(document::ConstructionKind::Curve3D);
    auto first=document::create_owned_point(curve.id),second=document::create_owned_point(curve.id);
    first.origin={10,0,0};second.origin={10,20,0};
    // One plane constrains only one translation direction; this is valid.
    second.definition=document::ConstructionDefinition::PointReference;
    second.references={{{},first.container_origin.id,"origin:plane:yz"}};
    curve.curve_points={first,second};document.constructions={curve};document.resolve_constructions();
    auto issue=[&]{return app::construction_reference_issue(document.constructions.front(),index(document));};
    require(document.constructions.front().reference_valid,"Fixture reference did not resolve");
    require(issue().empty(),"Hidden child Origin falsely marks an underconstrained curve as missing");
    QTreeWidgetItem row;app::TreeReferenceState state;
    auto& reference=document.constructions.front().curve_points[1].references.front();
    const auto original=reference;
    reference.semantic_key="origin:plane:missing";
    state.apply(&row,"document",curve.id,issue());
    require(row.data(0,app::missing_reference_role).toBool(),"Unknown datum key was accepted");
    reference=original;reference.owner_id="deleted-origin";
    require(!issue().empty(),"Deleted datum owner was accepted");
    reference=original;
    state.apply(&row,"document",curve.id,issue());
    require(!row.data(0,app::missing_reference_role).toBool()&&row.toolTip(0).isEmpty()&&row.background(0).style()==Qt::NoBrush,"Repaired reference retained the red state");
    document.constructions.front().curve_points.front().reference_valid=false;
    require(!issue().empty(),"Invalid source point was accepted");
    document.constructions.front().curve_points.front().reference_valid=true;
    document.constructions.front().curve_points.front().suppressed=true;
    require(!issue().empty(),"Suppressed source point was accepted");
}
int main(int argc,char** argv) {QApplication app(argc,argv);try {
    {
        auto part=document::PartDocument::create_default();
        auto curve=document::PartDocument::create_construction(document::ConstructionKind::Curve3D);
        auto a=document::create_owned_point(curve.id),b=document::create_owned_point(curve.id),c=document::create_owned_point(curve.id);
        a.origin={0,0,0};b.origin={100,0,0};c.origin={154,0,36};curve.curve_points={a,b,c};part.constructions={curve};
        auto sketch=sketcher::Sketch::create_default();
        auto feature=document::PartDocument::create_feature_container(sketch.id);feature.feature.type=document::FeatureType::Point;
        sketch.owner_container_id=feature.id;part.sketches={sketch};
        feature.placement.references={{{},curve.entity_id,"placement-route:3d"}};part.history={feature};
        const auto stored=part.serialized();
        require(app::feature_reference_issue(part.history.front(),part,app::part_reference_index(part)).empty(),"Valid whole 3D route marks Point red");
        require(part.serialized()==stored,"Tree route validation changed the document");
        part.constructions.clear();require(!app::feature_reference_issue(part.history.front(),part,app::part_reference_index(part)).empty(),"Deleted 3D route was accepted");
        part.constructions={curve};part.history.front().placement.references.front().instance_path="missing-occurrence";
        require(!app::feature_reference_issue(part.history.front(),part,app::part_reference_index(part)).empty(),"Route from another occurrence was accepted");
        part.history.front().placement.references.front().instance_path.clear();
        part.history.front().placement.references.front().semantic_key="placement-route:missing";
        require(!app::feature_reference_issue(part.history.front(),part,app::part_reference_index(part)).empty(),"Unknown route was accepted");
        part.history.front().placement.references.front().semantic_key="placement-route:3d";
        auto reopened=document::PartDocument::from_serialized(part.serialized());
        QTreeWidgetItem row;app::TreeReferenceState state;
        state.apply(&row,reopened.document_id,feature.id,"missing");
        state.apply(&row,reopened.document_id,feature.id,app::feature_reference_issue(reopened.history.front(),reopened,app::part_reference_index(reopened)));
        require(!row.data(0,app::missing_reference_role).toBool(),"Reopened valid route retained red Tree state");
    }
    if(argc>1) {
        const auto part=document::PartDocument::load(argv[1]);
        const auto index=app::part_reference_index(part);
        for(const auto& object:part.constructions){const auto issue=app::construction_reference_issue(object,index);if(!issue.empty())throw std::runtime_error(issue);}
        for(const auto& feature:part.history){const auto issue=app::feature_reference_issue(feature,part,index);if(!issue.empty())throw std::runtime_error(feature.name+": "+issue);}
    }
    verify(document::PartDocument::create_default(),[](const auto& d){return app::part_reference_index(d);});
    verify(assembly::AssemblyDocument{},[](const auto& d){return app::assembly_reference_index(d);});
    std::cout<<"Construction Tree: hidden child origins, partial constraints, missing/invalid sources and repair passed\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
