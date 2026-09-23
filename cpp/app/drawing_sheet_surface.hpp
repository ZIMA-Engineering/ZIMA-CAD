#pragma once
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QOpenGLPaintDevice>
#include <QPainter>
#include <functional>

namespace zima::app {
// Build a placement background with the GPU painter, not the raster engine.
// The caller reuses the image until the viewport or sheet changes.
class DrawingSheetSurface {
    QOffscreenSurface surface_;
    QOpenGLContext context_;
    bool available_{};
public:
    DrawingSheetSurface() {
        QSurfaceFormat format;format.setVersion(2,0);format.setDepthBufferSize(24);format.setStencilBufferSize(8);
        surface_.setFormat(format);surface_.create();context_.setFormat(surface_.format());available_=context_.create();
    }
    QImage render(QSize size,double ratio,const std::function<void(QPainter&)>& paint) {
        auto* previous=QOpenGLContext::currentContext();auto* previous_surface=previous?previous->surface():nullptr;
        struct Restore {QOpenGLContext* context;QSurface* surface;~Restore(){if(context&&surface)context->makeCurrent(surface);else if(auto* c=QOpenGLContext::currentContext())c->doneCurrent();}} restore{previous,previous_surface};
        if(!available_||!context_.makeCurrent(&surface_))return {};
        QOpenGLFramebufferObjectFormat format;format.setAttachment(QOpenGLFramebufferObject::CombinedDepthStencil);
        QOpenGLFramebufferObject target(size,format);if(!target.isValid())return {};target.bind();
        QOpenGLPaintDevice device(size);device.setDevicePixelRatio(ratio);
        {QPainter painter(&device);painter.fillRect(QRectF(QPointF{},QSizeF(size)/ratio),Qt::black);paint(painter);}
        auto image=target.toImage();image.setDevicePixelRatio(ratio);target.release();return image;
    }
};
}
