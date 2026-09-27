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
    if(argc>1) {
        const auto part=document::PartDocument::load(argv[1]);
        const auto index=app::part_reference_index(part);
        for(const auto& object:part.constructions){const auto issue=app::construction_reference_issue(object,index);if(!issue.empty())throw std::runtime_error(issue);}
    }
    verify(document::PartDocument::create_default(),[](const auto& d){return app::part_reference_index(d);});
    verify(assembly::AssemblyDocument{},[](const auto& d){return app::assembly_reference_index(d);});
    std::cout<<"Construction Tree: hidden child origins, partial constraints, missing/invalid sources and repair passed\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
