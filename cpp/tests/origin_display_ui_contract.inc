#include "origin_display_controls.hpp"
#include "confirmed_face_hit.hpp"
#include <zima/document/container_origin_display.hpp>

static void verify_origin_display_and_pick_seed(QWidget& parent) {
    using namespace zima;
    const auto check=[](bool ok,const char* why){if(!ok)throw std::runtime_error(why);};
    using Kind=document::FeatureKind;
    for(const auto kind:{Kind::Sweep2D,Kind::Sweep3D,Kind::HelicalSweep}) {
        auto initial=kind==Kind::Sweep2D?document::PartDocument::create_sweep2d_container():
            kind==Kind::Sweep3D?document::PartDocument::create_sweep3d_container():document::PartDocument::create_helical_sweep_container();
        std::unique_ptr<ui::PropertiesSubWindow> dialog;
        if(kind==Kind::Sweep2D)dialog=std::make_unique<app::Sweep2DDialog>(initial,[](auto){},&parent);
        else if(kind==Kind::HelicalSweep)dialog=std::make_unique<app::HelicalSweepDialog>(initial,[](auto){},&parent);
        else dialog=std::make_unique<app::ConstructionPropertiesDialog>(initial,false,true,[](auto){},&parent);
        auto* custom=dialog->findChild<QCheckBox*>("sweepCustomPrecision");
        auto* tolerance=dialog->findChild<QDoubleSpinBox*>("sweepPrecisionTolerance");
        check(custom&&tolerance&&!custom->isChecked()&&!tolerance->isEnabled(),"Sweep precision default state incorrect");
        check(tolerance->value()==(kind==Kind::HelicalSweep?.1:.001),"Wrong factory sweep precision");
        custom->setChecked(true);tolerance->setValue(.012345678);
        const auto pending=[&] {
            if(auto* spatial=dynamic_cast<app::ConstructionPropertiesDialog*>(dialog.get()))return spatial->pending_sweep_value();
            return dynamic_cast<app::SweepPlacementDialog*>(dialog.get())->pending;
        };
        check(pending().sweep_precision.effective()==.012345678,"Sweep precision control lost the pending value");
        custom->setChecked(false);
        check(!pending().sweep_precision.custom_tolerance&&pending().sweep_precision.effective()==initial.sweep_precision.effective(),
            "Disabling custom precision did not restore the saved default");
    }
    for(const auto kind:{Kind::Extrusion,Kind::Revolution,Kind::TwistedSheet,Kind::Hole,Kind::Thread,
        Kind::ImportedStep,Kind::Fillet,Kind::Chamfer,Kind::Shell,Kind::DrillPoint}) {
        auto value=document::PartDocument::create_twisted_sheet_container();value.feature_kind=kind;
        if(kind==Kind::Hole)value=document::PartDocument::create_hole_container();
        if(kind==Kind::Thread)value=document::PartDocument::create_thread_container();
        app::PrimitivePropertiesDialog dialog(value,true,true,[](auto){},&parent);
        auto* point=dialog.findChild<QCheckBox*>("originShowPoint");
        auto* text=dialog.findChild<QCheckBox*>("originShowText");
        check(bool(point)==document::has_origin_display_controls(kind),"Wrong Origin visibility capability");
        if(!point)continue;
        check(text&&!point->isChecked()&&!text->isChecked(),"Origin visibility changed the default appearance");
        point->setChecked(true);text->setChecked(true);
        const auto pending=dialog.pending_value();
        check(pending.origin_point_visible&&pending.origin_text_visible,"Origin controls did not update pending metadata");
        const auto marker=document::container_origin_marker(pending);
        check(marker.always_visible&&marker.label==pending.name,"Origin marker lost visibility or label");
    }
    const std::vector<kernel::Vec3> triangles{{0,0,5},{20,0,5},{0,20,5},{0,0,0},{20,0,0},{0,20,0}};
    const auto hit=app::confirmed_face_hit(triangles,{3,4,10},{0,0,-1});
    check(hit&&hit->x==3&&hit->y==4&&hit->z==5,"Face hit used a triangle center or the farther surface");
    check(!app::confirmed_face_hit(triangles,{30,30,10},{0,0,-1}),"Face hit accepted an outside click");
    for(const bool locked:{false,true}) {
        QWidget holder;QVBoxLayout layout(&holder);
        ui::ContainerPlacementSection placement(&holder,&layout,true,true,3);
        document::Placement initial;initial.x=9;if(locked)initial.value_locks.insert("x");
        placement.initialize_numeric_values(initial);
        document::ConstructionReference reference{{},"face-owner","face",0,true};
        const std::array exact{3.123456789,4.234567891,5.345678912};
        reference.picked_position=exact;
        QString error;check(placement.set_reference(0,reference,"Face",&error),"First face reference rejected");
        const auto value=placement.numeric_placement();
        check(value.x==(locked?9:exact[0])&&value.y==exact[1]&&value.z==exact[2],"Initial click rounded position or ignored numeric lock");
        reference.owner_id="second-face";reference.picked_position=std::array{100.,200.,300.};
        check(placement.set_reference(1,reference,"Second face",&error),"Second reference rejected");
        check(placement.numeric_placement().y==exact[1],"A later reference reused initial click positioning");
    }
}
