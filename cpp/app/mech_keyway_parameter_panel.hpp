#pragma once
#include <zima/document/keyway_catalog.hpp>
#include <zima/ui/reference_cell.hpp>
#include <zima/ui/unit_spin_box.hpp>
#include <zima/symbols/placement.hpp>
#include <zima/kernel/stable_id.hpp>
#include <QCheckBox>
#include <QPushButton>
#include <QComboBox>
#include <QEvent>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QSignalBlocker>
#include <QStandardItemModel>
#include <QTableWidget>
#include <QToolButton>
#include <QVBoxLayout>
#include <functional>

namespace zima::app {
// The ordinary placement section owns the axial Origin and orientation.
// This panel owns only the analytic support and the keyway's manufacturing data.
class MechKeywayParameterPanel final:public QWidget {
public:
    std::function<void(int)> pick_requested;
    std::function<void()> changed,inspection_changed;
    std::function<void(int)> roughness_edit_requested;
    std::function<void()> annotations_changed;
    std::function<QString(const kernel::FaceReference&)> face_label;
    explicit MechKeywayParameterPanel(document::MechKeywayParameters initial,QWidget* parent)
        :QWidget(parent),pending_(std::move(initial)) {
        setObjectName("mechKeywayParameters");
        auto* layout=new QVBoxLayout(this);layout->setContentsMargins(0,0,0,0);
        refs_=new QTableWidget(3,4,this);refs_->setObjectName("mechKeywayReferences");
        refs_->verticalHeader()->hide();refs_->verticalHeader()->setDefaultSectionSize(34);
        refs_->setSelectionMode(QAbstractItemView::NoSelection);refs_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        ui::install_reference_cell_delegate(refs_);
        for(int col:{0,1,3}){refs_->horizontalHeader()->setSectionResizeMode(col,QHeaderView::Fixed);refs_->setColumnWidth(col,34);}
        refs_->horizontalHeader()->setSectionResizeMode(2,QHeaderView::Stretch);layout->addWidget(refs_);
        for(int i=0;i<3;++i) {
            refs_->setItem(i,0,new QTableWidgetItem(QString::number(i+1)));
            cells_[i]=new ui::ReferenceCellItem;refs_->setItem(i,2,cells_[i]);
            indicators_[i]=ui::build_reference_row_indicator([this,i]{
                if(i){reference(i)={};inspected_[i]=false;notify();if(inspection_changed)inspection_changed();}
                begin_entry(i);
            });
            refs_->setCellWidget(i,1,indicators_[i]);
            eyes_[i]=ui::build_reference_inspection_button(false,false,[this,i](bool on){inspected_[i]=on;refresh();if(inspection_changed)inspection_changed();});
            refs_->setCellWidget(i,3,ui::centered_cell_widget(eyes_[i]));
        }
        connect(refs_,&QTableWidget::cellClicked,this,[this](int row,int col){if(col==2)begin_entry(row);});
        form_=new QFormLayout;layout->addLayout(form_);
        side_=new QLabel(this);add(QT_TR_NOOP("Support"),side_);
        type_=choice(QT_TR_NOOP("Key type"),"mechKeyType",{QT_TR_NOOP("Parallel key"),QT_TR_NOOP("Woodruff key")},int(pending_.type),[this](int n){
            pending_.type=static_cast<document::KeywayType>(n);
            pending_.table_id=n?"din6888-a-i":"din6885-1-maedler";pending_.table_row_id.clear();
            if(n){pending_.retaining_screws=0;if(!pending_.internal){pending_.position=document::KeywayPosition::Symmetric;
                pending_.first_end=pending_.last_end=document::KeywayEnd::Round;pending_.first_overrun=pending_.last_overrun=0;}}
            refill_catalogues();
        });
        catalogue_=new QComboBox(this);catalogue_->setObjectName("mechKeyCatalogue");add(QT_TR_NOOP("Standard / series"),catalogue_);refill_catalogues();
        connect(catalogue_,&QComboBox::currentIndexChanged,this,[this](int){
            pending_.table_id=catalogue_->currentData().toString().toStdString();pending_.table_row_id.clear();notify();
        });
        size_=new QComboBox(this);size_->setObjectName("mechKeySize");add(QT_TR_NOOP("Key size"),size_);
        connect(size_,&QComboBox::currentIndexChanged,this,[this](int index){
            if(index<0)return;pending_.table_row_id=size_->itemData(index).toString().toStdString();notify();
        });
        depth_=new QLabel(this);depth_->setObjectName("mechKeyDepth");add("Hloubka",depth_);
        position_=choice(QT_TR_NOOP("Axial placement"),"mechKeyPosition",{QT_TR_NOOP("From face / plane"),QT_TR_NOOP("Symmetric"),QT_TR_NOOP("Two directions")},int(pending_.position),
            [this](int n){pending_.position=static_cast<document::KeywayPosition>(n);});
        direction_=choice("Směr","mechKeyDirection",{"Vpřed","Vzad"},pending_.axial_direction<0?1:0,
            [this](int n){pending_.axial_direction=n?-1:1;});
        offset_=number("Odsazení","mechKeyOffset",pending_.axial_offset,-1e6,1e6,[this](double n){pending_.axial_offset=n;});
        length_=number("Délka","mechKeyLength",pending_.length,1e-5,1e6,[this](double n){pending_.length=n;});
        for(int i=0;i<2;++i) {
            limits_[i]=choice(i?QT_TR_NOOP("Reverse termination"):QT_TR_NOOP("Forward termination"),i?"mechKeyLimit1":"mechKeyLimit0",{"Délka","Až k..."},int(pending_.limits[i].termination),
                [this,i](int n){pending_.limits[i].termination=static_cast<document::KeywayTermination>(n);if(!n){inspected_[i+1]=false;if(active_==i+1)active_=-1;}});
            lengths_[i]=number(i?QT_TR_NOOP("Reverse length"):QT_TR_NOOP("Forward length"),i?"mechKeyLength1":"mechKeyLength0",pending_.limits[i].length,0,1e6,[this,i](double n){pending_.limits[i].length=n;});
            corrections_[i]=number(i?QT_TR_NOOP("Reverse correction"):QT_TR_NOOP("Forward correction"),i?"mechKeyCorrection1":"mechKeyCorrection0",pending_.limits[i].correction,-1e6,1e6,[this,i](double n){pending_.limits[i].correction=n;});
            ends_[i]=choice(i?QT_TR_NOOP("Last end"):QT_TR_NOOP("First end"),i?"mechKeyEnd1":"mechKeyEnd0",{QT_TR_NOOP("Rounded end"),QT_TR_NOOP("Open end")},int(i?pending_.last_end:pending_.first_end),[this,i](int n){
                (i?pending_.last_end:pending_.first_end)=static_cast<document::KeywayEnd>(n);
                if(!n)(i?pending_.last_overrun:pending_.first_overrun)=0;
            });
            overruns_[i]=number(i?QT_TR_NOOP("Last overrun"):QT_TR_NOOP("First overrun"),i?"mechKeyOverrun1":"mechKeyOverrun0",i?pending_.last_overrun:pending_.first_overrun,0,1e6,
                [this,i](double n){(i?pending_.last_overrun:pending_.first_overrun)=n;});
        }
        floor_=choice(QT_TR_NOOP("Floor direction"),"mechKeyFloor",{QT_TR_NOOP("Parallel to axis"),QT_TR_NOOP("Parallel to cone generator")},int(pending_.floor),
            [this](int n){pending_.floor=static_cast<document::KeywayFloor>(n);});
        screws_=choice(QT_TR_NOOP("Retaining screws"),"mechKeyScrews",{"Žádné","1","2"},int(pending_.retaining_screws),[this](int n){pending_.retaining_screws=unsigned(n);});
        roughness_=new QCheckBox(this);roughness_->setObjectName("mechKeyRoughness");roughness_->setChecked(pending_.show_roughness);add(QT_TR_NOOP("Show surface texture"),roughness_);
        roughness_standard_=new QComboBox(this);roughness_standard_->setObjectName("mechKeyRoughnessStandard");add(QT_TR_NOOP("Surface texture symbol"),roughness_standard_);
        for(int i=0;i<2;++i){roughness_values_[i]=new QPushButton(this);roughness_values_[i]->setObjectName(i?"mechKeyFloorTexture":"mechKeyWallTexture");
            add(i?QT_TR_NOOP("Floor surface texture"):QT_TR_NOOP("Wall surface texture"),roughness_values_[i]);
            connect(roughness_values_[i],&QPushButton::clicked,this,[this,i]{if(roughness_edit_requested)roughness_edit_requested(i);});}
        connect(roughness_,&QCheckBox::toggled,this,[this](bool on){pending_.show_roughness=on;if(on)ensure_roughness();refresh();notify();});
        connect(roughness_standard_,&QComboBox::currentIndexChanged,this,[this](int){
            if(!pending_.show_roughness)return;ensure_roughness(true);refresh();if(annotations_changed)annotations_changed();notify();});
        preview_error_=new QLabel(this);preview_error_->setObjectName("mechKeyPreviewError");
        preview_error_->setWordWrap(true);layout->addWidget(preview_error_);preview_error_->hide();
        ready_=true;refresh();
    }
    [[nodiscard]] const document::MechKeywayParameters& parameters()const{return pending_;}
    [[nodiscard]] const std::vector<symbols::Placement>& pending_annotations()const{return annotations_;}
    [[nodiscard]] sketcher::SymbolInstance roughness_instance(int row)const{
        const auto& id=row?pending_.floor_symbol_id:pending_.walls_symbol_id;
        return std::ranges::find(annotations_,id,[](const auto& value){return value.symbol.id;})->symbol;
    }
    void set_roughness_instance(int row,sketcher::SymbolInstance value){
        const auto& id=row?pending_.floor_symbol_id:pending_.walls_symbol_id;
        auto item=std::ranges::find(annotations_,id,[](const auto& entry){return entry.symbol.id;});
        if(item==annotations_.end()||value.id!=id||item->symbol==value)return;
        item->symbol=std::move(value);refresh();if(annotations_changed)annotations_changed();notify();
    }
    void initialize_roughness(std::vector<symbols::Definition> definitions,const std::vector<symbols::Placement>& existing){
        definitions_=std::move(definitions);annotations_.clear();
        for(const auto& value:existing)if(value.symbol.id==pending_.walls_symbol_id||value.symbol.id==pending_.floor_symbol_id)annotations_.push_back(value);
        const QSignalBlocker guard(roughness_standard_);roughness_standard_->clear();
        for(const auto& definition:definitions_)roughness_standard_->addItem(QString::fromStdString(definition.name),QString::fromStdString(definition.id));
        if(!annotations_.empty()){
            const auto stored=symbols::Definition::from_serialized(annotations_.front().symbol.definition);
            if(roughness_standard_->findData(QString::fromStdString(stored.id))<0){definitions_.push_back(stored);roughness_standard_->addItem(QString::fromStdString(stored.name),QString::fromStdString(stored.id));}
            roughness_standard_->setCurrentIndex(roughness_standard_->findData(QString::fromStdString(stored.id)));
        }
        refresh();
    }
    [[nodiscard]] int active_pick()const{return active_;}
    void begin_entry(int row){if(row<0||row>2||(row&&!pending_.internal))return;active_=row;refresh();if(pick_requested)pick_requested(row);}
    void finish_entry(){active_=-1;inspected_.fill(false);refresh();if(inspection_changed)inspection_changed();}
    void set_support(kernel::FaceReference face,const kernel::SurfaceGeometry& surface,int initial_direction=0) {
        if(!pending_.support.valid()&&initial_direction)pending_.axial_direction=initial_direction;
        pending_.support=std::move(face);pending_.internal=surface.reversed;
        cone_=surface.kind==kernel::SurfaceGeometry::Kind::Cone;
        if(cone_&&!pending_.internal&&pending_.type!=document::KeywayType::Woodruff){
            pending_.type=document::KeywayType::Woodruff;pending_.table_id="din6888-a-i";pending_.table_row_id.clear();
        }
        if(pending_.internal){pending_.position=document::KeywayPosition::TwoSided;pending_.first_end=pending_.last_end=document::KeywayEnd::Open;pending_.retaining_screws=0;
            for(auto& limit:pending_.limits)limit.termination=document::KeywayTermination::UpTo;}
        else if(pending_.type==document::KeywayType::Woodruff){pending_.position=document::KeywayPosition::Symmetric;pending_.retaining_screws=0;
            pending_.first_end=pending_.last_end=document::KeywayEnd::Round;pending_.first_overrun=pending_.last_overrun=0;}
        inspected_[0]=false;active_=-1;refill_catalogues();refresh();notify();
    }
    void set_limit(int side,kernel::FaceReference face){pending_.limits.at(side).reference=std::move(face);inspected_[side+1]=false;active_=-1;refresh();notify();}
    void update_derived(const document::MechKeywayParameters& value,bool cone,double support_diameter=0){
        preview_error_key_.clear();preview_error_->clear();preview_error_->hide();
        // Only catalogue-derived quantities change here. Authored extents and
        // full-precision values must not be rounded by display widgets.
        pending_.width=value.width;pending_.key_height=value.key_height;pending_.depth=value.depth;
        pending_.table_row_id=value.table_row_id;pending_.bottom_radius=value.bottom_radius;
        pending_.woodruff_diameter=value.woodruff_diameter;pending_.width_fit=value.width_fit;
        pending_.depth_fit=value.depth_fit;pending_.depth_upper_tolerance=value.depth_upper_tolerance;
        pending_.depth_lower_tolerance=value.depth_lower_tolerance;
        pending_.retaining_diameter=value.retaining_diameter;pending_.retaining_pitch=value.retaining_pitch;
        if(pending_.position==document::KeywayPosition::TwoSided)pending_.length=value.length;
        pending_.retaining_depth=value.retaining_depth;cone_=cone;support_diameter_=support_diameter;refresh();
    }
    void set_draft_error(const char* message){preview_error_key_=message;refresh();}
    [[nodiscard]] std::vector<kernel::FaceReference> inspected_faces()const {
        std::vector<kernel::FaceReference> result;for(int i=0;i<3;++i)if(inspected_[i]&&reference(i).valid())result.push_back(reference(i));return result;
    }
    void refresh(){
        if(!ready_)return;
        // Hub grooves pass through between two explicit end references. Length
        // remains a derived measurement; it is never an authored blind cut.
        if(pending_.internal){pending_.position=document::KeywayPosition::TwoSided;
            pending_.first_end=pending_.last_end=document::KeywayEnd::Open;
            for(auto& limit:pending_.limits)limit.termination=document::KeywayTermination::UpTo;}
        const bool disc=pending_.type==document::KeywayType::Woodruff&&!pending_.internal;
        const bool two=pending_.position==document::KeywayPosition::TwoSided;
        side_->setText(pending_.support.valid()?(pending_.internal?tr("Náboj"):tr("Hřídel")):tr("Zadejte referenci"));
        {
            const QSignalBlocker guard(size_);size_->clear();
            for(const auto& catalogue:document::keyway_catalogues())if(catalogue.id==pending_.table_id)
                for(const auto& row:catalogue.rows)if(support_diameter_>row.diameter_over&&support_diameter_<=row.diameter_through){
                    auto text=QStringLiteral("%1 × %2").arg(row.width).arg(row.height);
                    if(row.disc_diameter)text+=QStringLiteral(" × %1").arg(row.disc_diameter);
                    size_->addItem(text+" mm",QString::fromStdString(row.id));
                }
            if(size_->count()==0)size_->addItem(QStringLiteral("%1 × %2 mm").arg(pending_.width).arg(pending_.key_height),QString::fromStdString(pending_.table_row_id));
            size_->setCurrentIndex(size_->findData(QString::fromStdString(pending_.table_row_id)));size_->setEnabled(size_->count()>1);
        }
        depth_->setText(QStringLiteral("%1 mm").arg(pending_.depth));
        sync(type_,int(pending_.type));type_->setEnabled(!(cone_&&!pending_.internal));
        sync(position_,int(pending_.position));position_->setEnabled(!disc&&!pending_.internal);
        form_->setRowVisible(position_,!pending_.internal);
        sync(direction_,pending_.axial_direction<0?1:0);
        sync(floor_,int(pending_.floor));form_->setRowVisible(floor_,cone_&&pending_.internal);
        form_->setRowVisible(length_,!two&&!disc);form_->setRowVisible(screws_,!pending_.internal&&!disc);
        sync(screws_,int(pending_.retaining_screws));
        roughness_->setEnabled(!definitions_.empty()||pending_.show_roughness);
        form_->setRowVisible(roughness_standard_,pending_.show_roughness);
        for(int i=0;i<2;++i){
            form_->setRowVisible(roughness_values_[i],pending_.show_roughness);
            const auto& id=i?pending_.floor_symbol_id:pending_.walls_symbol_id;
            const auto item=std::ranges::find(annotations_,id,[](const auto& value){return value.symbol.id;});
            roughness_values_[i]->setEnabled(item!=annotations_.end());
            roughness_values_[i]->setText(item!=annotations_.end()&&item->symbol.text_values.contains("Specification")?
                QString::fromStdString(item->symbol.text_values.at("Specification")):tr("Vlastnosti symbolu"));
        }
        const document::KeywayCatalogRow* selected_row{};
        for(const auto& catalogue:document::keyway_catalogues())if(catalogue.id==pending_.table_id)
            for(const auto& row:catalogue.rows)if(row.id==pending_.table_row_id)selected_row=&row;
        for(int count=1;count<=2;++count){
            const auto interval=selected_row?(count==1?selected_row->one_screw_lengths:selected_row->two_screw_lengths):std::nullopt;
            const bool available=interval&&pending_.length>=(*interval)[0]&&pending_.length<=(*interval)[1];
            static_cast<QStandardItemModel*>(screws_->model())->item(count)->setEnabled(available);
        }
        refs_->setHorizontalHeaderLabels({tr("Č."),{},tr("Reference"),{}});int visible=1;
        for(int i=0;i<2;++i){
            const bool upto=pending_.internal;
            if(!upto)pending_.limits[i].termination=document::KeywayTermination::Length;
            sync(limits_[i],int(pending_.limits[i].termination));
            {const QSignalBlocker guard(corrections_[i]);corrections_[i]->setMinimum(upto?0:-1e6);}
            form_->setRowVisible(limits_[i],false);form_->setRowVisible(lengths_[i],two&&!upto);form_->setRowVisible(corrections_[i],upto);
            form_->setRowVisible(ends_[i],!disc&&!pending_.internal);sync(ends_[i],int(i?pending_.last_end:pending_.first_end));
            const bool open=(i?pending_.last_end:pending_.first_end)==document::KeywayEnd::Open;
            {const QSignalBlocker guard(overruns_[i]);overruns_[i]->setValue(i?pending_.last_overrun:pending_.first_overrun);}
            form_->setRowVisible(overruns_[i],!disc&&open);refs_->setRowHidden(i+1,!upto);if(upto)++visible;
        }
        refs_->setFixedHeight(refs_->horizontalHeader()->sizeHint().height()+34*visible+2*refs_->frameWidth());
        for(int i=0;i<3;++i){
            const auto& face=reference(i);const bool populated=face.valid();
            cells_[i]->setText(populated?(face_label?face_label(face):QString::fromStdString(face.semantic_key)):
                i==0?tr("Select a cylindrical or conical support face"):i==1?tr("Forward target face / plane"):tr("Reverse target face / plane"));
            if(populated)cells_[i]->set_reference(QString::fromStdString(face.owner_id+":"+face.semantic_key));else cells_[i]->clear_reference();
            cells_[i]->set_active_input(active_==i);cells_[i]->set_inspected(inspected_[i]);
            cells_[i]->setToolTip(i==0?tr("Select a cylindrical or conical support face"):
                i==1?tr("Forward target face / plane"):tr("Reverse target face / plane"));
            // The support is required and replaceable, never removable.
            ui::set_reference_row_populated(indicators_[i],i&&populated);
            eyes_[i]->setEnabled(populated);const QSignalBlocker guard(eyes_[i]);eyes_[i]->setChecked(inspected_[i]);
        }
        if(!preview_error_key_.empty()){
            preview_error_->setText(tr(preview_error_key_.c_str()));preview_error_->show();
            const QSignalBlocker guard(size_);size_->clear();size_->setEnabled(false);depth_->setText(QStringLiteral("—"));
        }
    }
protected:
    void changeEvent(QEvent* event)override {
        QWidget::changeEvent(event);if(event->type()!=QEvent::LanguageChange)return;
        for(const auto& [widget,key]:labels_)static_cast<QLabel*>(form_->labelForField(widget))->setText(tr(key));
        for(const auto& [combo,keys]:choices_)for(int i=0;i<int(keys.size());++i)combo->setItemText(i,tr(keys[i]));refill_catalogues();refresh();
    }
private:
    void ensure_roughness(bool replace=false){
        const auto index=roughness_standard_->currentIndex();if(index<0||index>=int(definitions_.size()))return;
        const auto& definition=definitions_[index];
        for(int i=0;i<2;++i){
            auto& id=i?pending_.floor_symbol_id:pending_.walls_symbol_id;
            if(id.empty())id=kernel::make_stable_id();
            auto found=std::ranges::find(annotations_,id,[](const auto& value){return value.symbol.id;});
            if(found==annotations_.end()){
                symbols::Placement placed;placed.symbol.id=id;placed.symbol.x=15;placed.symbol.y=i?12:24;
                placed.symbol.text_values["Specification"]=i?"Ra 6.3":"Ra 3.2";
                placed.leader=true;placed.perpendicular_leader=true;
                annotations_.push_back(std::move(placed));found=std::prev(annotations_.end());
            }else if(!replace)continue;
            found->symbol.definition=definition.serialized();
            if(!definition.variants.contains(found->symbol.variant))found->symbol.variant=definition.default_variant;
        }
    }
    void add(const char* label,QWidget* widget){form_->addRow(tr(label),widget);labels_.emplace_back(widget,label);}
    QComboBox* choice(const char* label,const char* name,std::vector<const char*> keys,int selected,std::function<void(int)> write){
        auto* combo=new QComboBox(this);combo->setObjectName(name);for(const auto* key:keys)combo->addItem(tr(key));combo->setCurrentIndex(selected);add(label,combo);
        choices_.emplace_back(combo,std::move(keys));connect(combo,&QComboBox::currentIndexChanged,this,[this,write=std::move(write)](int n){write(n);refresh();notify();});return combo;
    }
    ui::UnitDoubleSpinBox* number(const char* label,const char* name,double initial,double low,double high,std::function<void(double)> write){
        auto* field=new ui::UnitDoubleSpinBox(ui::InputQuantity::Length,this);
        field->setObjectName(name);field->setRange(low,high);field->set_display_decimals(ui::numeric_decimal_places(this));field->setValue(initial);add(label,field);
        connect(field,&QDoubleSpinBox::valueChanged,this,[this,write=std::move(write)](double n){write(n);notify();});return field;
    }
    static void sync(QComboBox* box,int index){const QSignalBlocker guard(box);box->setCurrentIndex(index);}
    void refill_catalogues(){
        if(!catalogue_)return;const QSignalBlocker guard(catalogue_);catalogue_->clear();
        for(const auto& cat:document::keyway_catalogues())if(cat.type==pending_.type){
            QString label;
            if(cat.id=="din6885-1-maedler")label=QStringLiteral("DIN 6885-1 · A (Mädler)");
            else if(cat.id=="zima-csn-parallel")label=tr("CSN 02 2507 / 02 2562 (historical table)");
            else if(cat.id=="zima-woodruff")label=tr("Historical CSN tables");
            else {label=QString::fromStdString(cat.id).toUpper();label.replace("DIN6888-","DIN 6888 · ");label.replace('-'," · ");}
            catalogue_->addItem(label,QString::fromStdString(cat.id));
        }
        catalogue_->setCurrentIndex(catalogue_->findData(QString::fromStdString(pending_.table_id)));
    }
    kernel::FaceReference& reference(int row){return row?pending_.limits.at(row-1).reference:pending_.support;}
    const kernel::FaceReference& reference(int row)const{return row?pending_.limits.at(row-1).reference:pending_.support;}
    void notify(){if(ready_&&changed)changed();}
    document::MechKeywayParameters pending_;
    std::string preview_error_key_;
    bool ready_{},cone_{};int active_{-1};double support_diameter_{};
    QFormLayout* form_{};QTableWidget* refs_{};
    std::array<ui::ReferenceCellItem*,3> cells_{};std::array<QWidget*,3> indicators_{};std::array<QToolButton*,3> eyes_{};std::array<bool,3> inspected_{};
    QLabel *side_{},*depth_{},*preview_error_{};QComboBox *size_{},*type_{},*catalogue_{},*position_{},*direction_{},*floor_{},*screws_{};
    ui::UnitDoubleSpinBox *offset_{},*length_{};
    std::array<QComboBox*,2> limits_{},ends_{};
    std::array<ui::UnitDoubleSpinBox*,2> lengths_{},corrections_{},overruns_{};
    std::vector<std::pair<QWidget*,const char*>> labels_;
    std::vector<std::pair<QComboBox*,std::vector<const char*>>> choices_;
    QCheckBox* roughness_{};QComboBox* roughness_standard_{};
    std::array<QPushButton*,2> roughness_values_{};
    std::vector<symbols::Definition> definitions_;std::vector<symbols::Placement> annotations_;
};
}
