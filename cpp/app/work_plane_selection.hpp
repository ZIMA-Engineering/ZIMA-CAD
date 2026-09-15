#pragma once
#include <QComboBox>
#include <QSignalBlocker>

namespace zima::app {
inline bool automatic_work_plane(const QComboBox* combo) {
    return combo->currentData() == QVariant(QStringLiteral("auto"));
}
inline QVariant selected_work_plane(const QComboBox* combo) {
    return automatic_work_plane(combo) ? combo->currentData(Qt::UserRole + 1) : combo->currentData();
}
inline void update_automatic_work_plane(QComboBox* combo, const QVariant& plane, const QString& source = {}) {
    const QSignalBlocker blocker(combo);
    const int index = combo->findData(QStringLiteral("auto"));
    if (index < 0) return;
    const auto label = source.isEmpty() ? combo->itemText(combo->findData(plane)) : source;
    combo->setItemText(index, QObject::tr("Automaticky — %1").arg(label));
    combo->setItemData(index, plane, Qt::UserRole + 1);
}
inline void install_automatic_work_plane(QComboBox* combo, bool automatic) {
    const auto plane = combo->currentData();
    combo->addItem(QObject::tr("Automaticky"), QStringLiteral("auto"));
    update_automatic_work_plane(combo, plane);
    if (automatic) combo->setCurrentIndex(combo->count() - 1);
    combo->setToolTip(QObject::tr("Automaticky podle první rovinné reference. Výběr XY/XZ/YZ uloží ruční volbu v místních souřadnicích kontejneru."));
}
}
