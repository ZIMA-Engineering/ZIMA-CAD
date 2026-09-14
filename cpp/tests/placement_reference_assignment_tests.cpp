#include <zima/document/placement_reference_assignment.hpp>
#include <zima/ui/container_placement_section.hpp>
#include <QApplication>
#include <QVBoxLayout>
#include <QWidget>
#include <QPushButton>
#include <QTableWidget>
#include <iostream>
using namespace zima;using document::ConstructionReference;using Error=document::PlacementReferenceError;
namespace {
void require(bool value,const char* text){if(!value)throw std::runtime_error(text);}
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
void repeated_source() {
    std::vector<ConstructionReference> position,orientation;std::array<bool,3> locks{};const auto first=plane("A");
    require(document::assign_placement_reference({position,orientation,locks},true,0,first).error==Error::None,"Initial reference failed");
    require(document::assign_placement_reference({position,orientation,locks},true,0,first).error==Error::None,"Replacement of own row failed");
    require(orientation[1].owner_id.empty(),"Replacing a position reference with itself duplicated its automatic FRONT into TOP");
}
}
int main(int argc,char** argv){QApplication app(argc,argv);try{model();widget();repeated_source();removal_model();removal_widget();std::cout<<"Placement reference rows, exact occurrences, locks, orientation and GUI adapter passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
