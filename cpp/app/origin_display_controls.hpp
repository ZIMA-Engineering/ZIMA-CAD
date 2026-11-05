#pragma once
#include <QCheckBox>
#include <QHBoxLayout>
#include <functional>

namespace zima::app {
class OriginDisplayControls final : public QWidget {
public:
    OriginDisplayControls(bool point, bool text, QWidget* parent, std::function<void()> changed)
        : QWidget(parent) {
        setObjectName("originDisplayControls");
        auto* row=new QHBoxLayout(this);row->setContentsMargins(0,0,0,0);
        point_=new QCheckBox(QObject::tr("Bod"),this);point_->setObjectName("originShowPoint");
        text_=new QCheckBox(QObject::tr("Text"),this);text_->setObjectName("originShowText");
        point_->setChecked(point);text_->setChecked(text);
        row->addWidget(point_);row->addWidget(text_);row->addStretch();
        connect(point_,&QCheckBox::toggled,this,[changed]{if(changed)changed();});
        connect(text_,&QCheckBox::toggled,this,[changed]{if(changed)changed();});
    }
    bool point() const { return point_->isChecked(); }
    bool text() const { return text_->isChecked(); }
private:
    QCheckBox *point_{},*text_{};
};

// Some specialized dialogs share the placement layout with commands which
// still expose Origin presentation. Remove the complete row after installing
// that shared placement UI; deleting the widget also removes its layout item,
// so no blank checkbox row remains.
inline void remove_origin_display_controls(QWidget* parent) {
    // Adding the row to PropertiesSubWindow's content layout reparents it to
    // the internal content widget, so find it recursively from the dialog.
    if (auto* controls = parent->findChild<QWidget*>("originDisplayControls"))
        delete controls;
}
}
