#pragma once
#include <QPaintDevice>
#include <QPaintEngine>
#include <QPainterPath>
#include <QFontMetricsF>
#include <QImage>
#include <QPixmap>
#include <QTransform>
#include <QTextStream>
#include <cmath>
#include <algorithm>

namespace zima::app {
// Millimetre drawing output. Qt supplies the same vector primitives as PDF;
// no model calculation or screen-resolution projection is performed here.
class DrawingDxfDevice final : public QPaintDevice {
    class Engine final : public QPaintEngine {
    public:
        explicit Engine(double height):QPaintEngine(AllFeatures),height_(height){}
        bool begin(QPaintDevice*) override { setActive(true);return true; }
        bool end() override {setActive(false);return true;}
        Type type()const override{return User;}
        void updateState(const QPaintEngineState& s) override {
            if(s.state()&DirtyTransform) transform_=s.transform();
            if(s.state()&DirtyPen) pen_=s.pen();
            if(s.state()&DirtyBrush) brush_=s.brush();
        }
        void code(int n,const QString& value){entities_+=QString::number(n)+'\n'+value+'\n';}
        void code(int n,double value){code(n,QString::number(value,'g',15));}
        QPointF point(QPointF p)const {p=transform_.map(p);return {p.x(),height_-p.y()};}
        void vertex(int n,QPointF p){code(n,p.x());code(n+10,p.y());code(n+20,0);}
        void entity(const char* name,QColor color) {
            code(0,QLatin1String(name));code(5,QString::number(next_handle_++,16));code(100,QStringLiteral("AcDbEntity"));code(8,QStringLiteral("0"));
            code(420,static_cast<double>((color.red()<<16)|(color.green()<<8)|color.blue()));
            const auto type=QLatin1String(name);
            code(100,type=="LINE"?QStringLiteral("AcDbLine"):type=="TEXT"?QStringLiteral("AcDbText"):type=="HATCH"?QStringLiteral("AcDbHatch"):QStringLiteral("AcDbTrace"));
        }
        void line(QPointF a,QPointF b) {
            if(pen_.style()==Qt::NoPen)return;
            entity("LINE",pen_.color());
            const bool center=pen_.style()==Qt::DashDotLine||pen_.style()==Qt::DashDotDotLine;
            code(6,center?QStringLiteral("CENTER"):pen_.style()==Qt::SolidLine?QStringLiteral("CONTINUOUS"):QStringLiteral("DASHED"));
            code(370,std::clamp(static_cast<int>(std::round(pen_.widthF()*100)),0,211));
            vertex(10,point(a));vertex(11,point(b));
        }
        void solid(QPointF a,QPointF b,QPointF c,QColor color) {
            if(color.alpha()==0)return;
            entity("SOLID",color);vertex(10,point(a));vertex(11,point(b));vertex(12,point(c));vertex(13,point(c));
        }
        void drawLines(const QLineF* lines,int count)override{for(int i=0;i<count;++i)line(lines[i].p1(),lines[i].p2());}
        void drawLines(const QLine* lines,int count)override{for(int i=0;i<count;++i)line(lines[i].p1(),lines[i].p2());}
        void drawPolygon(const QPointF* points,int count,PolygonDrawMode mode)override {
            if(count<2)return;
            if(mode!=PolylineMode&&brush_.style()!=Qt::NoBrush) {
                // HATCH retains concave boundaries without triangulation artifacts.
                entity("HATCH",brush_.color());vertex(10,{});code(210,0);code(220,0);code(230,1);
                code(2,QStringLiteral("SOLID"));code(70,1);code(71,0);code(91,1);
                code(92,2);code(72,0);code(73,1);code(93,count);
                for(int i=0;i<count;++i){auto p=point(points[i]);code(10,p.x());code(20,p.y());}
                code(97,0);code(75,0);code(76,1);code(98,0);
            }
            for(int i=1;i<count;++i)line(points[i-1],points[i]);
            if(mode!=PolylineMode)line(points[count-1],points[0]);
        }
        void drawPolygon(const QPoint* points,int count,PolygonDrawMode mode)override {
            QPolygonF polygon;for(int i=0;i<count;++i)polygon<<points[i];drawPolygon(polygon.data(),count,mode);
        }
        void drawPath(const QPainterPath& path)override {
            // Flatten after transformation for a fixed paper-space tolerance.
            const auto old=transform_;const auto mapped=transform_.map(path);
            transform_=QTransform();
            for(auto polygon:mapped.toSubpathPolygons(QTransform::fromScale(32,32))) {
                for(auto& p:polygon)p/=32;
                const bool closed=polygon.size()>2&&polygon.first()==polygon.last();
                drawPolygon(polygon.data(),polygon.size(),closed?WindingMode:PolylineMode);
            }
            transform_=old;
        }
        void drawEllipse(const QRectF& r)override{QPainterPath p;p.addEllipse(r);drawPath(p);}
        void drawEllipse(const QRect& r)override{drawEllipse(QRectF(r));}
        void drawTextItem(const QPointF& position,const QTextItem& item)override {
            entity("TEXT",pen_.color());vertex(10,point(position));
            const auto origin=point({}),x=point({1,0})-origin,y=point({0,1})-origin;
            const auto sx=std::hypot(x.x(),x.y()),sy=std::hypot(y.x(),y.y());
            code(40,std::max(.01,QFontMetricsF(item.font()).capHeight()*sy));
            code(41,sy>1e-12?sx/sy:1);code(50,std::atan2(x.y(),x.x())*180/3.141592653589793);
            if(x.x()*y.y()-x.y()*y.x()>0)code(71,4);
            code(1,item.text().replace('\n',' ').replace('\r',' '));code(7,QStringLiteral("STANDARD"));
        }
        void drawImage(const QRectF& target,const QImage& source,const QRectF& src,Qt::ImageConversionFlags)override {
            // Self-contained raster logos/shading: merge equal horizontal pixels
            // into filled rectangles, retaining their original colour.
            auto image=source.copy(src.toAlignedRect()).convertToFormat(QImage::Format_ARGB32);
            if(image.isNull())return;
            const double dx=target.width()/image.width(),dy=target.height()/image.height();
            for(int y=0;y<image.height();++y)for(int x=0;x<image.width();) {
                auto c=image.pixelColor(x,y);int end=x+1;
                while(end<image.width()&&image.pixelColor(end,y)==c)++end;
                if(c.alpha()) {
                    const double alpha=c.alphaF();c=QColor(qRound(c.red()*alpha+255*(1-alpha)),qRound(c.green()*alpha+255*(1-alpha)),qRound(c.blue()*alpha+255*(1-alpha)));
                    QPointF a=target.topLeft()+QPointF(x*dx,y*dy),b=target.topLeft()+QPointF(end*dx,y*dy),d=target.topLeft()+QPointF(x*dx,(y+1)*dy),e=target.topLeft()+QPointF(end*dx,(y+1)*dy);
                    solid(a,b,e,c);solid(a,e,d,c);
                }
                x=end;
            }
        }
        void drawPixmap(const QRectF& r,const QPixmap& p,const QRectF& s)override{drawImage(r,p.toImage(),s,Qt::AutoColor);}
        QString entities_;
        unsigned next_handle_{0x100};
    private:
        double height_;QTransform transform_;QPen pen_;QBrush brush_;
    };
public:
    DrawingDxfDevice(double width,double height):width_(width),height_(height),engine_(height){}
    QPaintEngine* paintEngine()const override{return const_cast<Engine*>(&engine_);}
    QByteArray data()const {
        QString s="0\nSECTION\n2\nHEADER\n9\n$ACADVER\n1\nAC1027\n9\n$INSUNITS\n70\n4\n9\n$MEASUREMENT\n70\n1\n0\nENDSEC\n0\nSECTION\n2\nTABLES\n0\nTABLE\n2\nLTYPE\n5\n1\n100\nAcDbSymbolTable\n70\n3\n";
        s+="0\nLTYPE\n5\n2\n330\n1\n100\nAcDbSymbolTableRecord\n100\nAcDbLinetypeTableRecord\n2\nCONTINUOUS\n70\n0\n3\nSolid\n72\n65\n73\n0\n40\n0\n";
        s+="0\nLTYPE\n5\n3\n330\n1\n100\nAcDbSymbolTableRecord\n100\nAcDbLinetypeTableRecord\n2\nDASHED\n70\n0\n3\nDashed\n72\n65\n73\n2\n40\n3\n49\n2\n74\n0\n49\n-1\n74\n0\n";
        s+="0\nLTYPE\n5\n4\n330\n1\n100\nAcDbSymbolTableRecord\n100\nAcDbLinetypeTableRecord\n2\nCENTER\n70\n0\n3\nCenter\n72\n65\n73\n4\n40\n8\n49\n5\n74\n0\n49\n-1\n74\n0\n49\n1\n74\n0\n49\n-1\n74\n0\n0\nENDTAB\n";
        s+="0\nTABLE\n2\nSTYLE\n5\n5\n100\nAcDbSymbolTable\n70\n1\n0\nSTYLE\n5\n6\n330\n5\n100\nAcDbSymbolTableRecord\n100\nAcDbTextStyleTableRecord\n2\nSTANDARD\n70\n0\n40\n0\n41\n1\n50\n0\n71\n0\n42\n2.5\n3\nArial.ttf\n4\n\n0\nENDTAB\n0\nENDSEC\n0\nSECTION\n2\nENTITIES\n";
        return (s+engine_.entities_+"0\nENDSEC\n0\nEOF\n").toUtf8();
    }
protected:
    int metric(PaintDeviceMetric m)const override {
        switch(m){case PdmWidth:return qRound(width_);case PdmHeight:return qRound(height_);
        case PdmWidthMM:return qRound(width_);case PdmHeightMM:return qRound(height_);
        case PdmDpiX:case PdmDpiY:case PdmPhysicalDpiX:case PdmPhysicalDpiY:return 96;
        case PdmDepth:return 32;case PdmNumColors:return 0;
        case PdmDevicePixelRatio:return 1;case PdmDevicePixelRatioScaled:return devicePixelRatioFScale();default:return 0;}
    }
private:
    double width_,height_;mutable Engine engine_;
};
}
