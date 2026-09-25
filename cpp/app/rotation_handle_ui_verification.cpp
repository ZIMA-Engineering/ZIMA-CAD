#include "../tests/gui_profile_fixture.hpp"
#include "assembly_workspace_window.hpp"
#include <zima/viewer/mesh_view.hpp>
#include <QApplication>
#include <QMouseEvent>
#include <QKeyEvent>
#include <cmath>
#include <iostream>

namespace zima::app {
int verify_rotation_handle_ui(QApplication& application, AssemblyWorkspaceWindow& window,
    const std::filesystem::path& directory) {
    const auto check=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
    const auto flush=[&]{application.processEvents();};
    const auto run=[&](const char* command,commands::Json args=commands::Json::object()) {
        auto result=window.execute_console_command(QString::fromStdString(commands::Json{{"command",command},{"arguments",args}}.dump()));
        if(!result.ok)throw std::runtime_error(std::string(command)+": "+result.message);
        flush();return result.data;
    };
    try {
        window.showMaximized();flush();
        const auto stem="rotation-handle-"+document::PartDocument::create_default().document_id;
        run("new",{{"type","part"},{"name",stem+"-part"}});
        zima::test::gui_rectangular_profile(window,30,10,8).data;
        run("save");
        const auto part=document::PartDocument::load(directory/(stem+"-part.prtz"));
        run("new",{{"type","assembly"},{"name",stem}});
        const std::string fixed=run("component.insert",{{"source",part.document_id}}).at("occurrence");
        const std::string moving=run("component.insert",{{"source",part.document_id}}).at("occurrence");
        const auto path=[](const auto& id){return assembly::InstancePath{}.child(id).encoded();};
        run("component.set",{{"instance_path",path(fixed)},{"grounded",true}});
        const auto ref=[&](const auto& id,const char* key) {return commands::Json{{"instance_path",path(id)},{"owner",part.document_id+":origin"},{"key",key}};};
        auto rows=commands::Json::array({
            {{"kind","axis_coincident"},{"component",ref(moving,"origin:axis:z")},{"target",ref(fixed,"origin:axis:z")},{"offset",0}},
            {{"kind","plane_coincident"},{"component",ref(moving,"origin:plane:xy")},{"target",ref(fixed,"origin:plane:xy")},{"offset",8}},
            {{"kind","plane_angle"},{"component",ref(moving,"origin:plane:yz")},{"target",ref(fixed,"origin:plane:yz")},{"offset",0},{"lower_limit",-180},{"upper_limit",180}}});
        run("component.set",{{"instance_path",path(moving)},{"grounded",false},{"placement_references",rows}});
        const std::string follower=run("component.insert",{{"source",part.document_id}}).at("occurrence");
        auto following=rows;
        for(auto& row:following) {
            row["component"]["instance_path"]=path(follower);
            row["target"]["instance_path"]=path(moving);
            row["offset"]=0;
        }
        run("component.set",{{"instance_path",path(follower)},{"grounded",false},{"placement_references",following}});
        run("regenerate");run("save");
        const auto file=directory/(stem+".asmz");
        const auto doc=assembly::AssemblyDocument::load(file);
        auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QWidget*>("modelWorkspace"));check(view,"View missing");
        view->set_projection_mode(viewer::ProjectionMode::Orthographic);
        view->set_camera_state({1,0,0,0,60,0,0,60});
        const auto mouse=[&](QEvent::Type type,QPointF position,Qt::MouseButton button,Qt::MouseButtons buttons) {
            QMouseEvent event(type,position,QPointF(view->mapToGlobal(position.toPoint())),button,buttons,Qt::NoModifier);
            QApplication::sendEvent(view,&event);flush();
        };
        const auto show=[&] {
            view->confirm_occurrence(path(moving));
            mouse(QEvent::MouseButtonDblClick,{view->width()/2.,view->height()/2.},Qt::LeftButton,Qt::LeftButton);
        };
        const auto candidate=[&] {
            const auto& dimensions=view->mesh().dimensions;
            for(std::size_t i=0;i<dimensions.size();++i)if(dimensions[i].rotation_handle&&dimensions[i].reference.semantic_key=="placement-reference:"+moving+":2")
                return viewer::ViewerCandidate{viewer::CandidateKind::Dimension,0,i,doc.document_id,dimensions[i].reference.semantic_key,{}};
            throw std::runtime_error("Hinge radial control missing");
        };
        const auto value=[&]{return run("component.get",{{"instance_path",path(moving)}}).at("placement_references")[2].at("offset").get<double>();};
        const auto project=[&](kernel::Vec3 p) {
            const auto world=[&](QPointF pixel) {
                const auto ray=view->ray_at(pixel);check(ray.has_value(),"No viewport ray");
                const double t=(p.z-ray->first.z)/ray->second.z;
                return kernel::Vec3{ray->first.x+t*ray->second.x,ray->first.y+t*ray->second.y,p.z};
            };
            const auto zero=world({0,0}),x=world({100,0}),y=world({0,100});
            const double ax=x.x-zero.x,ay=x.y-zero.y,bx=y.x-zero.x,by=y.y-zero.y,det=ax*by-ay*bx;
            return QPointF(100*((p.x-zero.x)*by-(p.y-zero.y)*bx)/det,100*(ax*(p.y-zero.y)-ay*(p.x-zero.x))/det);
        };
        const auto drag=[&](double degrees,bool cancel) {
            show();const auto c=candidate();const auto d=view->mesh().dimensions[c.geometry_index];
            const auto tip=view->dimension_handle_position(c,0);check(tip.has_value(),"Grip missing");
            const auto offered=view->selection_candidates_at(*tip);
            check(!offered.empty()&&offered.front().semantic_key==c.semantic_key,"Common picker does not offer radial endpoint first");
            const double radians=degrees*std::acos(-1.)/180;
            auto u=kernel::Vec3{d.line_first.x-d.witness_first.x,d.line_first.y-d.witness_first.y,d.line_first.z-d.witness_first.z};
            const auto n=d.plane_normal;const double nl=std::hypot(std::hypot(n.x,n.y),n.z);
            const kernel::Vec3 v{(n.y*u.z-n.z*u.y)/nl,(n.z*u.x-n.x*u.z)/nl,(n.x*u.y-n.y*u.x)/nl};
            const auto at=project({d.witness_first.x+u.x*std::cos(radians)+v.x*std::sin(radians),d.witness_first.y+u.y*std::cos(radians)+v.y*std::sin(radians),d.witness_first.z+u.z*std::cos(radians)+v.z*std::sin(radians)});
            mouse(QEvent::MouseMove,*tip,Qt::NoButton,Qt::NoButton);
            mouse(QEvent::MouseButtonPress,*tip,Qt::LeftButton,Qt::LeftButton);
            mouse(QEvent::MouseMove,*tip+QPointF(3,3),Qt::NoButton,Qt::LeftButton);
            mouse(QEvent::MouseMove,at,Qt::NoButton,Qt::LeftButton);
            if(cancel){QKeyEvent escape(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);QApplication::sendEvent(view,&escape);flush();}
            mouse(QEvent::MouseButtonRelease,at,Qt::LeftButton,Qt::NoButton);
        };
        drag(45,false);check(std::abs(value()-45)<.05,"Radial drag did not commit the angle");
        run("undo");check(std::abs(value())<1e-8,"Radial drag was not one Undo step");
        run("redo");check(std::abs(value()-45)<.05,"Radial drag Redo failed");
        drag(90,true);check(std::abs(value()-45)<.05,"Escape committed radial drag");
        drag(170,false);drag(190,false);check(std::abs(value()+170)<.05,"Radial drag did not cross 180 degrees in the signed angle range");
        run("save");const auto saved=assembly::AssemblyDocument::load(file);
        check(std::abs(saved.find_occurrence(moving)->placement_references[2].offset+170)<.05,"Native file lost dragged angle");
        const auto driver_plane=saved.resolve_plane(saved.find_occurrence(moving)->placement_references[2].component_reference).plane;
        const auto follower_plane=saved.resolve_plane(saved.find_occurrence(follower)->placement_references[2].component_reference).plane;
        check(driver_plane.normal.x*follower_plane.normal.x+driver_plane.normal.y*follower_plane.normal.y+driver_plane.normal.z*follower_plane.normal.z>1-1e-7,
            "Dependent mechanism component did not follow the dragged angle");
        rows[2]["offset"]=0;rows[2]["lower_limit"]=-90;rows[2]["upper_limit"]=90;
        run("component.set",{{"instance_path",path(moving)},{"placement_references",rows}});
        drag(120,false);check(std::abs(value()-90)<.05,"Rotation arm ignored the angle limit");
        show();window.grab().save(QString::fromStdString((directory/"rotation-handle.png").string()));
        std::cout<<"Rotation arm: common picker, angle editing, Undo/Redo, Escape, 180-degree crossing and native save passed\n";
        return 0;
    }catch(const std::exception& error){std::cerr<<"Rotation arm GUI: "<<error.what()<<'\n';window.grab().save(QString::fromStdString((directory/"rotation-handle-failure.png").string()));return 1;}
}
}
