#include <zima/document/placement_reference_assignment.hpp>
#include <zima/document/placement_json.hpp>
#include <zima/document/placement_orientation.hpp>
#include <zima/document/placement_surface.hpp>
#include <zima/ui/container_placement_section.hpp>
#include <QApplication>
#include <QCheckBox>
#include <QVBoxLayout>
#include <QWidget>
#include <QPushButton>
#include <QTableWidget>
#include <QToolButton>
#include <cmath>
#include <iostream>
using namespace zima;using document::ConstructionReference;using Error=document::PlacementReferenceError;
namespace {
void require(bool value,const char* text){if(!value)throw std::runtime_error(text);}
void near(double value,double expected,const char* text){if(!std::isfinite(value)||std::abs(value-expected)>1e-9)throw std::runtime_error(text);}
ConstructionReference plane(std::string owner,std::string path={}) {ConstructionReference r;r.owner_id=std::move(owner);r.instance_path=std::move(path);r.semantic_key="plane";r.supports_offset=true;return r;}
void model() {
    std::vector<ConstructionReference> position,orientation;std::array<bool,3> locks{};
    const auto assign=[&](std::size_t i,ConstructionReference ref,bool automatic=true,bool oriented=true){return document::assign_placement_reference({position,orientation,locks},oriented,i,std::move(ref),automatic);};
    auto first=plane("A","one");first.offset=4;
    require(assign(0,first).mirrored_orientation==0&&position.size()==1&&orientation.size()==2,"First plane did not seed FRONT");
    require(!position[0].orientation_only&&!position[0].offset_locked&&position[0].offset==4&&orientation[0].orientation_only&&orientation[0].orientation_drives_rotation&&orientation[0].orientation_role=="front","Position and FRONT contracts merged");
    require(assign(1,first).error==Error::Duplicate&&position.size()==1,"Duplicate positional source accepted");
    auto second=plane("A","two");require(assign(1,second).mirrored_orientation==1&&orientation[1].instance_path=="two","Different source occurrence merged");
    require(assign(4,first).error==Error::Duplicate&&orientation[1].instance_path=="two","Duplicate orientation source replaced TOP");
    require(assign(5,plane("B")).error==Error::InvalidSlot&&assign(3,plane("B"),false,false).error==Error::InvalidSlot,"Invalid/disabled orientation slot accepted");
    position[0].offset_locked=true;auto replacement=plane("C");replacement.offset=99;
    require(assign(0,replacement,false).error==Error::MissingMeasuredOffset&&position[0].owner_id=="A","A locked row lost its source without a measured distance");
    replacement.measured_offset=12.5;require(assign(0,replacement,false).error==Error::None&&position[0].offset==12.5&&position[0].offset_locked&&!position[0].measured_offset,"Locked replacement lost measured distance");
    position.clear();orientation.clear();locks[2]=true;replacement.measured_offset=-7;
    require(assign(2,replacement,false).error==Error::None&&position.size()==3&&position[2].offset==-7&&!position[2].offset_locked&&!locks[2],"Empty-slot capture did not release its one-shot lock");
    auto point=plane("point");point.supports_offset=false;point.offset=9;point.measured_offset=3;
    require(assign(1,point).error==Error::None&&position[1].offset==0&&position[1].offset_locked&&!position[1].measured_offset&&orientation.empty(),"Point reference retained a meaningless offset or added orientation");
}
void widget() {
    QWidget owner;auto* layout=new QVBoxLayout(&owner);ui::ContainerPlacementSection section(&owner,layout,true);QString error;int changed=0;
    section.set_changed_callback([&]{++changed;});auto first=plane("A");first.offset_locked=true;first.offset=2;
    section.initialize_from_references({first},{});auto replacement=plane("B");replacement.measured_offset=6;
    require(section.set_reference(0,replacement,QStringLiteral("Face B"),&error)&&error.isEmpty()&&changed==1,"GUI reference assignment or callback failed");
    require(section.references()[0].offset==6&&section.references()[0].offset_locked&&section.orientation_references()[0].orientation_only,"GUI lost shared lock or orientation state");
    require(!section.set_reference(1,replacement,{},&error)&&!error.isEmpty()&&changed==1,"GUI duplicate assignment changed pending values");
    require(section.combined_references(3).size()==2,"GUI failed to retain separate position and FRONT entries");
    require(section.set_reference(4,plane("TOP"),{},&error)&&section.orientation_references()[1].orientation_role=="top"&&changed==2,"GUI explicit TOP assignment changed contract");
    const auto before=section.references();auto missing=plane("C");
    require(!section.set_reference(0,missing,{},&error)&&section.references()==before&&!error.isEmpty()&&changed==2,"GUI missing locked measurement changed its reference");
    section.initialize_from_references({},{});changed=0;
    require(section.set_reference(0,plane("Repeated"),{},&error)&&section.set_reference(0,plane("Repeated"),{},&error),"GUI repeat of same position failed");
    require(section.combined_references(3).size()==2&&section.orientation_references()[1].owner_id.empty()&&changed==2,"GUI repeated source duplicated its automatic orientation");
}
void position_orientation_column() {
    QWidget owner;
    auto* layout=new QVBoxLayout(&owner);
    ui::ContainerPlacementSection section(&owner,layout,true);
    require(section.set_reference(0,plane("First"),{},nullptr) &&
            section.set_reference(1,plane("Second"),{},nullptr),
        "Cannot enter the position-orientation test references");
    const auto check=[&](int row) {
        auto* cell=section.reference_table()->cellWidget(row,5);
        auto* box=cell?cell->findChild<QCheckBox*>():nullptr;
        require(box&&box->isEnabled(),"Position reference has no orientation control");
        return box;
    };
    require(check(0)->isChecked()&&!check(1)->isChecked(),
        "The first direction did not own the default orientation");
    check(1)->click();
    require(!check(0)->isChecked()&&check(1)->isChecked() &&
            section.references()[0].owner_id=="First"&&section.references()[1].owner_id=="Second",
        "Switching orientation modified the position references");
    require(section.set_reference(1,plane("Replacement"),{},nullptr)&&check(1)->isChecked(),
        "Replacement lost the row's orientation selection");
    check(1)->click();
    auto saved=nlohmann::json(section.combined_references(3)).get<std::vector<ConstructionReference>>();
    document::normalize_container_front_references(saved);
    section.initialize_from_references(saved,{});
    section.refresh_reference_table();
    require(!check(0)->isChecked()&&!check(1)->isChecked(),
        "Reloading restored an explicitly disabled orientation");

    kernel::ViewerReferenceGeometry geometry;
    kernel::ViewerEdge edge;
    edge.reference={"Earlier edge","edge",{}};
    edge.points={{0,0,0},{10,0,0}};
    geometry.edges.push_back(edge);
    section.set_branch_geometry_resolver([&]() -> const auto& { return geometry; });
    section.initialize_from_references({{{},"Earlier edge","edge"}},{});
    section.refresh_reference_table();
    require(!check(0)->isChecked(),"An existing position-only edge changed orientation on open");
    check(0)->click();
    require(check(0)->isChecked()&&section.references()[0].orientation_role=="front",
        "An existing edge could not be chosen to drive orientation");
}
void removal_model() {
    auto first=plane("A","one"),second=plane("A","two"),third=plane("C");
    first.offset=2;first.offset_locked=true;second.offset=7;second.offset_locked=true;
    auto front=first,top=second;front.orientation_only=top.orientation_only=true;
    front.orientation_role="front";top.orientation_role="top";
    for(std::size_t slot=0;slot<3;++slot) {
        std::vector<ConstructionReference> position{first,second,third},orientation{front,top};
        std::array<bool,3> locks{true,true,true};
        const auto removed=document::remove_placement_reference({position,orientation,locks},true,slot);
        require(removed.changed&&removed.error==Error::None&&!locks[slot],"Removing a position did not release its own lock");
        require(position.size()==(slot==2?2:3),"Removing a position shifted or retained trailing rows");
        for(std::size_t other=0;other<3;++other)if(other!=slot) {
            const auto expected=other==0?first:other==1?second:third;
            require(position[other]==expected&&locks[other],"Removing a position changed a surviving row or lock");
        }
        if(slot<2) {
            require(position[slot].owner_id.empty()&&removed.paired_orientation==slot&&orientation.size()==1,
                "Position hole or exact paired orientation was lost");
            require(orientation[0].instance_path==(slot==0?"two":"one")&&orientation[0].orientation_role=="front",
                "Repeated occurrence was confused or remaining orientation was not reassigned FRONT");
        } else require(!removed.paired_orientation&&orientation==std::vector<ConstructionReference>{front,top},
            "Unrelated orientations changed");
        const auto saved_position=position,saved_orientation=orientation;const auto saved_locks=locks;
        require(!document::remove_placement_reference({position,orientation,locks},true,slot).changed&&
            position==saved_position&&orientation==saved_orientation&&locks==saved_locks,"Removing an empty slot was not a no-op");
    }
    for(std::size_t slot=0;slot<2;++slot) {
        std::vector<ConstructionReference> position{first,second,third},orientation{front,top};
        std::array<bool,3> locks{true,true,true};
        const auto removed=document::remove_placement_reference({position,orientation,locks},true,slot+3);
        require(removed.changed&&!removed.paired_orientation&&orientation.size()==2&&orientation[slot].owner_id.empty(),
            "Direct orientation removal compacted the other orientation");
        require(orientation[1-slot]==(slot==0?top:front)&&position==std::vector<ConstructionReference>{first,second,third}&&
            locks==std::array<bool,3>{true,true,true},"Direct orientation removal changed another row");
        const auto saved=orientation;
        require(document::remove_placement_reference({position,orientation,locks},true,5).error==Error::InvalidSlot&&
            document::remove_placement_reference({position,orientation,locks},false,3).error==Error::InvalidSlot&&orientation==saved,
            "Invalid or disabled orientation removal changed data");
    }
}
void removal_widget() {
    QWidget owner;auto* layout=new QVBoxLayout(&owner);ui::ContainerPlacementSection section(&owner,layout,true);
    auto first=plane("A","one"),second=plane("A","two"),third=plane("C");
    first.offset=2;second.offset=7;second.offset_locked=true;
    auto front=first,top=second;front.orientation_only=top.orientation_only=true;
    front.orientation_drives_rotation=top.orientation_drives_rotation=true;
    front.orientation_role="front";top.orientation_role="top";
    section.initialize_from_references({first,second,third,front,top},{});
    section.refresh_reference_table();section.refresh_orientation_table();
    section.set_reference_inspected(3,true);section.set_reference_inspected(4,true);
    std::vector<std::size_t> callbacks;
    section.set_changed_callback([&]{callbacks.push_back(99);});
    section.set_highlights_changed_callback([&]{callbacks.push_back(88);});
    section.set_reference_request_callback([&](std::size_t slot){callbacks.push_back(slot);});
    const auto click=[](QTableWidget* table,int row) {
        auto* cell=table->cellWidget(row,0);auto* button=cell?cell->findChild<QPushButton*>():nullptr;
        require(button!=nullptr,"The reference row has no remove button");button->click();
    };
    click(section.reference_table(),0);
    require(callbacks==std::vector<std::size_t>{99,88,0},"GUI did not rearm picking after the preview callback");
    require(section.references().size()==3&&section.references()[0].owner_id.empty()&&section.references()[1]==second&&
        section.references()[2]==third,"GUI shifted or changed surviving positional rows");
    const auto inspected=section.highlighted_reference_entries();
    require(inspected.size()==1&&inspected[0].instance_path=="two"&&inspected[0].orientation_role=="front",
        "GUI inspection did not follow the surviving exact orientation source");
    require(section.combined_references(3).size()==3,"GUI persistence did not compact only its empty position");
    callbacks.clear();click(section.orientation_table(),0);
    require(callbacks==std::vector<std::size_t>{99,88,3}&&section.orientation_references()[0].owner_id.empty()&&
        section.references()[1]==second&&section.highlighted_reference_entries().empty(),
        "Direct GUI orientation removal changed position or retained stale inspection");
    callbacks.clear();click(section.reference_table(),2);
    require(callbacks==std::vector<std::size_t>{99,88,2}&&section.references().size()==2&&section.references()[1]==second,
        "GUI did not remove trailing position while retaining the earlier hole");
}
void final_removal_preserves_frame() {
    QWidget owner;auto* layout=new QVBoxLayout(&owner);ui::ContainerPlacementSection section(&owner,layout,true,true);
    document::Placement initial;
    initial.x=4;initial.y=5;initial.z=6;
    initial.absolute_rotation_x=1;initial.absolute_rotation_y=2;initial.absolute_rotation_z=3;
    initial.rotation_x=17;initial.rotation_y=-23;initial.rotation_z=91;
    initial.rotation_offset_x=7;initial.rotation_offset_y=8;initial.rotation_offset_z=9;
    initial.orientation_back=true;initial.orientation_quarter_turns=1;
    section.initialize_numeric_values(initial);
    auto first=plane("origin","yz"),second=plane("origin","xz"),third=plane("origin","xy");
    auto front=first,top=second;front.orientation_only=top.orientation_only=true;
    front.orientation_drives_rotation=top.orientation_drives_rotation=true;
    front.orientation_role="front";top.orientation_role="top";
    section.initialize_from_references({first,second,third,front,top},{});
    section.set_rotation_constraint_state({0,{true,true,true}});
    section.set_orientation_base_rotation({10,20,30},true);
    section.set_resolved_rotation({17,-23,91});
    section.refresh_reference_table();section.refresh_orientation_table();
    int previews=0;document::Placement final_preview;
    section.set_changed_callback([&] {
        ++previews;
        auto preview=section.numeric_placement();
        preview.references=section.combined_references(3);
        if(preview.references.empty()) {
            kernel::Vec3 base;bool from_reference=true;
            require(document::resolve_placement(preview,{},&base,&from_reference),
                "Free placement failed after removing its last reference");
            require(!from_reference,"Empty placement remained reference-driven");
            final_preview=preview;
            section.set_rotation_constraint_state({3,{false,false,false}});
            section.set_orientation_base_rotation(base,false);
            section.set_resolved_rotation({preview.rotation_x,preview.rotation_y,preview.rotation_z});
        }
    });
    const auto click=[&](int row) {
        auto* cell=section.reference_table()->cellWidget(row,0);
        auto* button=cell?cell->findChild<QPushButton*>():nullptr;
        require(button!=nullptr,"The STEP placement row has no remove button");button->click();
    };
    click(2);click(1);click(0);
    require(previews==3&&section.combined_references(3).empty(),
        "Bottom-up STEP reference removal did not publish every preview");
    require(final_preview.x==4&&final_preview.y==5&&final_preview.z==6&&
        final_preview.rotation_x==17&&final_preview.rotation_y==-23&&final_preview.rotation_z==91,
        "Removing the last STEP reference changed its displayed frame");
    require(final_preview.absolute_rotation_x==17&&final_preview.absolute_rotation_y==-23&&
        final_preview.absolute_rotation_z==91&&!final_preview.orientation_back&&
        final_preview.orientation_quarter_turns==0&&final_preview.rotation_offset_x==0&&
        final_preview.rotation_offset_y==0&&final_preview.rotation_offset_z==0,
        "The released STEP frame retained stale reference-only modifiers");
}
void mesh_capture() {
    QWidget owner;auto* layout=new QVBoxLayout(&owner);ui::ContainerPlacementSection section(&owner,layout,true);
    kernel::ViewerReferenceGeometry geometry;
    geometry.vertices={{0,0,0},{10,0,0},{0,10,0},{10,10,5}};
    geometry.triangles={0,1,2,1,3,2};geometry.triangle_references={{"mesh","face",{}},{"mesh","face",{}}};
    section.set_branch_geometry_resolver([&]() -> const kernel::ViewerReferenceGeometry& {return geometry;});
    auto first=plane("mesh");first.semantic_key="face";first.picked_position=std::array{2.,3.,0.};
    require(section.set_reference(0,first,QStringLiteral("Mesh"),nullptr),"General face input failed");
    auto* first_lock=section.reference_table()->cellWidget(1,3)->findChild<QToolButton*>("numericValueLockButton");
    require(first_lock&&first_lock->isChecked(),"General curved face did not visibly activate its next capture lock");
    for(std::size_t i=1;i<3;++i) {
        auto next=plane("plane-"+std::to_string(i));
        require(!section.set_reference(i,next,{},nullptr),"Mesh did not arm the next one-shot distance lock");
        next.measured_offset=i*2.5;
        require(section.set_reference(i,next,{},nullptr),"Measured mesh reference rejected");
        require(section.references()[i].offset==i*2.5&&!section.references()[i].offset_locked,"Mesh capture did not release its one-shot lock");
    }
}
void analytic_curved_capture() {
    for(const auto kind:{kernel::SurfaceGeometry::Kind::Cylinder,
            kernel::SurfaceGeometry::Kind::Cone,
            kernel::SurfaceGeometry::Kind::Torus}) {
        QWidget owner;auto* layout=new QVBoxLayout(&owner);
        ui::ContainerPlacementSection section(&owner,layout,true);
        kernel::SurfaceGeometry face;face.kind=kind;
        section.set_surface_resolver([&](const auto&) {return &face;});
        auto first=plane("analytic-curved-face");first.semantic_key="face";
        first.picked_position=std::array{4.,5.,6.};
        require(section.set_reference(0,first,QStringLiteral("Curved face"),nullptr),
            "Analytic curved View face input failed");
        auto lock_at=[&](int row) {
            auto* field=section.reference_table()->cellWidget(row,3);
            return field?field->findChild<QToolButton*>("numericValueLockButton"):nullptr;
        };
        require(lock_at(1)&&lock_at(1)->isChecked(),
            "Analytic curved face did not visibly activate the second-reference lock");
        auto second=plane("analytic-second");
        require(!section.set_reference(1,second,{},nullptr),
            "Analytic curved face silently assigned zero to its second reference");
        second.measured_offset=-12.5;
        require(section.set_reference(1,second,{},nullptr)&&
            section.references()[1].offset==-12.5&&!section.references()[1].offset_locked,
            "Analytic curved face did not capture and release its second signed offset");
        require(lock_at(1)&&!lock_at(1)->isChecked()&&lock_at(2)&&lock_at(2)->isChecked(),
            "Analytic curved capture did not visibly release row two and arm row three");
        auto third=plane("analytic-third");third.measured_offset=8.75;
        require(section.set_reference(2,third,{},nullptr)&&
            section.references()[2].offset==8.75&&!section.references()[2].offset_locked,
            "Analytic curved face did not capture and release its third signed offset");
    }
}
void planar_face_capture() {
    QWidget owner;auto* layout=new QVBoxLayout(&owner);ui::ContainerPlacementSection section(&owner,layout,true);
    kernel::ViewerReferenceGeometry geometry;
    const auto add_plane=[&](const char* owner,kernel::Vec3 origin,kernel::Vec3 axis,bool reversed=false) {
        auto surface=std::make_shared<kernel::SurfaceGeometry>();
        surface->kind=kernel::SurfaceGeometry::Kind::Plane;surface->origin=origin;
        surface->axis=axis;surface->radial={0,1,0};surface->reversed=reversed;
        geometry.triangle_references.push_back({owner,"plane","",std::move(surface)});
    };
    add_plane("planar-face",{400,230,262.637},{0,0,1});
    add_plane("planar-y",{400,230,136.224},{0,-1,0},true);
    add_plane("planar-x",{400,0,0},{-1,0,0});
    const double oblique_scale=std::sqrt(14.0);
    add_plane("planar-oblique",{9,-4,12},
        {1/oblique_scale,2/oblique_scale,3/oblique_scale});
    section.set_surface_resolver([&](const auto& reference) {
        return document::placement_surface(reference,geometry);
    });
    document::Placement initial;
    initial.x=411.;initial.y=222.;initial.z=100.;
    section.initialize_numeric_values(initial);
    auto first=plane("planar-face");
    first.picked_position=std::array{459.,199.,262.637};
    require(section.set_reference(0,first,QStringLiteral("Planar face"),nullptr),
        "Ordinary planar View face input failed");
    auto pending=section.numeric_placement();
    near(pending.x,0,"One planar support did not start at its owning Origin X");
    near(pending.y,0,"One planar support did not start at its owning Origin Y");
    near(pending.z,0,"One planar support did not start at its owning Origin Z");
    auto ordinary=pending;
    ordinary.references=section.combined_references(3);
    require(document::resolve_placement(ordinary,geometry),
        "Ordinary planar support did not resolve");
    near(ordinary.x,0,"Ordinary planar support did not preserve owning-Origin X");
    near(ordinary.y,0,"Ordinary planar support did not preserve owning-Origin Y");
    near(ordinary.z,262.637,"Ordinary planar support did not project onto its plane");
    require(section.set_reference(1,plane("ordinary-planar-limit"),{},nullptr),
        "Ordinary container unexpectedly armed Thread-only planar capture");
    section.initialize_from_references({},{});
    require(section.set_reference(0,first,QStringLiteral("Planar face"),nullptr),
        "Ordinary planar support could not be restored before replacement");
    section.initialize_numeric_values(ordinary);
    auto replacement=plane("planar-x");
    replacement.picked_position=std::array{400.,999.,888.};
    require(section.set_reference(0,replacement,QStringLiteral("Replacement plane"),nullptr),
        "Ordinary planar support replacement failed");
    auto replaced=section.numeric_placement();
    near(replaced.x,0,"Replacing the sole planar support retained its previous projected X");
    near(replaced.y,0,"Replacing the sole planar support retained its previous projected Y");
    near(replaced.z,0,"Replacing the sole planar support retained its previous projected Z");
    replaced.references=section.combined_references(3);
    require(document::resolve_placement(replaced,geometry),
        "Replaced ordinary planar support did not resolve");
    near(replaced.x,400.,"Replaced ordinary support did not project onto its plane");
    near(replaced.y,0,"Replaced ordinary support did not retain owning-Origin Y");
    near(replaced.z,0,"Replaced ordinary support did not retain owning-Origin Z");

    auto oblique=plane("planar-oblique");
    oblique.picked_position=std::array{777.,888.,999.};
    require(section.set_reference(0,oblique,QStringLiteral("Oblique plane"),nullptr),
        "Oblique planar support replacement failed");
    auto oblique_placement=section.numeric_placement();
    near(oblique_placement.x,0,"Oblique plane copied the View pick X");
    near(oblique_placement.y,0,"Oblique plane copied the View pick Y");
    near(oblique_placement.z,0,"Oblique plane copied the View pick Z");
    oblique_placement.references=section.combined_references(3);
    require(document::resolve_placement(oblique_placement,geometry),
        "Oblique planar support did not resolve");
    const kernel::Vec3 normal{1/oblique_scale,2/oblique_scale,3/oblique_scale};
    const kernel::Vec3 point{9,-4,12};
    const double distance=normal.x*point.x+normal.y*point.y+normal.z*point.z;
    near(oblique_placement.x,distance*normal.x,
        "Oblique support origin is not above the owning Origin X");
    near(oblique_placement.y,distance*normal.y,
        "Oblique support origin is not above the owning Origin Y");
    near(oblique_placement.z,distance*normal.z,
        "Oblique support origin is not above the owning Origin Z");

    // The real Properties callback writes constraint feedback into the numeric
    // fields synchronously. For an oblique plane its one pivot column must not
    // combine one solved coordinate with two old zero coordinates and project
    // that mixed seed a second time along the plane.
    QWidget live_owner;auto* live_layout=new QVBoxLayout(&live_owner);
    ui::ContainerPlacementSection live(&live_owner,live_layout,true);
    live.set_surface_resolver([&](const auto& reference) {
        return document::placement_surface(reference,geometry);
    });
    live.initialize_numeric_values(initial);
    live.set_changed_callback([&] {
        auto solved=live.numeric_placement();
        solved.references=live.combined_references(3);
        require(document::resolve_placement(solved,geometry),
            "Live one-plane placement did not resolve");
        live.set_translation_constraint_state(
            document::point_constraint_state(solved.references,geometry,
                {solved.x,solved.y,solved.z}),
            {solved.x,solved.y,solved.z});
    });
    auto live_oblique=oblique;
    live_oblique.picked_position=std::array{-700.,800.,-900.};
    require(live.set_reference(0,live_oblique,
        QStringLiteral("Live oblique plane"),nullptr),
        "Live oblique support replacement failed");
    auto live_result=live.numeric_placement();
    live_result.references=live.combined_references(3);
    require(document::resolve_placement(live_result,geometry),
        "Live oblique support was not stable after GUI feedback");
    near(live_result.x,oblique_placement.x,
        "GUI feedback drifted the one-plane projection X");
    near(live_result.y,oblique_placement.y,
        "GUI feedback drifted the one-plane projection Y");
    near(live_result.z,oblique_placement.z,
        "GUI feedback drifted the one-plane projection Z");

    section.initialize_numeric_values(oblique_placement);
    require(section.set_reference(0,replacement,QStringLiteral("Replacement plane"),nullptr),
        "Return from the oblique plane failed");
    auto returned=section.numeric_placement();
    returned.references=section.combined_references(3);
    require(document::resolve_placement(returned,geometry),
        "Returned planar support did not resolve");
    near(returned.x,400.,"Plane replacement accumulated the previous X");
    near(returned.y,0,"Plane replacement accumulated the previous Y");
    near(returned.z,0,"Plane replacement accumulated the previous Z");
    section.initialize_numeric_values(returned);
    require(section.set_reference(0,oblique,QStringLiteral("Oblique plane"),nullptr),
        "Repeated oblique-plane replacement failed");
    auto repeated_oblique=section.numeric_placement();
    repeated_oblique.references=section.combined_references(3);
    require(document::resolve_placement(repeated_oblique,geometry),
        "Repeated oblique planar support did not resolve");
    near(repeated_oblique.x,oblique_placement.x,
        "Repeated plane replacement drifted in X");
    near(repeated_oblique.y,oblique_placement.y,
        "Repeated plane replacement drifted in Y");
    near(repeated_oblique.z,oblique_placement.z,
        "Repeated plane replacement drifted in Z");

    section.initialize_numeric_values(repeated_oblique);
    auto second=plane("planar-y");
    require(section.set_reference(4,second,QStringLiteral("TOP plane"),nullptr),
        "Second orientation plane was rejected");
    auto two_planes=section.numeric_placement();
    near(two_planes.x,repeated_oblique.x,
        "Adding TOP moved the one-reference X position");
    near(two_planes.y,repeated_oblique.y,
        "Adding TOP moved the one-reference Y position");
    near(two_planes.z,repeated_oblique.z,
        "Adding TOP moved the one-reference Z position");
    two_planes.references=section.combined_references(3);
    require(document::resolve_placement(two_planes,geometry),
        "Position plane plus TOP did not resolve");
    near(two_planes.x,repeated_oblique.x,
        "Resolving TOP moved the one-reference X position");
    near(two_planes.y,repeated_oblique.y,
        "Resolving TOP moved the one-reference Y position");
    near(two_planes.z,repeated_oblique.z,
        "Resolving TOP moved the one-reference Z position");
    require(document::orientation_constraint_state(two_planes.references,
            geometry,true,{two_planes.x,two_planes.y,two_planes.z}).remaining_dof==0,
        "The second orientation plane did not fix the remaining rotation");
    section.initialize_from_references({},{});
    section.initialize_numeric_values({});
    section.set_planar_face_capture_policy(true);
    require(section.set_reference(0,first,QStringLiteral("Planar face"),nullptr),
        "Thread planar View face input failed");
    for(std::size_t i=1;i<3;++i) {
        auto next=plane(i==1?"planar-y":"planar-x");
        require(!section.set_reference(i,next,{},nullptr),
            "Planar View face did not arm the next one-shot distance lock");
        next.measured_offset=document::measure_placement_reference_offset(
            next,geometry,{459,199,262.637});
        require(next.measured_offset.has_value(),
            "Planar View face signed distance could not be measured");
        near(*next.measured_offset,i==1?-31.:-59.,
            "Planar View face measured the opposite signed side");
        require(section.set_reference(i,next,{},nullptr),
            "Measured planar-face reference rejected");
        require(section.references()[i].offset==*next.measured_offset&&
            !section.references()[i].offset_locked,
            "Planar-face capture did not preserve the signed distance and release its one-shot lock");
    }
    auto resolved=section.numeric_placement();
    resolved.references=section.combined_references(3);
    require(document::resolve_placement(resolved,geometry),
        "Captured planar Thread references did not resolve");
    near(resolved.x,459.,"Captured X side was mirrored after final resolve");
    near(resolved.y,199.,"Captured Y side was mirrored after final resolve");
    near(resolved.z,262.637,"Captured support face moved after final resolve");
    // Selecting a datum without a concrete View point keeps the ordinary
    // zero-offset workflow and must not arm the following rows.
    section.initialize_from_references({},{});
    require(section.set_reference(0,plane("datum"),QStringLiteral("Datum"),nullptr),
        "Datum reference input failed");
    require(section.set_reference(1,plane("datum-limit"),{},nullptr),
        "Datum reference unexpectedly required a measured face offset");
}
void repeated_source() {
    std::vector<ConstructionReference> position,orientation;std::array<bool,3> locks{};const auto first=plane("A");
    require(document::assign_placement_reference({position,orientation,locks},true,0,first).error==Error::None,"Initial reference failed");
    require(document::assign_placement_reference({position,orientation,locks},true,0,first).error==Error::None,"Replacement of own row failed");
    require(orientation[1].owner_id.empty(),"Replacing a position reference with itself duplicated its automatic FRONT into TOP");
}
void orientation_poles() {
    for(bool front:{false,true})for(double sign:{-1.,1.})for(double manual:{0.,17.,-32.})
    for(double correction:{0.,11.})for(double a:{-1e-15,1e-15})for(double b:{-1e-15,1e-15}) {
        const auto resolve=[&](bool rounded) {
            document::Placement placement;placement.absolute_rotation_y=front?manual:0.;
            placement.absolute_rotation_z=front?0.:manual;placement.rotation_offset_x=correction;
            auto ref=plane("pole");ref.orientation_drives_rotation=true;ref.orientation_role=front?"front":"top";
            placement.references={ref};kernel::ViewerReferenceGeometry geometry;
            auto surface=std::make_shared<kernel::SurfaceGeometry>();surface->kind=kernel::SurfaceGeometry::Kind::Plane;
            surface->axis=front?kernel::Vec3{rounded?a:0.,rounded?b:0.,sign}:kernel::Vec3{rounded?a:0.,sign,rounded?b:0.};
            geometry.triangle_references.push_back({ref.owner_id,ref.semantic_key,{},surface});
            require(document::resolve_placement(placement,geometry),"Polar reference did not resolve");
            require(document::orientation_constraint_state(placement.references,geometry,true).remaining_dof==1,"Polar reference consumed the free rotation");
            const auto stored=nlohmann::json(placement).dump();
            auto reopened=nlohmann::json::parse(stored).get<document::Placement>();
            require(reopened==placement,"Polar placement round trip changed its authored values");
            for(int repeat=0;repeat<3;++repeat) {
                require(document::resolve_placement(reopened,geometry),"Reopened polar placement did not resolve");
                near(reopened.rotation_x,placement.rotation_x,"Polar RX drifted after regeneration");
                near(reopened.rotation_y,placement.rotation_y,"Polar RY drifted after regeneration");
                near(reopened.rotation_z,placement.rotation_z,"Polar RZ drifted after regeneration");
            }
            return placement;
        };
        const auto exact=resolve(false),rounded=resolve(true);
        const auto matrix=[](const auto& p) {
            const double k=std::acos(-1.)/180.;
            const double cx=std::cos(p.rotation_x*k),sx=std::sin(p.rotation_x*k),cy=std::cos(p.rotation_y*k),sy=std::sin(p.rotation_y*k),cz=std::cos(p.rotation_z*k),sz=std::sin(p.rotation_z*k);
            return std::array{cz*cy,cz*sy*sx-sz*cx,cz*sy*cx+sz*sx,sz*cy,sz*sy*sx+cz*cx,sz*sy*cx-cz*sx,-sy,cy*sx,cy*cx};
        };
        const auto ideal=matrix(exact),actual=matrix(rounded);
        for(std::size_t i=0;i<ideal.size();++i)near(actual[i],ideal[i],"Near-polar reference introduced arbitrary twist");
    }
}
}
int main(int argc,char** argv){QApplication app(argc,argv);try{orientation_poles();model();widget();position_orientation_column();repeated_source();removal_model();removal_widget();final_removal_preserves_frame();mesh_capture();analytic_curved_capture();planar_face_capture();std::cout<<"Placement reference rows, exact occurrences, locks, orientation and GUI adapter passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
