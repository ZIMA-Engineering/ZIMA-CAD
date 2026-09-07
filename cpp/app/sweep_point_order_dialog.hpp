#pragma once
#include <zima/document/part_document.hpp>
#include <zima/ui/properties_subwindow.hpp>
#include <QComboBox>
#include <QHeaderView>
#include <QLabel>
#include <QTableWidget>
#include <QVBoxLayout>
namespace zima::app {
class SweepPointOrderDialog final : public zima::ui::PropertiesSubWindow {
public:
    SweepPointOrderDialog(const zima::sketcher::Sketch& sketch,
        const std::string& start, std::function<void(std::string)> preview, QWidget* parent, bool allow_open)
        : PropertiesSubWindow(tr("Pořadí bodů profilu"),parent),
          sketch_(sketch), preview_(std::move(preview)), allow_open_(allow_open) {
        setAttribute(Qt::WA_DeleteOnClose,true);
        setObjectName("sweepPointOrderDialog");
        setMinimumWidth(400);
        auto* help=new QLabel(tr("Zvolte první bod. Další body následují po obvodu proti směru hodinových "
            "ručiček při pohledu proti normále skici. Mezi profily se spojuje 1 → 1, 2 → 2, …"),this);
        help->setWordWrap(true);content_layout()->addWidget(help);
        first_=new QComboBox(this);first_->setObjectName("sweepFirstCorrespondencePoint");
        first_->addItem(tr("Automaticky"),QString{});
        table_=new QTableWidget(this);table_->setColumnCount(3);
        table_->setHorizontalHeaderLabels({tr("Pořadí"),tr("X [mm]"),tr("Y [mm]")});
        table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        const auto mapping=zima::document::sweep3d_profile_correspondence(sketch_,{},allow_open_);
        for(std::size_t i=0;i<mapping.point_ids.size();++i) {
            if (!mapping.closed && i!=0 && i+1!=mapping.point_ids.size()) continue;
            const auto* point=sketch_.find_point(mapping.point_ids[i]);
            first_->addItem(tr("Bod %1 (%2; %3)").arg(i+1).arg(point->x).arg(point->y),
                QString::fromStdString(point->id));
        }
        if (!mapping.closed) {
            help->setText(tr("Otevřený profil se páruje od zvoleného koncového bodu. "
                "Volbou druhého konce obrátíte pořadí párování i stranu tloušťky."));
        }
        first_->setCurrentIndex(std::max(0,first_->findData(QString::fromStdString(start))));
        content_layout()->addWidget(first_);content_layout()->addWidget(table_);
        if(mapping.point_ids.empty()) {
            auto* info=new QLabel(tr("Kružnice bez bodů se spojují bez pootočení. Pro řízené párování "
                "přidejte ve Sketchi body s vazbou C na kružnici nebo K na její kvadranty."),this);
            info->setWordWrap(true);content_layout()->addWidget(info);
            first_->setEnabled(false);
        }
        refresh();
        connect(first_,&QComboBox::currentIndexChanged,this,[this] {
            refresh();preview_(first_->currentData().toString().toStdString());
        });
    }
protected:
    bool submit() override {
        preview_(first_->currentData().toString().toStdString());
        return true;
    }
private:
    void refresh() {
        const auto mapping=zima::document::sweep3d_profile_correspondence(
            sketch_,first_->currentData().toString().toStdString(),allow_open_);
        table_->setRowCount(static_cast<int>(mapping.point_ids.size()));
        for(std::size_t i=0;i<mapping.point_ids.size();++i) {
            const auto* point=sketch_.find_point(mapping.point_ids[i]);
            table_->setItem(i,0,new QTableWidgetItem(i==0?tr("1 – začátek"):QString::number(i+1)));
            table_->setItem(i,1,new QTableWidgetItem(QString::number(point->x)));
            table_->setItem(i,2,new QTableWidgetItem(QString::number(point->y)));
        }
    }
    zima::sketcher::Sketch sketch_;
    std::function<void(std::string)> preview_;
    bool allow_open_{};
    QComboBox* first_{};
    QTableWidget* table_{};
};

}
