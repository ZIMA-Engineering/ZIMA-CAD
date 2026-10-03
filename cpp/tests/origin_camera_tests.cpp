#include <zima/document/part_document.hpp>
#include <zima/viewer/mesh_view.hpp>
#include <QApplication>
#include <cmath>
#include <iostream>
#include <stdexcept>

int main(int argc,char** argv) {
    QApplication application(argc,argv);
    try {
        const auto origin=zima::document::PartDocument::create_default().origin_viewer_mesh();
        zima::viewer::MeshView direct;direct.resize(1000,700);direct.set_mesh(origin);
        const auto baseline=direct.camera_state()[7];
        if(baseline<=1)throw std::runtime_error("Origin fixture has no visible datum extent");
        for(int mode=0;mode<3;++mode) for(bool fit:{false,true}) {
            zima::viewer::MeshView delayed;delayed.resize(1000,700);
            zima::kernel::ViewerMesh initial;
            if(mode==1)initial.points.push_back({{0,0,0},{"part:origin","origin:point"}});
            if(mode==2)initial.vertices={{-5,-5,-5},{5,5,5}};
            delayed.set_mesh(initial);
            const auto before=delayed.camera_state();
            delayed.set_mesh(origin,fit);
            if(!fit)for(int i=0;i<7;++i)
                if(delayed.camera_state()[i]!=before[i])throw std::runtime_error("Datum initialization moved a preserved camera");
            if(std::abs(delayed.camera_state()[7]-baseline)>1e-5)
                throw std::runtime_error("First geometry without datum planes locked an oversized Origin baseline");
            auto zoom=delayed.camera_state();zoom[4]*=8;delayed.set_camera_state(zoom);
            delayed.fit_all();
            if(delayed.camera_state()[7]!=baseline)throw std::runtime_error("Fit changed established Origin size");
        }
        std::cout<<"Origin baseline survives delayed datum geometry, zoom and fit\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
