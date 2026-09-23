#pragma once
#include <zima/drawing/drawing_document.hpp>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QOpenGLFramebufferObject>
#include <QOpenGLShaderProgram>
#include <QOpenGLBuffer>
#include <QImage>
#include <QRectF>
#include <QColor>
#include <QMatrix4x4>
#include <QElapsedTimer>
#include <cstdio>
#include <algorithm>
#include <cmath>
#include <memory>

namespace zima::app {
// The same depth-buffer principle as MeshView: no vector visibility splitting
// during interaction. Output strokes are prepared separately by SheetRenderer.
class DrawingDepthView {
    struct RestoreContext {
        QOpenGLContext* previous=QOpenGLContext::currentContext();
        QSurface* surface=previous?previous->surface():nullptr;
        ~RestoreContext(){if(previous&&surface)previous->makeCurrent(surface);else if(auto* current=QOpenGLContext::currentContext())current->doneCurrent();}
    };
    QOffscreenSurface surface_;
    QOpenGLContext context_;
    std::unique_ptr<QOpenGLShaderProgram> shader_;
    bool available_{};
    struct Packet {
        std::vector<drawing::ProjectedEdge> edges;
        std::vector<drawing::ProjectedTriangle> triangles;
        QOpenGLBuffer buffer;
        std::unique_ptr<QOpenGLFramebufferObject> target;
        struct Range {int first{},count{};drawing::ProjectedEdge style;};
        std::vector<Range> ranges;
        int triangle_vertices{};
        double zmin{},zmax{};
        drawing::Point2 origin;
        QRectF bounds;
        std::array<double,7> style{};
        QColor visible,hidden;
        QImage image;
    };
    std::map<std::string,std::unique_ptr<Packet>> packets_;
public:
    DrawingDepthView() {
        RestoreContext restore;
        QSurfaceFormat format;format.setDepthBufferSize(24);format.setVersion(2,0);
        surface_.setFormat(format);surface_.create();context_.setFormat(surface_.format());
        if(!context_.create()||!context_.makeCurrent(&surface_))return;
        shader_=std::make_unique<QOpenGLShaderProgram>();
        available_=shader_->addShaderFromSourceCode(QOpenGLShader::Vertex,
            "attribute vec3 position; attribute float lighting; uniform mat4 transform; varying float light; void main(){gl_Position=transform*vec4(position,1.0);light=lighting;}")&&
            shader_->addShaderFromSourceCode(QOpenGLShader::Fragment,
            "uniform vec4 ink; varying float light; void main(){gl_FragColor=vec4(ink.rgb*light,ink.a);}")&&shader_->link();
        context_.doneCurrent();
    }
    QImage render(const drawing::DrawingView& view,const QRectF& bounds,double pixels,double paper_pixels,
                  QColor visible,QColor hidden,double thick,double thin,bool fill_only=false) {
        const bool profile=qEnvironmentVariableIsSet("ZIMA_DRAWING_PROFILE_DEPTH");
        QElapsedTimer timer;if(profile)timer.start();
        const auto stamp=[&](const char* stage){if(profile)std::fprintf(stderr,"drawing depth %s %.3f ms\n",stage,timer.nsecsElapsed()/1e6);};
        if(!available_||bounds.width()<=0||bounds.height()<=0)return {};
        const double reduction=std::min({1.0,8192/std::max(bounds.width()*pixels,bounds.height()*pixels),
            std::sqrt(16'000'000.0/std::max(1.0,bounds.width()*bounds.height()*pixels*pixels))});
        const int w=std::max(1,int(std::ceil(bounds.width()*pixels*reduction)));
        const int h=std::max(1,int(std::ceil(bounds.height()*pixels*reduction)));
        const auto found=packets_.find(view.id);
        const auto* previous=found==packets_.end()?nullptr:found->second.get();
        const bool same_geometry=previous&&std::ranges::equal(previous->edges,view.projected_edges,[](const auto& a,const auto& b){
            return a.points==b.points&&a.vertex_depths==b.vertex_depths&&a.tangent==b.tangent&&a.thread==b.thread&&a.thread_leadin==b.thread_leadin&&a.hatch==b.hatch;
        })&&std::ranges::equal(previous->triangles,view.projected_triangles,[](const auto& a,const auto& b){
            return a.points==b.points&&a.vertex_depths==b.vertex_depths&&a.light==b.light&&a.source.semantic_key.starts_with("thread:surface:")==b.source.semantic_key.starts_with("thread:surface:");
        });
        const std::array style{thick*reduction,thin*reduction,double(view.display_style),double(view.tangent_edge_style),double(view.show_thread_leadins),double(fill_only),double(view.hidden_edge_style)};
        // A CPU image cache hit needs no context switch or GPU synchronization.
        if(same_geometry&&!previous->image.isNull()&&previous->image.size()==QSize(w,h)&&previous->bounds==bounds&&previous->style==style&&previous->visible==visible&&previous->hidden==hidden){stamp("cached");return previous->image;}
        RestoreContext restore;
        if(!context_.makeCurrent(&surface_))return {};
        stamp("context");auto* gl=context_.functions();
        if(!previous) {
            if(packets_.size()>=8)packets_.erase(packets_.begin());
            packets_[view.id]=std::make_unique<Packet>();
        }
        auto& packet=*packets_.at(view.id);
        struct Vertex{float x,y,z,light;};
        if(!packet.buffer.isCreated()||!same_geometry) {
            packet.edges=view.projected_edges;packet.triangles=view.projected_triangles;packet.image={};
            packet.zmin=packet.zmax=0;
            for(const auto& t:packet.triangles)for(double z:t.vertex_depths){packet.zmin=std::min(packet.zmin,z);packet.zmax=std::max(packet.zmax,z);}
            for(const auto& e:packet.edges)for(double z:e.vertex_depths){packet.zmin=std::min(packet.zmin,z);packet.zmax=std::max(packet.zmax,z);}
            std::vector<Vertex> vertices;
            packet.origin={};if(!packet.triangles.empty())packet.origin=packet.triangles.front().points.front();else if(!packet.edges.empty()&&!packet.edges.front().points.empty())packet.origin=packet.edges.front().points.front();
            const auto vertex=[&](drawing::Point2 p,double z,float light=1){return Vertex{float(p.x-packet.origin.x),float(p.y-packet.origin.y),float(z-packet.zmin),light};};
            for(const auto& t:packet.triangles)if(!t.source.semantic_key.starts_with("thread:surface:"))for(int k=0;k<3;++k)vertices.push_back(vertex(t.points[k],t.vertex_depths[k],float(std::clamp(t.light,0.,1.))));
            packet.triangle_vertices=int(vertices.size());packet.ranges.clear();
            std::map<unsigned,std::vector<std::size_t>> groups;
            for(std::size_t i=0;i<packet.edges.size();++i){const auto& e=packet.edges[i];groups[unsigned(e.tangent)|(unsigned(e.thread)<<1)|(unsigned(e.thread_leadin)<<2)|(unsigned(e.hatch)<<3)].push_back(i);}
            for(const auto& [style,indices]:groups){Packet::Range range;range.first=int(vertices.size());
                range.style.tangent=style&1;range.style.thread=style&2;range.style.thread_leadin=style&4;range.style.hatch=style&8;
                for(auto index:indices){const auto& e=packet.edges[index];
                    for(std::size_t i=1;i<e.points.size();++i)for(auto k:{i-1,i})vertices.push_back(vertex(e.points[k],e.vertex_depths.size()==e.points.size()?e.vertex_depths[k]:0));
                }
                range.count=int(vertices.size())-range.first;packet.ranges.push_back(std::move(range));
            }
            if(!packet.buffer.isCreated())packet.buffer.create();packet.buffer.bind();packet.buffer.allocate(vertices.data(),int(vertices.size()*sizeof(Vertex)));packet.buffer.release();
        }
        stamp("geometry");
        if(!packet.target||packet.target->size()!=QSize(w,h)) {
            QOpenGLFramebufferObjectFormat format;format.setAttachment(QOpenGLFramebufferObject::CombinedDepthStencil);
            packet.target=std::make_unique<QOpenGLFramebufferObject>(w,h,format);
        }
        auto& target=*packet.target;if(!target.isValid())return {};
        target.bind();gl->glViewport(0,0,w,h);gl->glClearColor(0,0,0,0);gl->glClearDepthf(1);
        gl->glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);gl->glEnable(GL_DEPTH_TEST);gl->glDepthFunc(GL_LEQUAL);
        const double zspan=std::max(1.,packet.zmax-packet.zmin);
        const auto shaded=fill_only||view.display_style==drawing::DisplayStyle::Shaded||view.display_style==drawing::DisplayStyle::ShadedWithEdges;
        packet.buffer.bind();shader_->bind();shader_->enableAttributeArray("position");
        shader_->setAttributeBuffer("position",GL_FLOAT,0,3,sizeof(Vertex));
        shader_->enableAttributeArray("lighting");shader_->setAttributeBuffer("lighting",GL_FLOAT,3*sizeof(float),1,sizeof(Vertex));
        QMatrix4x4 transform;transform(0,0)=float(2/bounds.width());transform(0,3)=float(-1-2*(bounds.left()-packet.origin.x)/bounds.width());
        transform(1,1)=float(2/bounds.height());transform(1,3)=float(-1-2*(bounds.top()-packet.origin.y)/bounds.height());
        transform(2,2)=float(-1.96/zspan);transform(2,3)=.98f;shader_->setUniformValue("transform",transform);
        gl->glColorMask(shaded,shaded,shaded,shaded);gl->glEnable(GL_POLYGON_OFFSET_FILL);gl->glPolygonOffset(1,1);
        shader_->setUniformValue("ink",QColor(185,194,204));gl->glDrawArrays(GL_TRIANGLES,0,packet.triangle_vertices);
        gl->glDisable(GL_POLYGON_OFFSET_FILL);gl->glColorMask(true,true,true,true);gl->glDepthMask(false);
        if(!fill_only&&view.display_style!=drawing::DisplayStyle::Shaded)for(bool occluded:{true,false}) {
            if(occluded&&view.display_style!=drawing::DisplayStyle::HiddenEdges)continue;
            gl->glDepthFunc(occluded?GL_GREATER:GL_LEQUAL);
            for(const auto& range:packet.ranges){auto edge=range.style;edge.hidden=occluded;
                if(!drawing::drawing_edge_visible(view,edge))continue;
                // Interactive hidden edges match the continuous gray 3D display.
                // The saved dash style is applied only by vector output.
                shader_->setUniformValue("ink",occluded||edge.tangent||edge.thread?hidden:visible);
                gl->glLineWidth(float((occluded||edge.thread||(edge.tangent&&view.tangent_edge_style==drawing::TangentEdgeStyle::Thin)?thin:thick)*reduction));
                gl->glDrawArrays(GL_LINES,range.first,range.count);
            }
        }
        gl->glDepthMask(true);shader_->disableAttributeArray("position");shader_->disableAttributeArray("lighting");shader_->release();packet.buffer.release();
        stamp("draw submitted");
        packet.image=target.toImage();stamp("readback");target.release();packet.bounds=bounds;packet.style=style;packet.visible=visible;packet.hidden=hidden;
        qsizetype memory=0;for(const auto& [id,p]:packets_)memory+=p->image.sizeInBytes();
        if(memory>64*1024*1024)for(auto& [id,p]:packets_)if(p.get()!=&packet){p->image={};p->target.reset();}
        return packet.image;
    }
};
inline DrawingDepthView& drawing_depth_view(){static thread_local DrawingDepthView view;return view;}
}
