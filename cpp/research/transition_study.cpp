// Isolated editable surface study. Never installed as the CAD product.
#include "transition_half.hpp"
#include "transition_pattern.hpp"
#include "../app/application_settings.hpp"
#include <zima/ui/properties_subwindow.hpp>
#include <zima/viewer/mesh_view.hpp>
#include <QApplication>
#include <QDialogButtonBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <algorithm>
#include <QPainter>
#include <QWheelEvent>
#include <QFormLayout>
#include <QGroupBox>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMainWindow>
#include <QMouseEvent>
#include <QPushButton>
#include <QSpinBox>
#include <QSplitter>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <array>
#include <iostream>
#include <numbers>

using namespace zima;
using namespace research::transition;
namespace {
QString text(const char* value){return QCoreApplication::translate("TransitionStudy",value);}
class PatternView final:public QWidget {
public:
    using QWidget::QWidget;
    QSize sizeHint()const override{return {600,600};}
    Pattern lines;double zoom{1};QPointF pan,last;
    void fit_all(){zoom=1;pan={};update();}
    void set_pattern(Pattern value){lines=std::move(value);fit_all();}
protected:
    void wheelEvent(QWheelEvent* event)override{zoom=std::clamp(zoom*std::pow(1.15,event->angleDelta().y()/120.),.1,20.);update();event->accept();}
    void mousePressEvent(QMouseEvent* event)override{last=event->position();}
    void mouseMoveEvent(QMouseEvent* event)override{if(event->buttons()&(Qt::LeftButton|Qt::MiddleButton)){pan+=event->position()-last;last=event->position();update();}}
    void paintEvent(QPaintEvent*)override {
        QPainter painter(this);painter.setRenderHint(QPainter::Antialiasing);painter.fillRect(rect(),QColor(35,42,50));if(lines.empty())return;
        QRectF bounds(QPointF(lines.front().first.x,lines.front().first.y),QSizeF(.001,.001));
        for(const auto& line:lines)for(auto p:{line.first,line.second})bounds=bounds.united(QRectF(p.x,p.y,.001,.001));
        const double scale=zoom*std::min((width()-60)/bounds.width(),(height()-60)/bounds.height());
        const auto project=[&](Vec3 p){return QPointF(width()/2.+pan.x()+(p.x-bounds.center().x())*scale,height()/2.+pan.y()-(p.y-bounds.center().y())*scale);};
        for(const auto& line:lines){const bool axis=line.role==PatternRole::BendAxis;QPen pen(axis?QColor(245,205,80):QColor(Qt::white),axis?.8:2.);
            if(axis){pen.setStyle(Qt::CustomDashLine);pen.setDashPattern({12,3,1.5,3});}painter.setPen(pen);painter.drawLine(project(line.first),project(line.second));}
    }
};
struct Inputs {
    std::array<double,3> root_position{},root_angles{},relative_position{0,0,150},relative_angles{};
    double radius{80},major{100},minor{60},width{200},depth{160},corner{20};int facets{4},left_facets{4};bool half{true};
    bool operator==(const Inputs&)const=default;
};
Frame frame(const std::array<double,3>& p,const std::array<double,3>& angle) {
    const double k=std::numbers::pi/180,x=angle[0]*k,y=angle[1]*k,z=angle[2]*k;
    const double cx=std::cos(x),sx=std::sin(x),cy=std::cos(y),sy=std::sin(y),cz=std::cos(z),sz=std::sin(z);
    return {{p[0],p[1],p[2]},{cz*cy,sz*cy,-sy},{cz*sy*sx-sz*cx,sz*sy*sx+cz*cx,cy*sx},{cz*sy*cx+sz*sx,sz*sy*cx-cz*sx,cy*cx}};
}
void dimensions(Model& model,const Inputs& input) {
    auto& first=model.profiles[0].sketch;auto& a=first.arcs.front();a.radius=input.radius;
    first.find_point(a.start_point_id)->x=input.radius;first.find_point(a.end_point_id)->y=input.radius;
    auto& second=model.profiles[1].sketch;auto& b=second.elliptical_arcs.front();b.major_radius=input.major;b.minor_radius=input.minor;
    second.find_point(b.major_point_id)->x=input.major;second.find_point(b.minor_point_id)->y=input.minor;
    second.find_point(b.start_point_id)->x=input.major;second.find_point(b.end_point_id)->y=input.minor;
    model.first_origin=frame(input.root_position,input.root_angles);model.second_relative=frame(input.relative_position,input.relative_angles);
    model.options.facets=static_cast<std::size_t>(input.facets);
}
class Editor final:public ui::PropertiesSubWindow {
public:
    Editor(Inputs input,std::function<void(Inputs)> commit,QWidget* parent):PropertiesSubWindow(text("Parametry prototypu přechodu"),parent),pending(input),commit_(std::move(commit)) {
        setObjectName("transitionStudyProperties");setAttribute(Qt::WA_DeleteOnClose);set_initial_size({470,660});
        auto* explanation=new QLabel(text("První Origin umísťuje celý model. Druhý Origin je vztažený k prvnímu."),this);explanation->setWordWrap(true);content_layout()->addWidget(explanation);
        auto* tabs=new QTabWidget(this);content_layout()->addWidget(tabs);
        auto page=[&](const QString& title){auto* w=new QWidget(tabs);tabs->addTab(w,title);auto* layout=new QFormLayout(w);return layout;};
        auto number=[&](QFormLayout* layout,const QString& label,const QString& name,double& value,double min,double max,const QString& suffix){
            auto* box=new QDoubleSpinBox(this);box->setObjectName(name);box->setRange(min,max);box->setDecimals(4);box->setSuffix(suffix);box->setValue(value);layout->addRow(label,box);
            connect(box,&QDoubleSpinBox::valueChanged,this,[&value](double v){value=v;});return box;
        };
        auto* geometry=page(text("Skici"));
        auto* mode=new QComboBox(this);mode->setObjectName("mode");mode->addItems({text("Rohový přechod"),text("Půlkruh na zaoblený půlobdélník")});mode->setCurrentIndex(pending.half?1:0);geometry->addRow(text("Typ přechodu"),mode);
        number(geometry,text("Poloměr první skici"),"radius",pending.radius,.1,1000," mm");
        number(geometry,text("První poloosa druhé skici"),"major",pending.major,.1,1000," mm");
        number(geometry,text("Druhá poloosa druhé skici"),"minor",pending.minor,.1,1000," mm");
        number(geometry,text("Šířka obdélníku"),"width",pending.width,.1,2000," mm");
        number(geometry,text("Celková hloubka obdélníku"),"depth",pending.depth,.1,2000," mm");
        number(geometry,text("Poloměr rohů obdélníku"),"corner",pending.corner,.1,1000," mm");
        auto* count=new QSpinBox(this);count->setObjectName("facets");count->setRange(2,128);count->setValue(pending.facets);geometry->addRow(text("Počet rovinných plošek"),count);
        connect(count,&QSpinBox::valueChanged,this,[this](int v){pending.facets=v;});
        auto* left_count=new QSpinBox(this);left_count->setObjectName("leftFacets");left_count->setRange(2,128);left_count->setValue(pending.left_facets);geometry->addRow(text("Počet plošek levého rohu"),left_count);
        connect(left_count,&QSpinBox::valueChanged,this,[this](int v){pending.left_facets=v;});
        auto* count_label=qobject_cast<QLabel*>(geometry->labelForField(count));
        const auto show_mode=[this,geometry,count_label](bool half){
            for(const auto* name:{"width","depth","corner","leftFacets"})geometry->setRowVisible(findChild<QWidget*>(name),half);
            for(const auto* name:{"major","minor"})geometry->setRowVisible(findChild<QWidget*>(name),!half);
            count_label->setText(half?text("Počet plošek pravého rohu"):text("Počet rovinných plošek"));
        };
        show_mode(pending.half);connect(mode,&QComboBox::currentIndexChanged,this,[this,show_mode](int index){pending.half=index==1;show_mode(pending.half);});
        auto* note=new QLabel(text("Mezi N ploškami je N−1 vnitřních hran. Bez tloušťky a poloměrů ohybů."),this);note->setWordWrap(true);geometry->addRow(note);
        const auto origin_page=[&](QFormLayout* layout,const QString& prefix,std::array<double,3>& position,std::array<double,3>& rotation){
            for(int i=0;i<3;++i){const auto axis=QString(QChar('X'+i));number(layout,axis,prefix+axis,position[i],-10000,10000," mm");}
            for(int i=0;i<3;++i){const auto axis=QString(QChar('X'+i));number(layout,text("Natočení %1").arg(axis),prefix+"R"+axis,rotation[i],-180,180,QString::fromUtf8(" °"));}
        };
        origin_page(page(text("První Origin")),"root",pending.root_position,pending.root_angles);
        origin_page(page(text("Druhý Origin vůči prvnímu")),"relative",pending.relative_position,pending.relative_angles);
    }
    Inputs pending;
private:
    bool submit()override{commit_(pending);return true;}
    std::function<void(Inputs)> commit_;
};
class Window final:public QMainWindow {
public:
    Model model=Model::example();Inputs input;Result result;HalfResult half_result;viewer::MeshView *view{};PatternView* flat{};QLabel* status{};QPushButton* edit{};
    Window() {
        setWindowTitle(text("Prototyp přechodu — plošný model"));resize(1400,860);
        auto* central=new QWidget(this);auto* layout=new QVBoxLayout(central);setCentralWidget(central);
        auto* warning=new QLabel(text("Výzkumný prototyp: editovatelná plocha, nikoli hotový plechový příkaz."),this);layout->addWidget(warning);
        auto* convention=new QLabel(text("Skici určují vnější rozměry. Žluté osy označují skutečné ohyby; tloušťka zatím není započítána."),this);convention->setWordWrap(true);layout->addWidget(convention);
        auto* row=new QHBoxLayout;layout->addLayout(row);edit=new QPushButton(text("Upravit skici a počátky…"),this);row->addWidget(edit);
        auto* fit=new QPushButton(text("Přizpůsobit"),this);row->addWidget(fit);row->addStretch();
        auto* export_button=new QPushButton(text("Export rozvinu…"),this);row->addWidget(export_button);
        connect(export_button,&QPushButton::clicked,this,[this]{
            QString filter;auto path=QFileDialog::getSaveFileName(this,text("Export rozvinu…"),"transition-study.dxf","DXF (*.dxf);;SVG (*.svg)",&filter);if(path.isEmpty())return;
            if(QFileInfo(path).suffix().isEmpty())path+=filter.startsWith("SVG")?".svg":".dxf";
            try{export_pattern(flat->lines,std::filesystem::path(path.toStdWString()));status->setText(text("Rozvin prototypu byl exportován: %1").arg(path));}
            catch(const std::exception&){status->setText(text("Export rozvinu se nezdařil."));}
        });
        auto* splitter=new QSplitter(this);layout->addWidget(splitter,1);
        auto panel=[&](const QString& title,viewer::MeshView*& target){auto* group=new QGroupBox(title,splitter);auto* box=new QVBoxLayout(group);target=new viewer::MeshView(group);target->set_display_mode(viewer::DisplayMode::ShadedWithEdges);box->addWidget(target);splitter->addWidget(group);};
        panel(text("Prostorový model"),view);
        auto* flat_group=new QGroupBox(text("Geometrický rozvin"),splitter);auto* flat_layout=new QVBoxLayout(flat_group);flat=new PatternView(flat_group);flat_layout->addWidget(flat);splitter->addWidget(flat_group);
        splitter->setStretchFactor(0,1);splitter->setStretchFactor(1,1);splitter->setSizes({700,700});
        status=new QLabel(this);status->setWordWrap(true);layout->addWidget(status);
        connect(edit,&QPushButton::clicked,this,[this]{open_editor();});connect(fit,&QPushButton::clicked,this,[this]{view->fit_all();flat->fit_all();});
        commit(input);view->set_standard_view(viewer::StandardView::Isometric);
    }
    Editor* open_editor(){edit->setEnabled(false);auto* d=new Editor(input,[this](Inputs value){commit(value);},this);connect(d,&QObject::destroyed,this,[this]{edit->setEnabled(true);});d->show();return d;}
    void commit(Inputs next) {
        auto candidate=model;dimensions(candidate,next);
        HalfModel half;half.first_origin=candidate.first_origin;half.second_relative=candidate.second_relative;
        half.radius=next.radius;half.width=next.width;half.depth=next.depth;half.corner_radius=next.corner;half.corner_facets={static_cast<std::size_t>(next.facets),static_cast<std::size_t>(next.left_facets)};
        const auto calculated=next.half?Result{}:candidate.evaluate();const auto calculated_half=next.half?calculate(half):HalfResult{};
        if(!calculated.valid()||!calculated_half.valid())throw std::runtime_error(text("Pro tuto kombinaci profilů a natočení nebyl nalezen platný rozvinutelný přechod. Změňte rozměry nebo polohu druhé skici.").toStdString());
        model=std::move(candidate);input=next;result=calculated;half_result=calculated_half;
        view->set_mesh(input.half?mesh(half_result):mesh(result),false);flat->set_pattern(input.half?pattern(half_result):pattern(result));view->fit_all();flat->fit_all();
        const auto& deviation=input.half?half_result.boundary_deviation:result.boundary_deviation;
        status->setText(text("Plošky: %1; vnitřní hrany: %2. Horní mez odchylky obrysu: %3 / %4 mm. Nejde o výrobní rozvin.")
            .arg(input.half?half_result.faces.size():result.facets.size()).arg(input.half?half_result.folds.size():result.folds.size()).arg(deviation[0].upper,0,'f',3).arg(deviation[1].upper,0,'f',3));
    }
};
void install_language(QApplication& app,const QString& language) {
    QFile file("config/localization/"+language+".qt.json");if(!file.open(QIODevice::ReadOnly))throw std::runtime_error("Run the study from the repository directory");
    const auto object=QJsonDocument::fromJson(file.readAll()).object();app::ApplicationSettings settings;settings.language=language;
    for(auto it=object.begin();it!=object.end();++it)settings.qt_translations.insert(it.key(),it.value().toString());app::apply_application_translations(app,settings);
}
void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
void verify(QApplication& app) {
    for(const auto& language:{"cs","en","de","fr","ru"}) {
        install_language(app,language);Window w;w.show();app.processEvents();
        const auto original=w.input;const auto id=w.model.profiles[0].curve_id;auto* d=w.open_editor();app.processEvents();
        check((d->windowFlags()&Qt::WindowType_Mask)==Qt::SubWindow,"Properties must remain internal");
        if(QString(language)!="cs")check(d->windowTitle()!=QString::fromUtf8("Parametry prototypu přechodu"),"Dialog title untranslated");
        d->findChild<QDoubleSpinBox*>("relativeZ")->setValue(240);d->reject();app.processEvents();
        check(w.input==original,"Cancel changed model");
        d=w.open_editor();d->findChild<QDoubleSpinBox*>("relativeZ")->setValue(240);d->findChild<QSpinBox*>("facets")->setValue(8);d->findChild<QSpinBox*>("leftFacets")->setValue(8);app.processEvents();
        QMouseEvent double_click(QEvent::MouseButtonDblClick,QPointF(40,40),w.view->mapToGlobal(QPoint(40,40)),Qt::MiddleButton,Qt::MiddleButton,Qt::NoModifier);
        QApplication::sendEvent(w.view,&double_click);app.processEvents();
        check(w.input.relative_position[2]==240&&w.half_result.faces.size()==19,"Middle-button OK over View did not commit");
        check(w.model.profiles[0].curve_id==id,"Parameter edit replaced source sketch identity");
        d=w.open_editor();check(d->pending==w.input,"Reopening lost model inputs");
        d->findChild<QDoubleSpinBox*>("relativeRY")->setValue(30);d->buttons()->button(QDialogButtonBox::Ok)->click();app.processEvents();
        check(w.input.relative_angles[1]==30&&w.half_result.valid(),"Compatible tilted half transition rejected");
        d=w.open_editor();d->findChild<QDoubleSpinBox*>("corner")->setValue(1000);d->buttons()->button(QDialogButtonBox::Ok)->click();app.processEvents();
        check(w.input.corner==20&&d->isVisible(),"Invalid input committed or closed editor");d->reject();app.processEvents();
        QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
        check(w.edit->isEnabled(),"Editor cannot be reopened after close");
        d=w.open_editor();d->findChild<QComboBox*>("mode")->setCurrentIndex(0);d->findChild<QDoubleSpinBox*>("relativeRY")->setValue(0);
        d->buttons()->button(QDialogButtonBox::Ok)->click();app.processEvents();
        check(!w.input.half&&w.result.facets.size()==8,"Corner study mode regression");
        d=w.open_editor();d->findChild<QComboBox*>("mode")->setCurrentIndex(1);d->findChild<QDoubleSpinBox*>("relativeRY")->setValue(30);
        d->buttons()->button(QDialogButtonBox::Ok)->click();app.processEvents();
        check(w.input.half&&w.half_result.faces.size()==19,"Half transition mode did not restore");
        QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
        if(QString(language)=="cs") {
            export_pattern(w.flat->lines,"build/transition-model/transition-axes.dxf");export_pattern(w.flat->lines,"build/transition-model/transition-axes.svg");
            w.grab().save("build/transition-model/native-study.png");
            d=w.open_editor();app.processEvents();
            w.grab().save("build/transition-model/native-study-properties.png");d->reject();
        }
        w.close();app.processEvents();
    }
    std::cout<<"Editable transition study GUI: five languages, Cancel, middle-button OK, reopen and invalid input passed\n";
}
}
int main(int argc,char** argv) {
    QApplication application(argc,argv);application.setStyle("Fusion");
    try {
        if(application.arguments().contains("--verify")){verify(application);return 0;}
        install_language(application,"cs");Window window;window.show();return application.exec();
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
