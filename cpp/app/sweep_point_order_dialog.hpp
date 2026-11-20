#pragma once
#include <zima/document/part_document.hpp>
#include <zima/ui/properties_subwindow.hpp>
#include <zima/ui/unit_spin_box.hpp>
#include <QComboBox>
#include <QHeaderView>
#include <QLabel>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QCheckBox>
#include <QPointer>
#include <QPushButton>
#include <zima/viewer/mesh_view.hpp>
#include "../common/interaction_colors.hpp"
namespace zima::app {
class SweepPointOrderDialog final : public zima::ui::PropertiesSubWindow {
public:
    SweepPointOrderDialog(const zima::sketcher::Sketch& sketch,
        const std::string& start, bool reversed, std::function<void(std::string,bool)> preview, QWidget* parent, bool allow_open)
        : PropertiesSubWindow(tr("Pořadí bodů profilu"),parent),
          sketch_(sketch.evaluated_profile_sketch()), preview_(std::move(preview)), allow_open_(allow_open) {
        setAttribute(Qt::WA_DeleteOnClose,true);
        setObjectName("sweepPointOrderDialog");
        setMinimumWidth(400);
        help_=new QLabel(tr("Ve View vyberte první bod a potom sousední bod pro směr. Mezi profily se spojuje 1 → 1, 2 → 2, …"),this);
        help_->setWordWrap(true);content_layout()->addWidget(help_);
        first_=new QComboBox(this);first_->setObjectName("sweepFirstCorrespondencePoint");
        first_->addItem(tr("Automaticky"),QString{});
        table_=new QTableWidget(this);table_->setColumnCount(3);
        const auto unit=zima::ui::document_unit(this,"Length","mm");
        table_->setHorizontalHeaderLabels({tr("Pořadí"),tr("X [%1]").arg(unit),tr("Y [%1]").arg(unit)});
        table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        const auto mapping=zima::document::sweep3d_profile_correspondence(sketch_,{},allow_open_);
        closed_=mapping.closed;
        for(std::size_t i=0;i<mapping.point_ids.size();++i) {
            if (!mapping.closed && i!=0 && i+1!=mapping.point_ids.size()) continue;
            const auto* point=sketch_.find_point(mapping.point_ids[i]);
            first_->addItem(tr("Bod %1 (%2; %3)").arg(i+1).arg(coordinate(point->x),coordinate(point->y)),
                QString::fromStdString(point->id));
        }
        if (!mapping.closed) {
            help_->setText(tr("Otevřený profil se páruje od zvoleného koncového bodu. "
                "Volbou druhého konce obrátíte pořadí párování i stranu tloušťky."));
        }
        first_->setCurrentIndex(std::max(0,first_->findData(QString::fromStdString(start))));
        reverse_=new QCheckBox(tr("Obrátit směr"),this);reverse_->setObjectName("sweepCorrespondenceReversed");
        reverse_->setChecked(reversed);reverse_->setEnabled(closed_&&mapping.point_ids.size()>2);
        pick_=new QPushButton(tr("Vybrat ve View"),this);pick_->setObjectName("sweepPickPointOrder");
        pick_->setEnabled(!mapping.point_ids.empty());
        connect(pick_,&QPushButton::clicked,this,[this]{await_second_=false;set_pick_help();arm_view();});
        content_layout()->addWidget(first_);content_layout()->addWidget(reverse_);
        content_layout()->addWidget(pick_);content_layout()->addWidget(table_);
        if(mapping.point_ids.empty()) {
            auto* info=new QLabel(tr("Kružnice bez bodů se spojují bez pootočení. Pro řízené párování "
                "přidejte ve Sketchi body s vazbou C na kružnici nebo K na její kvadranty."),this);
            info->setWordWrap(true);content_layout()->addWidget(info);
            first_->setEnabled(false);
        }
        refresh();
        connect(first_,&QComboBox::currentIndexChanged,this,[this] {publish();});
        connect(reverse_,&QCheckBox::toggled,this,[this] {publish();});
        connect(this,&QDialog::finished,this,[this]{release_view();});
    }
    // The owning command supplies its already prepared, scene-coordinate
    // points. The dialog never calculates geometry or reconstructs placement.
    void bind_view(zima::viewer::MeshView* view,std::string owner,std::string prefix,std::string occurrence={}) {
        if(!view)return;
        view_=view;owner_=std::move(owner);prefix_=std::move(prefix);occurrence_=std::move(occurrence);
        saved_contract_=view->selection_contract();saved_filter_=view->candidate_filter();
        saved_advance_=view->advances_selection_on_hover();arm_view();
    }
protected:
    bool submit() override {
        publish();
        return true;
    }
private:
    void set_pick_help() {
        help_->setText(!closed_?tr("Otevřený profil se páruje od zvoleného koncového bodu. Volbou druhého konce obrátíte pořadí párování i stranu tloušťky."):
            await_second_?tr("Vyberte sousední bod pro směr číslování."):
            tr("Ve View vyberte první bod a potom sousední bod pro směr. Mezi profily se spojuje 1 → 1, 2 → 2, …"));
    }
    void pick_point(const std::string& id) {
        const auto item=first_->findData(QString::fromStdString(id));
        if(item<0)return; // An open contour offers only its real endpoints.
        if(!await_second_||!closed_) {
            first_->setCurrentIndex(item);await_second_=closed_;set_pick_help();return;
        }
        const auto first=first_->currentData().toString().toStdString();
        const auto normal=zima::document::sweep3d_profile_correspondence(sketch_,first,allow_open_);
        if(normal.point_ids.size()<2)return;
        if(id!=normal.point_ids[1]&&id!=normal.point_ids.back()) {
            help_->setText(tr("Vyberte sousední bod pro směr číslování."));return;
        }
        reverse_->setChecked(id==normal.point_ids.back()&&normal.point_ids.size()>2);
        await_second_=false;publish();set_pick_help();
    }
    void publish() {
        refresh();preview_(first_->currentData().toString().toStdString(),reverse_->isChecked());arm_view();
    }
    void arm_view() {
        if(!view_)return;
        update_links();
        view_->clear_selection();view_->set_command_snap_points(view_points_);
        view_->set_selection_contract({zima::viewer::CandidateKind::Vertex});
        const QPointer<SweepPointOrderDialog> self(this);
        view_->set_candidate_filter([self](const auto& candidate){return self&&std::ranges::any_of(self->view_points_,[&](const auto& point){
            return point.reference.owner_id==candidate.owner_id&&point.reference.semantic_key==candidate.semantic_key&&point.reference.instance_path==candidate.instance_path;});});
        view_->set_command_gesture_callbacks([self](const auto& candidate,const auto&,const auto&) {
            if(self&&candidate)for(const auto& point:self->sketch_.points)
                if(candidate->semantic_key.ends_with(":"+point.id)){self->pick_point(point.id);break;}
            return true;
        },{},{});
    }
    void release_view() {
        if(!view_)return;
        update_links(false);
        view_->set_command_gesture_callbacks({},{},{});view_->set_command_snap_points({});
        view_->set_selection_contract(saved_contract_);view_->set_candidate_filter(saved_filter_,saved_advance_);
        view_->clear_selection();view_=nullptr;
    }
    static std::string profile_group(const std::string& key) {
        if(key.starts_with("loft:input:from:")) {
            const auto at=key.find(":point:");return at==std::string::npos?std::string{}:key.substr(0,at+7);
        }
        if(key.starts_with("sweep2d:profile-point:")||key.starts_with("profile-point:")) {
            auto at=key.find(":in:");if(at!=std::string::npos)return key.substr(0,at+4);
            at=key.find(":out:");if(at!=std::string::npos)return key.substr(0,at+5);
        }
        return {};
    }
    void update_links(bool visible=true) {
        std::vector<std::pair<std::string,std::vector<zima::kernel::ViewerPoint>>> groups;
        const auto collect=[&](const auto& points) {
            for(const auto& point:points) {
                const auto& ref=point.reference;
                if(ref.owner_id!=owner_||ref.instance_path!=occurrence_)continue;
                const auto key=profile_group(ref.semantic_key);if(key.empty())continue;
                auto group=std::ranges::find_if(groups,[&](const auto& item){return item.first==key;});
                if(group==groups.end()){groups.emplace_back(key,std::vector<zima::kernel::ViewerPoint>{});group=std::prev(groups.end());}
                if(!std::ranges::any_of(group->second,[&](const auto& item){return item.reference.semantic_key==ref.semantic_key;}))group->second.push_back(point);
            }
        };
        // Transient command points take precedence over any retained base scene.
        collect(view_->transient_model_points());collect(view_->mesh().points);
        view_points_.clear();
        auto edges=view_->transient_edges();
        std::erase_if(edges,[](const auto& edge){return edge.reference.semantic_key.starts_with("profile-correspondence-link:");});
        if(visible)for(std::size_t g=0;g<groups.size();++g)if(groups[g].first==prefix_) {
            view_points_=groups[g].second;
            if(prefix_.starts_with("loft:"))continue; // Loft already publishes all profile-to-profile guides.
            for(const int step:{-1,1}) {
                const auto next=static_cast<int>(g)+step;
                if(next<0||next>=static_cast<int>(groups.size())||groups[next].second.size()!=view_points_.size())continue;
                for(std::size_t i=0;i<view_points_.size();++i) {
                    zima::kernel::ViewerEdge link;
                    link.points={view_points_[i].position,groups[next].second[i].position};
                    link.reference={owner_,"profile-correspondence-link:"+std::to_string(next)+":"+std::to_string(i),occurrence_};
                    link.overlay=true;link.preview_terminal_dashed=true;
                    link.color=zima::interaction::hover.name().toStdString();edges.push_back(std::move(link));
                }
            }
        }
        // set_transient_edges retires its old point/label overlays; retain the
        // owning command's prepared points and labels without rebuilding it.
        const auto points=view_->transient_model_points();const auto labels=view_->transient_labels();
        view_->set_transient_edges(std::move(edges));view_->set_transient_model_points(points);view_->set_transient_labels(labels);
    }
    QString coordinate(double native)const {
        const auto unit=zima::ui::document_unit(this,"Length","mm");
        return locale().toString(native/zima::document::length_unit_mm(unit.toStdString()),
            'f',zima::ui::numeric_decimal_places(this));
    }
    void refresh() {
        const auto mapping=zima::document::sweep3d_profile_correspondence(
            sketch_,first_->currentData().toString().toStdString(),allow_open_,reverse_->isChecked());
        table_->setRowCount(static_cast<int>(mapping.point_ids.size()));
        for(std::size_t i=0;i<mapping.point_ids.size();++i) {
            const auto* point=sketch_.find_point(mapping.point_ids[i]);
            table_->setItem(i,0,new QTableWidgetItem(i==0?tr("1 – začátek"):QString::number(i+1)));
            table_->setItem(i,1,new QTableWidgetItem(coordinate(point->x)));
            table_->setItem(i,2,new QTableWidgetItem(coordinate(point->y)));
        }
    }
    zima::sketcher::Sketch sketch_;
    std::function<void(std::string,bool)> preview_;
    bool allow_open_{},closed_{},await_second_{};
    QComboBox* first_{};
    QTableWidget* table_{};
    QCheckBox* reverse_{};
    QPushButton* pick_{};
    QLabel* help_{};
    QPointer<zima::viewer::MeshView> view_;
    std::vector<zima::kernel::ViewerPoint> view_points_;
    std::vector<zima::viewer::CandidateKind> saved_contract_;
    std::function<bool(const zima::viewer::ViewerCandidate&)> saved_filter_;
    bool saved_advance_{};
    std::string owner_,prefix_,occurrence_;
};

}
