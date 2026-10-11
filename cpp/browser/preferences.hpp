#pragma once
#include <QString>
#include <QUrl>

namespace zima::browser {
inline QString official_url() {
    return QStringLiteral("https://zima-engineering.cz/05-Software/01-ZIMA-CAD/0000-index/index_cs.html");
}
inline bool allowed_url(const QUrl& url) {
    return url.isValid() && !url.host().isEmpty() && url.userInfo().isEmpty()
        && (url.scheme() == "http" || url.scheme() == "https");
}
struct Preferences {
    QString home{official_url()};
    bool open_on_start{};
    bool ask_download_path{};
    bool block_third_party_cookies{true};
    bool save_passwords{true};
    bool autofill_passwords{true};
};
}
