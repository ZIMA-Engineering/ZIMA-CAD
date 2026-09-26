#pragma once
#include <QCheckBox>
#include <QHBoxLayout>
#include <functional>

namespace zima::app {
class OriginDisplayControls final : public QWidget {
public:
    OriginDisplayControls(bool point, bool text, QWidget* parent, std::function<void()> changed)
        : QWidget(parent) {
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
}
