#pragma once
#include <zima/document/cam_definition.hpp>
#include <zima/ui/properties_subwindow.hpp>
#include <QCheckBox>
#include <QEvent>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <numbers>
#include <map>

namespace zima::app {
// Presentation-only wire; exact simulation always consumes the native sweeps.
inline std::vector<kernel::ViewerEdge> cam_path_wire(const cam::CompiledProgram& program){
    std::vector<kernel::ViewerEdge> edges;edges.reserve(program.motions.size());
    for(const auto& motion:program.motions){kernel::ViewerEdge edge;edge.overlay=true;edge.color=motion.kind==cam::MotionKind::Rapid?"#FF3030":"#FFD530";
        edge.preview_terminal_dashed=motion.kind==cam::MotionKind::Rapid;edge.reference={motion.operation_id,motion.block_id,{}};
        int segments=1;if(motion.kind==cam::MotionKind::ArcClockwise||motion.kind==cam::MotionKind::ArcCounterclockwise){
            const auto radius=std::hypot(motion.start.x-motion.center.x,motion.start.y-motion.center.y);
            const auto angle=radius>.02?2*std::acos(std::clamp(1-.02/radius,-1.,1.)):std::numbers::pi/16;
            segments=std::max(2,int(std::ceil(std::abs(motion.sweep_radians)/std::max(angle,1e-6))));}
        for(int i=0;i<=segments;++i)edge.points.push_back(cam::point_at(motion,double(i)/segments));edges.push_back(std::move(edge));}
    return edges;
}
inline std::vector<kernel::ViewerEdge> cam_tool_wire(const cam::Tool& tool,cam::Point tip){
    std::vector<kernel::ViewerEdge> edges;const auto origin=cam::tool_origin(tool,tip);
    for(bool cutting:{true,false}){const auto& stations=cutting?tool.cutting:tool.non_cutting;
        const auto color=cutting?"#FFD530":"#FF3030";
        for(const auto& station:stations){kernel::ViewerEdge ring;ring.overlay=true;ring.color=color;
            for(int i=0;i<=64;++i){const auto angle=2*std::numbers::pi*i/64;ring.points.push_back({origin.x+station.radius_mm*std::cos(angle),origin.y+station.radius_mm*std::sin(angle),origin.z+station.z_mm});}edges.push_back(std::move(ring));}
        for(int i=0;i<8;++i){kernel::ViewerEdge side;side.overlay=true;side.color=color;const auto angle=2*std::numbers::pi*i/8;
            for(const auto& s:stations)side.points.push_back({origin.x+s.radius_mm*std::cos(angle),origin.y+s.radius_mm*std::sin(angle),origin.z+s.z_mm});edges.push_back(std::move(side));}}
    return edges;
}
class CamNcDialog final : public ui::PropertiesSubWindow {
public:
    using Present=std::function<void(std::vector<kernel::ViewerEdge>,bool)>;
    std::function<void(const std::string&)> export_program;
    CamNcDialog(std::shared_ptr<const document::CamCalculated> result,Present publish_view,QWidget* parent,
        std::map<std::string,QString> clampings={})
        :PropertiesSubWindow(tr("Generic NC program"),parent),result_(std::move(result)),present_(std::move(publish_view)),
         program_(cam::compile(result_->program,result_->machine,result_->tools,result_->machine_work_origin)),paths_(cam_path_wire(program_)),clampings_(std::move(clampings)) {
        setObjectName("camNcDialog");setAttribute(Qt::WA_DeleteOnClose);setProperty("expandBottomTable",true);
        help_=new QLabel(this);help_->setWordWrap(true);content_layout()->addWidget(help_);
        report_=new QLabel(this);report_->setObjectName("camNcReport");report_->setWordWrap(true);content_layout()->addWidget(report_);
        lines_=new QListWidget(this);lines_->setObjectName("camNcLines");lines_->setMinimumHeight(220);content_layout()->addWidget(lines_,1);
        for(std::size_t i=0;i<program_.lines.size();++i){auto* row=new QListWidgetItem(QString::number(i+1)+"  "+QString::fromStdString(program_.lines[i]),lines_);
            row->setData(Qt::UserRole,QString::fromStdString(program_.line_block_ids[i]));}
        connect(lines_,&QListWidget::currentRowChanged,this,[this](int){fraction_->setValue(0);present();});
        auto* row=new QHBoxLayout;content_layout()->addLayout(row);previous_=new QPushButton(this);next_=new QPushButton(this);previous_->setObjectName("camNcPrevious");next_->setObjectName("camNcNext");row->addWidget(previous_);row->addWidget(next_);
        fraction_=new QSlider(Qt::Horizontal,this);fraction_->setObjectName("camNcFraction");fraction_->setRange(0,1000);row->addWidget(fraction_,1);
        connect(previous_,&QPushButton::clicked,this,[this]{step(-1);});connect(next_,&QPushButton::clicked,this,[this]{step(1);});connect(fraction_,&QSlider::valueChanged,this,[this]{present();});
        remaining_=new QCheckBox(this);remaining_->setObjectName("camNcRemaining");content_layout()->addWidget(remaining_);connect(remaining_,&QCheckBox::toggled,this,[this]{present();});
        export_=new QPushButton(this);export_->setObjectName("camNcExport");content_layout()->addWidget(export_,0,Qt::AlignLeft);
        connect(export_,&QPushButton::clicked,this,[this]{if(export_program)export_program(program_text());});
        collisions_=new QListWidget(this);collisions_->setObjectName("camNcCollisions");collisions_->setMaximumHeight(130);content_layout()->addWidget(collisions_);
        for(const auto& c:result_->collisions){auto* item=new QListWidgetItem(collisions_);item->setData(Qt::UserRole,QString::fromStdString(c.block_id));}
        for(const auto& w:result_->warnings){auto* item=new QListWidgetItem(collisions_);item->setData(Qt::UserRole,QString::fromStdString(w.block_id));}
        for(const auto& c:result_->contacts)new QListWidgetItem(collisions_);
        collisions_->setFixedHeight(std::min(5,collisions_->count())*(fontMetrics().height()+8)+2*collisions_->frameWidth());
        connect(collisions_,&QListWidget::currentRowChanged,this,[this](int row){if(row<0)return;const auto id=collisions_->item(row)->data(Qt::UserRole).toString().toStdString();for(int i=0;i<int(program_.line_block_ids.size());++i)if(program_.line_block_ids[i]==id){lines_->setCurrentRow(i);break;}});
        retranslate();set_initial_size({680,0});present();
    }
    const cam::CompiledProgram& program()const{return program_;}
    std::string program_text()const{std::string text;for(const auto& line:program_.lines){text+=line;text+='\n';}return text;}
protected:
    bool submit()override{return true;}
    void changeEvent(QEvent* e)override{PropertiesSubWindow::changeEvent(e);if(e->type()==QEvent::LanguageChange&&collisions_)retranslate();}
private:
    std::shared_ptr<const document::CamCalculated> result_;Present present_;cam::CompiledProgram program_;std::vector<kernel::ViewerEdge> paths_;
    std::map<std::string,QString> clampings_;
    QLabel *help_{},*report_{};QListWidget *lines_{},*collisions_{};QPushButton *previous_{},*next_{},*export_{};QSlider* fraction_{};QCheckBox* remaining_{};
    const cam::Motion* motion()const {const auto row=lines_->currentRow();if(row<0)return nullptr;const auto& id=program_.line_block_ids.at(row);const auto found=std::ranges::find(program_.motions,id,&cam::Motion::block_id);return found==program_.motions.end()?nullptr:&*found;}
    void step(int direction){for(int row=lines_->currentRow()+direction;row>=0&&row<lines_->count();row+=direction){const auto& id=program_.line_block_ids[row];if(std::ranges::any_of(program_.motions,[&](const auto& m){return m.block_id==id;})){lines_->setCurrentRow(row);return;}}}
    void present(){auto edges=paths_;const auto* selected=motion();fraction_->setEnabled(selected!=nullptr);
        if(selected){for(auto& edge:edges)if(edge.reference.semantic_key==selected->block_id)edge.color="#00D1FF";
            const auto tool=std::ranges::find(result_->tools,selected->tool_id,&cam::Tool::id);if(tool!=result_->tools.end()){auto wire=cam_tool_wire(*tool,cam::point_at(*selected,fraction_->value()/1000.));edges.insert(edges.end(),std::make_move_iterator(wire.begin()),std::make_move_iterator(wire.end()));}}
        present_(std::move(edges),remaining_->isChecked());}
    void retranslate(){set_internal_title(tr("Generic NC program"));help_->setText(tr("Generic NC requires a controller postprocessor before machine use. Select a motion line to inspect the tool; stepping does not recalculate material."));
        previous_->setText(tr("Previous motion"));next_->setText(tr("Next motion"));remaining_->setText(tr("Show calculated remaining stock"));
        export_->setText(tr("Export generic NC text…"));
        report_->setText(tr("Removed material: %1 mm³. Collisions: %2. Missing target material: %3 mm³. Clamping warnings: %4.").arg(result_->removed_mm3,0,'g',10).arg(result_->collisions.size()).arg(result_->missing_target_mm3,0,'g',10).arg(result_->warnings.size()));collisions_->setVisible(collisions_->count()>0);
        for(int i=0;i<int(result_->collisions.size());++i){const auto& c=result_->collisions[i];QString kind;
            switch(c.kind){case cam::CollisionKind::RapidStock:kind=tr("Rapid move intersects stock");break;case cam::CollisionKind::CuttingFixture:kind=tr("Cutter intersects fixture");break;
            case cam::CollisionKind::NonCuttingStock:kind=tr("Shank intersects stock");break;case cam::CollisionKind::NonCuttingFixture:kind=tr("Shank intersects fixture");break;case cam::CollisionKind::TargetGouge:kind=tr("Cutter removes target material");break;
            case cam::CollisionKind::CuttingClamping:kind=tr("Cutter intersects clamping exclusion");break;case cam::CollisionKind::NonCuttingClamping:kind=tr("Shank intersects clamping exclusion");break;}
            if(clampings_.contains(c.obstacle_id))kind+=" ("+clampings_.at(c.obstacle_id)+")";
            const auto tool=std::ranges::find(result_->tools,c.tool_id,&cam::Tool::id);const auto line=std::ranges::find(program_.line_block_ids,c.block_id)-program_.line_block_ids.begin();
            collisions_->item(i)->setText(tr("Line %1: %2 — %3").arg(line+1).arg(kind,QString::fromStdString(tool==result_->tools.end()?c.tool_id:tool->name)));}
        int row=int(result_->collisions.size());const auto clamp_name=[&](const std::string& id){return clampings_.contains(id)?clampings_.at(id):tr("Clamping");};
        for(const auto& w:result_->warnings){const auto line=std::ranges::find(program_.line_block_ids,w.block_id)-program_.line_block_ids.begin();
            const auto detail=w.kind==cam::ClampingWarningKind::BelowJawTop?tr("Removed below jaw top: %1 mm³").arg(w.removed_mm3,0,'g',10):tr("Lost contact: %1 mm²").arg(w.lost_contact_mm2,0,'g',10);
            collisions_->item(row++)->setText(tr("Line %1: %2 — %3").arg(line+1).arg(clamp_name(w.clamping_id),detail));}
        for(const auto& c:result_->contacts)collisions_->item(row++)->setText(tr("%1: remaining contact %2 of %3 mm².").arg(clamp_name(c.clamping_id)).arg(c.remaining_mm2,0,'g',10).arg(c.initial_mm2,0,'g',10));
        if(!result_->contacts.empty())help_->setText(help_->text()+" "+tr("Geometric contact does not establish sufficient holding force."));
    }
};
} // namespace zima::app
