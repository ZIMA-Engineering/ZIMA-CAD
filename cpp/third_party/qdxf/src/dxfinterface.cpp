/*
  ZIMA-libqdxf
  http://www.zima-engineering.cz/projekty/ZIMA-libqdxf

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "dxfinterface.h"
#include "mtexttohtml.h"
#include <zima/sketcher/curve_geometry.hpp>
#include <cmath>
#include <iostream>
#include <QFileInfo>
#include <QPen>
#include <QDebug>
#include "scene_items.h"
#include <QPicture>
#include <QStyleOptionGraphicsItem>
#include <QGraphicsLineItem>
#include <QGraphicsPathItem>
#include <set>
#include <functional>

namespace {

bool isUsableMeasurement(double value)
{
    return std::isfinite(value) && value > 0.0 && value < 1000000000.0;
}

QString dxfStringToQString(const std::string &text)
{
    return QString::fromUtf8(text.c_str());
}

QPointF coordToPoint(const DRW_Coord &point)
{
    return QPointF(point.x, point.y);
}

QLineF textAlignmentLine(const DRW_Text &data)
{
    return QLineF(coordToPoint(data.basePoint), coordToPoint(data.secPoint));
}

bool textUsesFitLine(const DRW_Text &data)
{
    return data.alignH == DRW_Text::HAligned || data.alignH == DRW_Text::HFit;
}

double angleForLine(const QLineF &line)
{
    return std::atan2(line.dy(), line.dx()) * 180.0 / M_PI;
}

int attachmentPointForMText(const DRW_MText &data)
{
    if (data.textgen >= DRW_MText::TopLeft && data.textgen <= DRW_MText::BottomRight)
        return data.textgen;

    const int alignV = static_cast<int>(data.alignV);
    if (alignV >= DRW_MText::TopLeft && alignV <= DRW_MText::BottomRight)
        return alignV;

    return DRW_MText::TopLeft;
}

} // namespace

DXFInterface::DXFInterface(QString filename)
{

    const auto encoded=filename.toUtf8();
    dxfRW rw(encoded.constData());
    loaded_=rw.read(this, false);
    expandInserts();

    mScene.setBackgroundBrush(QBrush(Qt::black));
}

DXFInterface::~DXFInterface()
{

}

void DXFInterface::add3dFace(const DRW_3Dface & /*data*/)
{
    unsupported_.insert("3DFACE");
}

void DXFInterface::addArc(const DRW_Arc &data)
{
    auto* arc=new SceneArc(data);arc->setPen(attributesToPen(&data));addSceneItem(arc,QString::fromStdString(data.layer));
}

void DXFInterface::addBlock(const DRW_Block &data)
{
    current_block_=QString::fromStdString(data.name);block_bases_[current_block_]=QPointF(data.basePoint.x,data.basePoint.y);
}

void DXFInterface::setBlock(const int /*handle*/)
{

}

void DXFInterface::addCircle(const DRW_Circle &data)
{
    auto* item=new QGraphicsEllipseItem(data.basePoint.x-data.radious,data.basePoint.y-data.radious,2*data.radious,2*data.radious);item->setPen(attributesToPen(&data));addSceneItem(item,QString::fromStdString(data.layer));
}

void DXFInterface::addComment(const char * /*comment*/)
{

}

void DXFInterface::addDimAlign(const DRW_DimAligned * /*data*/)
{
    unsupported_.insert("DIMENSION");
}

void DXFInterface::addDimAngular(const DRW_DimAngular * /*data*/)
{
    unsupported_.insert("DIMENSION");
}

void DXFInterface::addDimAngular3P(const DRW_DimAngular3p * /*data*/)
{
    unsupported_.insert("DIMENSION");
}

void DXFInterface::addDimDiametric(const DRW_DimDiametric * /*data*/)
{
    unsupported_.insert("DIMENSION");
}

void DXFInterface::addDimLinear(const DRW_DimLinear * /*data*/)
{
    unsupported_.insert("DIMENSION");
}

void DXFInterface::addDimOrdinate(const DRW_DimOrdinate * /*data*/)
{
    unsupported_.insert("DIMENSION");
}

void DXFInterface::addDimRadial(const DRW_DimRadial * /*data*/)
{
    unsupported_.insert("DIMENSION");
}

void DXFInterface::addDimStyle(const DRW_Dimstyle & /*data*/)
{

}

void DXFInterface::addEllipse(const DRW_Ellipse &data)
{
    DRW_Ellipse ellipse=data;DRW_Polyline polyline;ellipse.toPolyline(&polyline);
    std::vector<PolylinePoint> points;for(const auto* point:polyline.vertlist)points.push_back({QPointF(point->basePoint.x,point->basePoint.y),point->bulge});
    addPolylinePath(points,(polyline.flags&1)!=0,attributesToPen(&data),QString::fromStdString(data.layer));
}

void DXFInterface::addHatch(const DRW_Hatch * /*data*/)
{
    unsupported_.insert("HATCH");
}

void DXFInterface::addHeader(const DRW_Header *data)
{
    const auto found=data->vars.find("$INSUNITS");if(found!=data->vars.end()&&found->second&&found->second->type==DRW_Variant::INTEGER)units_=found->second->content.i;
}

void DXFInterface::addImage(const DRW_Image * /*data*/)
{
    unsupported_.insert("IMAGE");
}

void DXFInterface::addInsert(const DRW_Insert &data)
{
    inserts_.push_back({current_block_,data});
}

void DXFInterface::addKnot(const DRW_Entity & /*data*/)
{

}

void DXFInterface::addLayer(const DRW_Layer &data)
{
    layers.append(data);
}

void DXFInterface::addLeader(const DRW_Leader * /*data*/)
{
    unsupported_.insert("LEADER");
}

void DXFInterface::addLine(const DRW_Line &data)
{
    addLineItem(QLineF(data.basePoint.x,data.basePoint.y,data.secPoint.x,data.secPoint.y),attributesToPen(&data),QString::fromStdString(data.layer));
}

void DXFInterface::addLType(const DRW_LType & /*data*/)
{

}

void DXFInterface::addLWPolyline(const DRW_LWPolyline &data)
{
    std::vector<PolylinePoint> points;for(const auto* point:data.vertlist)points.push_back({QPointF(point->x,point->y),point->bulge});
    addPolylinePath(points,(data.flags&1)!=0,attributesToPen(&data),QString::fromStdString(data.layer));
}

void DXFInterface::addMText(const DRW_MText &data)
{
    QString html = MTextToHTML::convert(dxfStringToQString(data.text));
    if (html.isEmpty())
        return;

    SceneText *item = new SceneText(html, fontForText(data),
                                    attributesToPen(&data).color(),
                                    heightForText(data),
                                    1.0,
                                    data.angle,
                                    QPointF(data.basePoint.x, data.basePoint.y),
                                    horizontalAlignmentForMText(data),
                                    verticalAlignmentForMText(data),
                                    true,
                                    widthForMText(data));
    addSceneItem(item,QString::fromStdString(data.layer));
}

void DXFInterface::addPoint(const DRW_Point &data)
{
    addLineItem(QLineF(data.basePoint.x,data.basePoint.y,data.basePoint.x,data.basePoint.y),attributesToPen(&data),QString::fromStdString(data.layer));
}

void DXFInterface::addPolyline(const DRW_Polyline &data)
{
    std::vector<PolylinePoint> points;for(const auto* point:data.vertlist)points.push_back({QPointF(point->basePoint.x,point->basePoint.y),point->bulge});
    addPolylinePath(points,(data.flags&1)!=0,attributesToPen(&data),QString::fromStdString(data.layer));
}

void DXFInterface::addRay(const DRW_Ray & /*data*/)
{
    unsupported_.insert("RAY");
}

void DXFInterface::addSolid(const DRW_Solid & /*data*/)
{
    unsupported_.insert("SOLID");
}

void DXFInterface::addSpline(const DRW_Spline* data){
    try{
        zima::kernel::BSplineGeometry curve;curve.degree=data->degree;curve.knots=data->knotslist;curve.weights=data->weightslist;
        for(const auto* pole:data->controllist)curve.poles.push_back({pole->x,pole->y,pole->z});
        if(curve.weights.empty())curve.weights.assign(curve.poles.size(),1);
        curve=zima::sketcher::clamp_curve_geometry(std::move(curve));
        QPainterPath path;for(int sample=0;sample<=256;++sample){const auto point=zima::kernel::bspline_value(curve,sample/256.);if(sample==0)path.moveTo(point.x,point.y);else path.lineTo(point.x,point.y);}
        auto* item=new QGraphicsPathItem(path);item->setPen(attributesToPen(data));addSceneItem(item,QString::fromStdString(data->layer));
    }catch(const std::exception&){unsupported_.insert("SPLINE invalid data");}
}

void DXFInterface::addText(const DRW_Text &data)
{
    QString text = dxfStringToQString(data.text);
    if (text.isEmpty())
        return;

    const QLineF alignmentLine = textAlignmentLine(data);
    QPointF anchorPoint = anchorPointForText(data);
    double textAngle = data.angle;
    double widthScale = widthScaleForText(data);
    double targetWidth = 0.0;
    SceneText::ScaleMode scaleMode = SceneText::ScaleFromTextHeight;

    if (textUsesFitLine(data) && alignmentLine.length() > 0.0)
    {
        anchorPoint = alignmentLine.p1();
        textAngle = angleForLine(alignmentLine);
        widthScale = 1.0;
        targetWidth = alignmentLine.length();
        scaleMode = data.alignH == DRW_Text::HAligned
                ? SceneText::ScaleUniformToTarget
                : SceneText::ScaleWidthToTarget;
    }

    ScenePlainText *item = new ScenePlainText(text, fontForText(data),
                                              attributesToPen(&data).color(),
                                              heightForText(data),
                                              widthScale,
                                              textAngle,
                                              anchorPoint,
                                              horizontalAlignmentForText(data),
                                              verticalAlignmentForText(data),
                                              scaleMode,
                                              targetWidth);
    addSceneItem(item,QString::fromStdString(data.layer));
}

void DXFInterface::addTextStyle(const DRW_Textstyle &data)
{
    textStyles.append(data);
}

void DXFInterface::addTrace(const DRW_Trace & /*data*/)
{
    unsupported_.insert("TRACE");
}

void DXFInterface::addViewport(const DRW_Viewport & /*data*/)
{

}

void DXFInterface::addVport(const DRW_Vport & /*data*/)
{

}

void DXFInterface::addXline(const DRW_Xline & /*data*/)
{
    unsupported_.insert("XLINE");
}

void DXFInterface::endBlock()
{
    current_block_.clear();
}

void DXFInterface::linkImage(const DRW_ImageDef * /*data*/)
{

}

void DXFInterface::writeBlockRecords()
{

}

void DXFInterface::writeBlocks()
{

}

void DXFInterface::writeDimstyles()
{

}

void DXFInterface::writeEntities()
{

}

void DXFInterface::writeHeader(DRW_Header & /*data*/)
{

}

void DXFInterface::writeLayers()
{

}

void DXFInterface::writeLTypes()
{

}

void DXFInterface::writeTextstyles()
{

}

void DXFInterface::writeVports()
{

}


QPen DXFInterface::attributesToPen(const DRW_Entity *e)
{
    QPen p(Qt::yellow, 0, Qt::SolidLine);

    if(e->visible == false)
        qDebug() << "nevid";

    if (e->color == 256)
    {   // BYLAYER
        DRW_Layer l = getLayer(e->layer);
        p.setColor( numberToColor( l.color));
    }

    else if (e->color24 >= 0)
        p.setColor(QColor(e->color24 >> 16,
                          e->color24 >> 8 & 0xFF,
                          e->color24 & 0xFF));

    else
        p.setColor(numberToColor(e->color));

    if(e->lineType == "BYLAYER")
    {
        DRW_Layer l = getLayer(e->layer);
        setQPenLinetype(p, l.lineType);
    }
    else
        setQPenLinetype(p, e->lineType);


    return p;
}

QColor DXFInterface::numberToColor(int col)
{
    // 7 je black or white - potrebuju white (255)
    if (col == 7)
        col = 255;

    if (col==0)
    {
        //return RS_Color(RS2::FlagByBlock);
    }
    else if (col==256)
    {
        //return RS_Color(RS2::FlagByLayer);
    }
    else if (col<=255 && col>=0)
    {

        return QColor::fromRgb(DRW::dxfColors[col][0],
                               DRW::dxfColors[col][1],
                               DRW::dxfColors[col][2]);
    }

    return QColor(Qt::red);
    // else: neznama barva
}

DRW_Layer DXFInterface::getLayer(std::string name)
{
    foreach(DRW_Layer l, layers)
    {
        if (l.name == name)
            return l;
    }

    qDebug() << "layer not found :" << name.c_str();
    return DRW_Layer();
}

DRW_Textstyle DXFInterface::getTextStyle(std::string name)
{
    foreach(DRW_Textstyle style, textStyles)
    {
        if (style.name == name)
            return style;
    }

    const QString requestedName = dxfStringToQString(name);
    foreach(DRW_Textstyle style, textStyles)
    {
        if (dxfStringToQString(style.name).compare(requestedName, Qt::CaseInsensitive) == 0)
            return style;
    }

    return DRW_Textstyle();
}

QFont DXFInterface::fontForText(const DRW_Text &data)
{
    DRW_Textstyle style = getTextStyle(data.style);
    QString family = dxfStringToQString(style.font).trimmed();
    family.replace(QLatin1Char('\\'), QLatin1Char('/'));
    family = QFileInfo(family).completeBaseName();

    if (family.isEmpty()
            || family.compare(QStringLiteral("txt"), Qt::CaseInsensitive) == 0
            || family.compare(QStringLiteral("standard"), Qt::CaseInsensitive) == 0)
    {
        family = QStringLiteral("Arial");
    }

    QFont font(family);
    font.setStyleStrategy(QFont::PreferAntialias);
    return font;
}

double DXFInterface::heightForText(const DRW_Text &data)
{
    if (isUsableMeasurement(data.height))
        return data.height;

    DRW_Textstyle style = getTextStyle(data.style);
    if (isUsableMeasurement(style.height))
        return style.height;
    if (isUsableMeasurement(style.lastHeight))
        return style.lastHeight;

    return 1.0;
}

double DXFInterface::widthScaleForText(const DRW_Text &data)
{
    if (isUsableMeasurement(data.widthscale))
        return data.widthscale;

    DRW_Textstyle style = getTextStyle(data.style);
    if (isUsableMeasurement(style.width))
        return style.width;

    return 1.0;
}

QPointF DXFInterface::anchorPointForText(const DRW_Text &data)
{
    const bool usesAlignmentPoint = data.alignH != DRW_Text::HLeft
            || data.alignV != DRW_Text::VBaseLine;
    const DRW_Coord &point = usesAlignmentPoint
            ? data.secPoint
            : data.basePoint;

    return coordToPoint(point);
}

SceneText::HorizontalAlignment DXFInterface::horizontalAlignmentForText(const DRW_Text &data)
{
    switch (data.alignH)
    {
    case DRW_Text::HCenter:
    case DRW_Text::HMiddle:
        return SceneText::AlignCenter;
    case DRW_Text::HRight:
        return SceneText::AlignRight;
    default:
        return SceneText::AlignLeft;
    }
}

SceneText::VerticalAlignment DXFInterface::verticalAlignmentForText(const DRW_Text &data)
{
    if (data.alignH == DRW_Text::HMiddle)
        return SceneText::AlignMiddle;

    switch (data.alignV)
    {
    case DRW_Text::VTop:
        return SceneText::AlignTop;
    case DRW_Text::VMiddle:
        return SceneText::AlignMiddle;
    case DRW_Text::VBottom:
        return SceneText::AlignBottom;
    default:
        return SceneText::AlignBaseline;
    }
}

double DXFInterface::widthForMText(const DRW_MText &data)
{
    if (data.hasReferenceWidth && isUsableMeasurement(data.widthscale))
        return data.widthscale;

    return 0.0;
}

SceneText::HorizontalAlignment DXFInterface::horizontalAlignmentForMText(const DRW_MText &data)
{
    switch ((attachmentPointForMText(data) - 1) % 3)
    {
    case 1:
        return SceneText::AlignCenter;
    case 2:
        return SceneText::AlignRight;
    default:
        return SceneText::AlignLeft;
    }
}

SceneText::VerticalAlignment DXFInterface::verticalAlignmentForMText(const DRW_MText &data)
{
    const int attachmentPoint = attachmentPointForMText(data);
    if (attachmentPoint >= DRW_MText::BottomLeft)
        return SceneText::AlignBottom;
    if (attachmentPoint >= DRW_MText::MiddleLeft)
        return SceneText::AlignMiddle;

    return SceneText::AlignTop;
}

void DXFInterface::setQPenLinetype(QPen & p, std::string linetype)
{
    if (linetype == "CONTINUOUS" || linetype == "Continuous")
        p.setStyle(Qt::SolidLine);

    else if(linetype == "HIDDEN")
        p.setStyle(Qt::DashLine);

    else if(linetype == "CENTER")
    {
        QVector<qreal> dashes;
        dashes << 6 << 2 << 2 << 2;
        p.setDashPattern(dashes);
    }
    else
    {
        qDebug() << "unknown linetype" << linetype.c_str();
    }
    return;
}

void DXFInterface::drawPolyline(std::vector<DRW_Vertex*> vertlist, QPen pen)
{
    for(unsigned int i = 1; i < vertlist.size(); i++)
    {
        DRW_Vertex *verta = vertlist[i-1];
        DRW_Vertex *vertb = vertlist[i];

        addLineItem(QLineF(verta->basePoint.x,verta->basePoint.y,vertb->basePoint.x,vertb->basePoint.y),pen,"0");
    }
}

QGraphicsScene * DXFInterface::scene()
{
    return &mScene;
}

namespace {
class RecordedDxfItem final : public QGraphicsItem {
public:
    RecordedDxfItem(QPicture picture,QRectF bounds):picture_(std::move(picture)),bounds_(bounds){}
    QRectF boundingRect()const override{return bounds_;}
    void paint(QPainter* painter,const QStyleOptionGraphicsItem*,QWidget*)override{painter->drawPicture(0,0,picture_);}
private:
    QPicture picture_;QRectF bounds_;
};
}
void DXFInterface::addSceneItem(QGraphicsItem* item,const QString& layer){
    item->setData(0,layer);
    if(current_block_.isEmpty()){mScene.addItem(item);return;}
    QPicture picture;QPainter painter(&picture);painter.setTransform(item->sceneTransform());QStyleOptionGraphicsItem option;item->paint(&painter,&option,nullptr);painter.end();
    blocks_[current_block_].push_back({picture,item->sceneBoundingRect(),layer});delete item;
}
void DXFInterface::addLineItem(QLineF line,QPen pen,const QString& layer){
    auto* item=new QGraphicsLineItem(line);item->setPen(pen);addSceneItem(item,layer);
}
void DXFInterface::addPolylinePath(const std::vector<PolylinePoint>& points,bool closed,QPen pen,const QString& layer){
    if(points.empty())return;QPainterPath path;path.moveTo(points.front().point);
    const auto count=closed?points.size():points.size()-1;
    for(std::size_t index=0;index<count;++index){const auto& a=points[index];const auto b=points[(index+1)%points.size()].point;
        const double dx=b.x()-a.point.x(),dy=b.y()-a.point.y(),chord=std::hypot(dx,dy);
        if(std::abs(a.bulge)<1e-12||chord<1e-12){path.lineTo(b);continue;}
        const double factor=(1-a.bulge*a.bulge)/(4*a.bulge);
        const QPointF center=(a.point+b)*.5+QPointF(-dy,dx)*factor;
        const double radius=chord*(1+a.bulge*a.bulge)/(4*std::abs(a.bulge));
        const double start=-std::atan2(a.point.y()-center.y(),a.point.x()-center.x())*180/M_PI;
        path.arcTo(QRectF(center.x()-radius,center.y()-radius,2*radius,2*radius),start,-4*std::atan(a.bulge)*180/M_PI);
    }
    if(closed)path.closeSubpath();auto* item=new QGraphicsPathItem(path);item->setPen(pen);addSceneItem(item,layer);
}
void DXFInterface::expandInserts(){
    std::set<QString> active;std::size_t count=0;
    std::function<void(const DRW_Insert&,const QTransform&,const QString&)> expand;
    expand=[&](const DRW_Insert& insert,const QTransform& parent,const QString& parent_layer){
        const QString name=QString::fromStdString(insert.name);if(active.contains(name)||active.size()>32||count>100000){unsupported_.insert("INSERT recursion/limit");return;}
        if(!block_bases_.contains(name)){unsupported_.insert("INSERT missing block");return;}active.insert(name);
        const QString own_layer=QString::fromStdString(insert.layer),layer=own_layer=="0"?parent_layer:own_layer;
        for(int row=0;row<std::max(1,insert.rowcount);++row)for(int column=0;column<std::max(1,insert.colcount);++column){
            if(++count>100000){unsupported_.insert("INSERT limit");active.erase(name);return;}QTransform transform;transform.translate(insert.basePoint.x,insert.basePoint.y);transform.rotate(insert.angle*180/M_PI);transform.translate(column*insert.colspace,row*insert.rowspace);transform.scale(insert.xscale,insert.yscale);transform.translate(-block_bases_[name].x(),-block_bases_[name].y());transform=transform*parent;
            for(const auto& record:blocks_[name]){auto* item=new RecordedDxfItem(record.picture,record.bounds);item->setTransform(transform);item->setData(0,record.layer=="0"?layer:record.layer);mScene.addItem(item);}
            for(const auto& child:inserts_)if(child.block==name)expand(child.insert,transform,layer);
        }active.erase(name);
    };
    for(const auto& entry:inserts_)if(entry.block.isEmpty())expand(entry.insert,QTransform(),QString::fromStdString(entry.insert.layer));
}
