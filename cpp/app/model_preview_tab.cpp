#include "model_preview_tab.hpp"
#include "resource_icon.hpp"
#include <zima/kernel/occt_kernel.hpp>
#include <zima/kernel/step_source_document.hpp>
#include <zima/document/general_surface.hpp>
#include <zima/document/file_path.hpp>
#include <zima/viewer/mesh_view.hpp>
#include <dxfinterface.h>
#include <dxfsceneview.h>
#include <QTreeWidget>
#include <QToolBar>
#include <QAction>
#include <QLabel>
#include <QSplitter>
#include <QVBoxLayout>
#include <QFileInfo>
#include <QSignalBlocker>
#include <QTimer>
#include <QGraphicsItem>
#include <QImage>
#include <map>

namespace zima::app {
StepPreviewData load_step_preview(const QString& path){
    const auto bytes=path.toUtf8();if(QStringList{"igs","iges"}.contains(QFileInfo(path).suffix().toLower())){kernel::OcctKernel kernel;StepPreviewNode node;node.source.name=QFileInfo(path).fileName().toStdString();node.source.component_path="iges";node.mesh=kernel.import_iges(bytes.toStdString(),"preview-iges").mesh;StepPreviewData result;result.nodes.push_back(std::move(node));return result;}kernel::StepSourceDocument source(bytes.toStdString());
    const auto nodes=interchange::inspect_step_parts(source,100000);
    kernel::OcctKernel kernel;std::vector<kernel::StepRequest> requests;std::map<std::string,std::size_t> definitions;
    for(const auto& node:nodes)if(!node.assembly&&!definitions.contains(node.definition_id)){
        definitions[node.definition_id]=requests.size();requests.push_back({bytes.toStdString(),node.definition_id,{},{},node.definition_id});
    }
    auto frozen=kernel.freeze_step_components(requests,&source);
    for(std::size_t index=0;index<requests.size();++index){requests[index].frozen_brep=std::move(frozen[index].brep);requests[index].topology=std::move(frozen[index].topology);}
    const auto bodies=kernel.import_step_components(requests);
    StepPreviewData result;result.nodes.reserve(nodes.size());
    for(const auto& node:nodes){StepPreviewNode entry;entry.source=node;if(!node.assembly){
        const document::Placement placement{node.global_x,node.global_y,node.global_z,node.global_rotation_x,node.global_rotation_y,node.global_rotation_z};
        entry.mesh=document::general_surface_place_definition_mesh(placement,bodies.at(definitions.at(node.definition_id)).mesh);
    }result.nodes.push_back(std::move(entry));}
    return result;
}
ModelPreviewTab::ModelPreviewTab(const QString& path,QWidget* parent):QWidget(parent),path_(QFileInfo(path).absoluteFilePath()){
    setObjectName("modelPreviewTab");auto* layout=new QVBoxLayout(this);layout->setContentsMargins(0,0,0,0);layout->setSpacing(3);
    auto* toolbar=new QToolBar(this);toolbar->setIconSize({20,20});auto* fit=toolbar->addAction(resource_icon("fit-all"),tr("Přizpůsobit pohled"));fit->setObjectName("previewFit");layout->addWidget(toolbar);
    auto* splitter=new QSplitter(this);tree_=new QTreeWidget(splitter);tree_->setObjectName("previewTree");tree_->setHeaderHidden(true);tree_->setMinimumWidth(160);
    auto* root=new QTreeWidgetItem(tree_,{QFileInfo(path_).fileName()});root->setIcon(0,resource_icon("file-manager"));root->setExpanded(true);
    const bool dxf=QFileInfo(path_).suffix().compare("dxf",Qt::CaseInsensitive)==0;
    if(dxf){drawing_=new DXFSceneView(splitter);drawing_->setObjectName("dxfPreviewView");drawing_->setFrameShape(QFrame::NoFrame);}
    else{model_=new viewer::MeshView(splitter);model_->setObjectName("stepPreviewView");model_->set_selection_contract({});
        for(const auto& entry:std::vector<std::pair<QString,viewer::StandardView>>{{tr("Izometrický"),viewer::StandardView::Isometric},{tr("Zepředu"),viewer::StandardView::Front},{tr("Shora"),viewer::StandardView::Top},{tr("Zprava"),viewer::StandardView::Right}}){auto* action=toolbar->addAction(entry.first);connect(action,&QAction::triggered,this,[this,view=entry.second]{model_->set_standard_view(view);});}}
    splitter->setSizes({240,900});splitter->setStretchFactor(1,1);layout->addWidget(splitter,1);details_=new QLabel(this);details_->setObjectName("previewDetails");details_->setWordWrap(true);details_->setTextFormat(Qt::PlainText);layout->addWidget(details_);
    connect(fit,&QAction::triggered,this,[this]{if(model_)model_->fit_all();if(drawing_)drawing_->fitAll();});
    connect(tree_,&QTreeWidget::itemChanged,this,[this](QTreeWidgetItem* item,int){if(model_){update_model();return;}if(!dxf_)return;const QString layer=item->data(0,Qt::UserRole).toString();if(layer.isEmpty())return;for(auto* entity:dxf_->scene()->items())if(entity->data(0).toString()==layer)entity->setVisible(item->checkState(0)==Qt::Checked);});
    connect(tree_,&QTreeWidget::itemDoubleClicked,this,[this](QTreeWidgetItem* item,int){if(!model_)return;const int index=item->data(0,Qt::UserRole+1).toInt();if(index<0||index>=static_cast<int>(step_.nodes.size()))return;model_->fit_points(step_.nodes[index].mesh.vertices);});
}
ModelPreviewTab::~ModelPreviewTab(){if(drawing_)drawing_->setScene(nullptr);}
void ModelPreviewTab::load_dxf(){
    dxf_=std::make_unique<DXFInterface>(path_);if(!dxf_->loaded())throw std::runtime_error("DXF soubor nelze načíst.");drawing_->setScene(dxf_->scene());
    auto* root=tree_->topLevelItem(0);QSignalBlocker block(tree_);auto* group=new QTreeWidgetItem(root,{tr("Vrstvy")});group->setExpanded(true);
    QSet<QString> present;for(auto* entity:dxf_->scene()->items())present.insert(entity->data(0).toString());
    for(const auto& layer:dxf_->layerList()){const QString name=QString::fromStdString(layer.name);present.insert(name);}
    QStringList layers(present.begin(),present.end());layers.sort(Qt::CaseInsensitive);
    for(const auto& layer:layers){if(layer.isEmpty())continue;auto* item=new QTreeWidgetItem(group,{layer});item->setData(0,Qt::UserRole,layer);item->setCheckState(0,Qt::Checked);const auto original=dxf_->getLayer(layer.toStdString());QPixmap swatch(12,12);swatch.fill(dxf_->numberToColor(std::abs(original.color)));item->setIcon(0,QIcon(swatch));if(original.color<0||(original.flags&1)){item->setCheckState(0,Qt::Unchecked);for(auto* entity:dxf_->scene()->items())if(entity->data(0).toString()==layer)entity->setVisible(false);}}
    QString units=tr("Jednotky nejsou určené");const QStringList labels{QString{},"in","ft","mi","mm","cm","m","km","microin","mil","yd","angstrom","nm","micrometre","dm","dam","hm","Gm","au","ly","pc"};if(dxf_->units()>0&&dxf_->units()<labels.size())units=labels[dxf_->units()];
    const QRectF bounds=dxf_->scene()->itemsBoundingRect();QString message=tr("Jednotky: %1; rozměry: %2 × %3").arg(units).arg(bounds.width(),0,'g',8).arg(bounds.height(),0,'g',8);
    if(!dxf_->unsupported().isEmpty()){QStringList types(dxf_->unsupported().begin(),dxf_->unsupported().end());types.sort();message+="\n"+tr("Náhled není úplný. Nepodporované entity: %1").arg(types.join(", "));}details_->setText(message);
    QTimer::singleShot(0,this,[this]{drawing_->fitAll();});
}
void ModelPreviewTab::set_step(StepPreviewData data){
    step_=std::move(data);QSignalBlocker block(tree_);auto* root=tree_->topLevelItem(0);root->setData(0,Qt::UserRole+1,-1);std::map<std::string,QTreeWidgetItem*> entries;
    for(std::size_t index=0;index<step_.nodes.size();++index){const auto& node=step_.nodes[index].source;auto found=entries.find(node.parent_path);auto* item=new QTreeWidgetItem(found==entries.end()?root:found->second,{QString::fromStdString(node.name)});entries[node.component_path]=item;item->setData(0,Qt::UserRole+1,static_cast<int>(index));item->setCheckState(0,Qt::Checked);item->setIcon(0,resource_icon(node.assembly?"assembly":"part"));item->setExpanded(node.assembly);}
    update_model();model_->fit_all();
    kernel::Vec3 low{1e100,1e100,1e100},high{-1e100,-1e100,-1e100};
    for(const auto& point:model_->mesh().vertices){low={std::min(low.x,point.x),std::min(low.y,point.y),std::min(low.z,point.z)};high={std::max(high.x,point.x),std::max(high.y,point.y),std::max(high.z,point.z)};}
    const bool populated=!model_->mesh().vertices.empty();details_->setText(tr("Pouze prohlížení; geometrie v mm.")+"\n"+tr("Rozměry: %1 × %2 × %3 mm").arg(populated?high.x-low.x:0,0,'g',8).arg(populated?high.y-low.y:0,0,'g',8).arg(populated?high.z-low.z:0,0,'g',8));
}
void ModelPreviewTab::update_model(){
    kernel::ViewerMesh merged;QTreeWidgetItemIterator iterator(tree_);for(;*iterator;++iterator){auto* item=*iterator;if(!item->data(0,Qt::UserRole+1).isValid())continue;const int index=item->data(0,Qt::UserRole+1).toInt();if(index<0||index>=static_cast<int>(step_.nodes.size()))continue;
        bool visible=item->checkState(0)==Qt::Checked;for(auto* parent=item->parent();parent&&parent!=tree_->topLevelItem(0);parent=parent->parent())visible=visible&&parent->checkState(0)==Qt::Checked;if(!visible)continue;
        const auto& mesh=step_.nodes[index].mesh;const auto offset=static_cast<std::uint32_t>(merged.vertices.size());merged.vertices.insert(merged.vertices.end(),mesh.vertices.begin(),mesh.vertices.end());for(auto triangle:mesh.triangles)merged.triangles.push_back(triangle+offset);merged.triangle_references.insert(merged.triangle_references.end(),mesh.triangle_references.begin(),mesh.triangle_references.end());merged.edges.insert(merged.edges.end(),mesh.edges.begin(),mesh.edges.end());
    }model_->set_mesh(std::move(merged),false);
}
}
