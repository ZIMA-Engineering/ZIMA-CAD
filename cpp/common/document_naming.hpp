#pragma once
#include <QString>
#include <string>

namespace zima {
// Applied only to new user-supplied document names, never to source paths.
struct DocumentNaming {
    bool uppercase{}, remove_diacritics{}, replace_spaces{};
    QString normalize(QString name) const {
        if(!uppercase&&!remove_diacritics&&!replace_spaces)return name;
        if(replace_spaces)name=name.trimmed();
        QString suffix;
        for(const auto* extension:{".prtz",".asmz",".drwz",".tblz",".frmz"})
            if(name.endsWith(QLatin1String(extension),Qt::CaseInsensitive)) {
                suffix=QLatin1String(extension);name.chop(suffix.size());break;
            }
        if(remove_diacritics) {
            QString plain;
            for(const auto c:name.normalized(QString::NormalizationForm_D))
                if(c.category()!=QChar::Mark_NonSpacing&&c.category()!=QChar::Mark_SpacingCombining&&c.category()!=QChar::Mark_Enclosing)plain+=c;
            name=plain.normalized(QString::NormalizationForm_C);
        }
        if(uppercase)name=name.toUpper();
        if(replace_spaces) {
            QString compact;bool pending=false;
            for(const auto c:name) {
                if(c.isSpace()){pending=!compact.isEmpty();continue;}
                if(pending)compact+=QLatin1Char('_');
                compact+=c;pending=false;
            }
            name=compact;
        }
        return name+suffix;
    }
    std::string operator()(const std::string& name) const { return normalize(QString::fromStdString(name)).toStdString(); }
};
}
