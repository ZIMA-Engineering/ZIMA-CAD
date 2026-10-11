#include "credentials.hpp"
#include <qtkeychain/keychain.h>
#include <zima/ui/properties_subwindow.hpp>
#include <QAuthenticator>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFormLayout>
#include <QHeaderView>
#include <QLineEdit>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QTableWidget>
#include <QTimer>
#include <QUuid>
#include <QVBoxLayout>
#include <QWebChannel>
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QWebEngineScript>
#include <QWebEngineScriptCollection>

namespace zima::browser {
namespace {
QString metadata_path(const QString& directory){return QDir(directory).filePath("credentials.ini");}
QString storage_error(QKeychain::Error error) {
    if(error==QKeychain::AccessDenied||error==QKeychain::AccessDeniedByUser)
        return QObject::tr("Access to the system credential store was denied.");
    if(error==QKeychain::NoBackendAvailable||error==QKeychain::NotImplemented)
        return QObject::tr("No supported system credential store is available.");
    if(error==QKeychain::CouldNotDeleteEntry)
        return QObject::tr("The saved password could not be removed from the system credential store.");
    return QObject::tr("The system credential store could not complete the operation.");
}
QVariantMap metadata(QSettings& settings,const QString& id){
    settings.beginGroup("Accounts/"+id);
    QVariantMap result;for(const auto& key:settings.childKeys())result[key]=settings.value(key);
    settings.endGroup();result["id"]=id;return result;
}
QStringList account_ids(QSettings& settings){settings.beginGroup("Accounts");auto ids=settings.childGroups();settings.endGroup();return ids;}
QList<QVariantMap> accounts(const QString& directory){
    QSettings settings(metadata_path(directory),QSettings::IniFormat);
    QList<QVariantMap> result;for(const auto& id:account_ids(settings))result.push_back(metadata(settings,id));return result;
}
bool same_form(const QVariantMap& stored,const QVariantMap& request){
    return stored.value("type")=="form"&&stored.value("origin")==request.value("origin")
        &&stored.value("actionOrigin")==request.value("actionOrigin")&&stored.value("actionPath")==request.value("actionPath");
}
class AuthenticationDialog final:public ui::PropertiesSubWindow {
public:
    AuthenticationDialog(QString site,QString realm,QWidget* owner)
        :PropertiesSubWindow(tr("Website sign-in"),owner){
        setObjectName("browserAuthenticationDialog");set_initial_size({460,260});
        auto* form=new QFormLayout;
        form->addRow(tr("Website"),new QLabel(site,this));
        if(!realm.isEmpty())form->addRow(tr("Realm"),new QLabel(realm,this));
        user=new QLineEdit(this);password=new QLineEdit(this);password->setEchoMode(QLineEdit::Password);
        remember=new QCheckBox(tr("Save password"),this);
        form->addRow(tr("Username"),user);form->addRow(tr("Password"),password);form->addRow(remember);
        content_layout()->addLayout(form);content_layout()->addStretch();
    }
    QLineEdit* user{};QLineEdit* password{};QCheckBox* remember{};
protected:bool submit()override{return !user->text().isEmpty();}
};
class ManagerDialog final:public ui::PropertiesSubWindow {
public:
    ManagerDialog(QString directory,QWidget* owner):PropertiesSubWindow(tr("Saved passwords"),owner),directory_(std::move(directory)){
        setObjectName("browserPasswordsDialog");set_initial_size({650,400});
        filter_=new QLineEdit(this);filter_->setPlaceholderText(tr("Search websites and usernames"));content_layout()->addWidget(filter_);
        table_=new QTableWidget(this);table_->setObjectName("browserPasswordAccounts");table_->setColumnCount(2);table_->setHorizontalHeaderLabels({tr("Website"),tr("Username")});
        table_->setSelectionBehavior(QAbstractItemView::SelectRows);table_->setSelectionMode(QAbstractItemView::SingleSelection);
        table_->setEditTriggers(QAbstractItemView::NoEditTriggers);table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        content_layout()->addWidget(table_,1);
        auto* remove=new QPushButton(tr("Remove saved password"),this);remove->setObjectName("browserPasswordRemove");remove->setEnabled(false);content_layout()->addWidget(remove);
        connect(filter_,&QLineEdit::textChanged,this,[this]{refresh();});
        connect(table_,&QTableWidget::itemSelectionChanged,this,[this,remove]{
            const auto* item=table_->item(table_->currentRow(),0);
            remove->setEnabled(item!=nullptr);
            remove->setText(item&&item->data(Qt::UserRole).toString().startsWith("never:")
                ?tr("Allow password saving for this website"):tr("Remove saved password"));
        });
        connect(remove,&QPushButton::clicked,this,[this,remove]{
            const auto row=table_->currentRow();if(row<0)return;
            const auto id=table_->item(row,0)->data(Qt::UserRole).toString();
            if(id.startsWith("never:")) {
                QSettings settings(metadata_path(directory_),QSettings::IniFormat);
                auto origins=settings.value("NeverSaveOrigins").toStringList();origins.removeAll(id.mid(6));
                settings.setValue("NeverSaveOrigins",origins);settings.sync();refresh();return;
            }
            auto* job=new QKeychain::DeletePasswordJob("ZIMA-CAD",this);job->setKey("browser/credential/"+id);job->setInsecureFallback(false);
            remove->setEnabled(false);
            connect(job,&QKeychain::Job::finished,this,[this,id,remove](QKeychain::Job* job){
                if(job->error()!=QKeychain::NoError&&job->error()!=QKeychain::EntryNotFound){remove->setEnabled(true);QMessageBox::warning(this,tr("Saved passwords"),storage_error(job->error()));return;}
                QSettings settings(metadata_path(directory_),QSettings::IniFormat);settings.remove("Accounts/"+id);settings.sync();refresh();
            });job->start();
        });refresh();
    }
protected:bool submit()override{return true;}
private:
    void refresh(){table_->setRowCount(0);for(const auto& item:accounts(directory_)){
        const auto site=item.value("origin").toString(),username=item.value("username").toString();
        if(!filter_->text().isEmpty()&&!site.contains(filter_->text(),Qt::CaseInsensitive)&&!username.contains(filter_->text(),Qt::CaseInsensitive))continue;
        const auto row=table_->rowCount();table_->insertRow(row);auto* cell=new QTableWidgetItem(site);cell->setData(Qt::UserRole,item.value("id"));
        table_->setItem(row,0,cell);table_->setItem(row,1,new QTableWidgetItem(username));
    }
        QSettings settings(metadata_path(directory_),QSettings::IniFormat);
        for(const auto& site:settings.value("NeverSaveOrigins").toStringList()){
            if(!filter_->text().isEmpty()&&!site.contains(filter_->text(),Qt::CaseInsensitive))continue;
            const auto row=table_->rowCount();table_->insertRow(row);auto* cell=new QTableWidgetItem(site);
            cell->setData(Qt::UserRole,"never:"+site);table_->setItem(row,0,cell);table_->setItem(row,1,new QTableWidgetItem(tr("Never save")));
        }
    }
    QString directory_;QTableWidget* table_{};QLineEdit* filter_{};
};
}
Credentials::Credentials(QString directory,const Preferences& preferences,QWidget* owner)
    :QObject(owner),directory_(std::move(directory)),preferences_(preferences),owner_(owner){}
QString Credentials::origin(const QUrl& url){
    if(!allowed_url(url))return {};
    QUrl value;value.setScheme(url.scheme().toLower());value.setHost(url.host().toLower());
    const int port=url.port();if(port!=-1&&!(port==443&&value.scheme()=="https")&&!(port==80&&value.scheme()=="http"))value.setPort(port);
    return value.toString(QUrl::FullyEncoded);
}
bool Credentials::valid_request(const QUrl& page,const QVariantMap& request){
    const auto actual=origin(page);
    const bool protected_transport=page.scheme()=="https"||page.host()=="localhost"||page.host()=="127.0.0.1"||page.host()=="::1";
    return protected_transport&&!actual.isEmpty()&&request.value("origin").toString()==actual
        &&request.value("actionOrigin").toString()==actual
        &&request.value("actionPath").toString().startsWith('/')
        &&request.value("requestId").toString().size()<=256;
}
void Credentials::warn(const QString& error){QMessageBox::warning(owner_,tr("Credential storage unavailable"),error);}
void Credentials::autofill(QPointer<QWebEnginePage> page,QVariantMap request,std::function<void(QVariantMap)> reply){
    QVariantMap response{{"requestId",request.value("requestId")}};
    if(!preferences_.autofill_passwords||!page||!valid_request(page->url(),request)){reply(response);return;}
    QList<QVariantMap> matches;QStringList names;
    for(const auto& item:accounts(directory_))if(same_form(item,request)){matches.push_back(item);names.push_back(item.value("username").toString());}
    response["availableUsernames"]=names;response["savedAccountsLabel"]=tr("Saved accounts (%1)").arg(matches.size());
    response["chooseAccountLabel"]=tr("Use saved account");response["chooseAccountPlaceholder"]=tr("Choose account");
    QVariantMap selected;
    for(const auto& item:matches)if(item.value("username")==request.value("username")){selected=item;break;}
    if(selected.isEmpty()&&matches.size()==1&&request.value("username").toString().isEmpty())selected=matches.front();
    if(selected.isEmpty()){reply(response);return;}
    auto* job=new QKeychain::ReadPasswordJob("ZIMA-CAD",this);job->setKey("browser/credential/"+selected.value("id").toString());job->setInsecureFallback(false);
    const auto original=page->url();
    connect(job,&QKeychain::Job::finished,this,[this,page,request,reply=std::move(reply),response,selected,original](QKeychain::Job* base)mutable{
        auto* job=static_cast<QKeychain::ReadPasswordJob*>(base);
        if(page&&page->url()==original&&valid_request(page->url(),request)&&job->error()==QKeychain::NoError){
            response["username"]=selected.value("username");response["password"]=job->textData();
        }
        reply(response);
    });job->start();
}
void Credentials::submitted(QPointer<QWebEnginePage> page,QVariantMap request){
    if(!preferences_.save_passwords||!page||!valid_request(page->url(),request)
        ||request.value("username").toString().isEmpty()||request.value("password").toString().isEmpty())return;
    QSettings settings(metadata_path(directory_),QSettings::IniFormat);
    const auto site=request.value("origin").toString();
    const auto blocked=settings.value("NeverSaveOrigins").toStringList();if(blocked.contains(site))return;
    QVariantMap existing;
    for(const auto& item:accounts(directory_))if(same_form(item,request)&&item.value("username")==request.value("username")){existing=item;break;}
    if(!existing.isEmpty()&&!request.value("secretChecked").toBool()){
        auto* job=new QKeychain::ReadPasswordJob("ZIMA-CAD",this);job->setKey("browser/credential/"+existing.value("id").toString());job->setInsecureFallback(false);
        connect(job,&QKeychain::Job::finished,this,[this,page,request](QKeychain::Job* base)mutable{
            auto* job=static_cast<QKeychain::ReadPasswordJob*>(base);
            if(job->error()==QKeychain::NoError&&job->textData()==request.value("password").toString())return;
            if(job->error()!=QKeychain::NoError&&job->error()!=QKeychain::EntryNotFound){warn(storage_error(job->error()));return;}
            request["secretChecked"]=true;submitted(page,request);
        });job->start();return;
    }
    QMessageBox box(owner_);box.setWindowTitle(tr("Save password"));
    box.setText(existing.isEmpty()?tr("Save password for %1?").arg(site):tr("Update saved password for %1?").arg(site));
    box.setInformativeText(tr("Username: %1").arg(request.value("username").toString()));
    auto* save=box.addButton(existing.isEmpty()?tr("Save"):tr("Update"),QMessageBox::AcceptRole);
    box.addButton(tr("Not now"),QMessageBox::RejectRole);auto* never=box.addButton(tr("Never for this website"),QMessageBox::DestructiveRole);
    box.exec();
    if(box.clickedButton()==never){auto next=blocked;next.push_back(site);settings.setValue("NeverSaveOrigins",next);settings.sync();return;}
    if(box.clickedButton()!=save||!page||!valid_request(page->url(),request))return;
    const auto id=existing.isEmpty()?QUuid::createUuid().toString(QUuid::WithoutBraces):existing.value("id").toString();
    auto* job=new QKeychain::WritePasswordJob("ZIMA-CAD",this);job->setKey("browser/credential/"+id);job->setInsecureFallback(false);
    job->setTextData(request.value("password").toString());request.remove("password");
    connect(job,&QKeychain::Job::finished,this,[this,id,request](QKeychain::Job* job){
        if(job->error()!=QKeychain::NoError){warn(storage_error(job->error()));return;}
        QSettings settings(metadata_path(directory_),QSettings::IniFormat);settings.beginGroup("Accounts/"+id);
        for(const auto& key:QStringList{"origin","username","actionOrigin","actionPath"})settings.setValue(key,request.value(key));
        settings.setValue("type","form");settings.endGroup();settings.sync();
        if(settings.status()!=QSettings::NoError)warn(tr("Cannot save credential metadata."));
    });job->start();
}
CredentialsBridge::CredentialsBridge(Credentials& credentials,QWebEnginePage* page):QObject(page),credentials_(credentials),page_(page){}
void CredentialsBridge::requestAutofill(const QVariantMap& request){
    QPointer<CredentialsBridge> self=this;
    credentials_.autofill(page_,request,[self](QVariantMap response){if(self)emit self->autofillReady(response);});
}
void CredentialsBridge::formSubmitted(const QVariantMap& request){credentials_.submitted(page_,request);}
void Credentials::attach(QWebEnginePage* page){
    auto* channel=new QWebChannel(page);channel->registerObject("zimaPasswordManager",new CredentialsBridge(*this,page));
    page->setWebChannel(channel,QWebEngineScript::ApplicationWorld);
    connect(page,&QWebEnginePage::authenticationRequired,this,[this,page](const QUrl& url,QAuthenticator* authenticator){
        // Authentication callbacks must be completed synchronously; form keychain I/O stays asynchronous.
        const auto site=origin(url),realm=authenticator->realm();
        AuthenticationDialog dialog(site,realm,owner_);dialog.user->setText(authenticator->user());
        QVariantMap existing;
        const bool secure=url.scheme()=="https"||url.host()=="localhost"||url.host()=="127.0.0.1";
        if(secure&&preferences_.autofill_passwords)for(const auto& item:accounts(directory_)) {
            if(item.value("type")!="http"||item.value("origin")!=site||item.value("realm")!=realm)continue;
            if(!authenticator->user().isEmpty()&&item.value("username")!=authenticator->user())continue;
            existing=item;break;
        }
        if(!existing.isEmpty()){
            auto* job=new QKeychain::ReadPasswordJob("ZIMA-CAD",this);job->setAutoDelete(false);
            job->setKey("browser/credential/"+existing.value("id").toString());job->setInsecureFallback(false);
            QEventLoop loop;QTimer timeout;timeout.setSingleShot(true);
            connect(job,&QKeychain::Job::finished,&loop,&QEventLoop::quit);
            connect(&timeout,&QTimer::timeout,&loop,&QEventLoop::quit);timeout.start(3000);job->start();loop.exec();
            if(timeout.isActive()&&job->error()==QKeychain::NoError){dialog.user->setText(existing.value("username").toString());dialog.password->setText(job->textData());}
            if(timeout.isActive())job->deleteLater();else connect(job,&QKeychain::Job::finished,job,&QObject::deleteLater);
        }
        dialog.remember->setVisible(preferences_.save_passwords&&secure);
        if(dialog.exec()!=QDialog::Accepted)return;
        authenticator->setUser(dialog.user->text());authenticator->setPassword(dialog.password->text());
        if(!preferences_.save_passwords||!secure||!dialog.remember->isChecked())return;
        const auto username=dialog.user->text();
        existing.clear();for(const auto& item:accounts(directory_))
            if(item.value("type")=="http"&&item.value("origin")==site&&item.value("realm")==realm&&item.value("username")==username){existing=item;break;}
        const auto id=existing.isEmpty()?QUuid::createUuid().toString(QUuid::WithoutBraces):existing.value("id").toString();
        auto* job=new QKeychain::WritePasswordJob("ZIMA-CAD",this);job->setKey("browser/credential/"+id);job->setInsecureFallback(false);job->setTextData(dialog.password->text());
        connect(job,&QKeychain::Job::finished,this,[this,id,site,realm,username](QKeychain::Job* job){
            if(job->error()!=QKeychain::NoError){warn(storage_error(job->error()));return;}
            QSettings settings(metadata_path(directory_),QSettings::IniFormat);settings.beginGroup("Accounts/"+id);
            settings.setValue("type","http");settings.setValue("origin",site);settings.setValue("realm",realm);settings.setValue("username",username);
            settings.endGroup();settings.sync();if(settings.status()!=QSettings::NoError)warn(tr("Cannot save credential metadata."));
        });job->start();
    });
}
void Credentials::install_script(QWebEngineProfile* profile){
    QFile channel(":/qtwebchannel/qwebchannel.js"),form(":/zima/browser/password_manager.js");
    if(!channel.open(QIODevice::ReadOnly)||!form.open(QIODevice::ReadOnly))return;
    QWebEngineScript script;script.setName("zima_password_manager");script.setInjectionPoint(QWebEngineScript::DocumentReady);
    script.setRunsOnSubFrames(false);script.setWorldId(QWebEngineScript::ApplicationWorld);
    script.setSourceCode(QString::fromUtf8(channel.readAll())+'\n'+QString::fromUtf8(form.readAll()));profile->scripts()->insert(script);
}
void Credentials::show_manager(){auto* dialog=new ManagerDialog(directory_,owner_);dialog->setAttribute(Qt::WA_DeleteOnClose);dialog->show();}
} // namespace zima::browser
