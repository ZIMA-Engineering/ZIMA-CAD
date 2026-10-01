#pragma once
#include <zima/viewer/mesh_view.hpp>
#include <QApplication>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <iostream>

namespace zima::test {
template<class Refresh> void probe_preview_refresh(viewer::MeshView& view,const char* name,Refresh refresh) {
    if(!qEnvironmentVariableIsSet("ZIMA_VERIFY_PREVIEW_REFRESH"))return;
    for(int trial=0;trial<4;++trial) {
        const auto revision=view.base_mesh_revision();
        QElapsedTimer timer;timer.start();refresh();QApplication::processEvents();
        const auto elapsed=timer.nsecsElapsed()/1e6;
        const auto pixels=view.grabFramebuffer().convertToFormat(QImage::Format_RGBA8888);
        const auto hash=QCryptographicHash::hash(QByteArrayView(reinterpret_cast<const char*>(pixels.constBits()),pixels.sizeInBytes()),QCryptographicHash::Sha256).toHex();
        std::cout<<"Preview refresh "<<name<<" trial="<<trial<<" ms="<<elapsed
            <<" publications="<<view.base_mesh_revision()-revision<<" sha256="<<hash.constData()<<std::endl;
    }
}
}
