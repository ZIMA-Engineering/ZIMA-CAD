#include <zima/viewer/mesh_view.hpp>
#include <zima/kernel/placement_route.hpp>
#include <QApplication>
#include <QMouseEvent>
#include <iostream>
#include <set>
#include <tuple>

int main(int argc,char** argv) {
    QApplication app(argc,argv);
    try {
        const auto check=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
        zima::kernel::ViewerMesh mesh;
        for(const auto& [owner,path,key]:std::vector<std::tuple<std::string,std::string,std::string>>{
                {"curve-a","one","curve:segment:a:b"},{"curve-b","one","curve:segment:c:d"},
                {"curve-a","two","curve:segment:a:b"},{"curve-a","one","curve:segment:b:e"}})
            mesh.edges.push_back({{{-10,0,0},{10,0,0}},{owner,key,path}});
        zima::viewer::MeshView view;view.resize(900,700);view.set_mesh(mesh);
        view.set_selection_contract({zima::viewer::CandidateKind::Edge});view.set_placement_route_offers(true);
        const QPointF pixel(view.rect().center());
        const auto offered=view.selection_candidates_at(pixel);
        std::set<std::tuple<std::string,std::string,std::string>> routes;
        for(std::size_t i=0;i<offered.size();++i) {
            const auto& member=offered[i];
            if(!zima::kernel::is_placement_route_member(member.semantic_key))continue;
            const auto key=zima::kernel::placement_route_key(member.semantic_key);
            if(!routes.emplace(member.owner_id,member.instance_path,key).second)continue;
            check(i+1<offered.size()&&offered[i+1].semantic_key==key&&offered[i+1].owner_id==member.owner_id&&
                offered[i+1].instance_path==member.instance_path,"Whole route is not immediately after its first segment");
        }
        check(routes.size()==3&&offered.size()==7,"Route offers lost or duplicated an occurrence or segment");
        const auto send=[&](QEvent::Type type,Qt::MouseButton button,Qt::MouseButtons buttons) {
            QMouseEvent event(type,pixel,QPointF(view.mapToGlobal(pixel.toPoint())),button,buttons,Qt::NoModifier);
            QApplication::sendEvent(&view,&event);
        };
        send(QEvent::MouseMove,Qt::NoButton,Qt::NoButton);
        check(view.hovered_candidate()==std::optional{offered[0]},"Hover differs from the shared first segment");
        send(QEvent::MouseButtonPress,Qt::RightButton,Qt::RightButton);send(QEvent::MouseButtonRelease,Qt::RightButton,Qt::NoButton);
        check(view.hovered_candidate()==std::optional{offered[1]},"One RMB cycle did not offer the whole route");
        send(QEvent::MouseButtonPress,Qt::LeftButton,Qt::LeftButton);send(QEvent::MouseButtonRelease,Qt::LeftButton,Qt::NoButton);
        check(view.confirmed_candidate()==std::optional{offered[1]},"LMB did not confirm the offered whole route");
        view.set_placement_route_offers(false);
        const auto segments=view.selection_candidates_at(pixel);
        check(segments.size()==4,"Disabling route offers changed the underlying segments");
        view.set_placement_route_offers(true);
        view.set_candidate_filter([](const auto& candidate){return !candidate.semantic_key.starts_with(zima::kernel::placement_route_prefix);});
        check(view.selection_candidates_at(pixel)==segments,"Route rejection reordered or removed the segment offers");
        std::cout<<"Route order: overlapping segments, repeated occurrences, deduplication, hover/RMB/LMB and filtering passed\n";
        return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
