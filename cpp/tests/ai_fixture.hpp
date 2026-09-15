// Local protocol fixture. No AI service, account, credentials or network access.
#pragma once
#include "aiprovider.h"
#include "aitools.h"
#include <QJsonDocument>
#include <QTextStream>
#include <QCoreApplication>
#include <QFile>
#include <cstdio>
class FakeCadAi final : public AiProvider {
public:
    bool running=false; int asks=0, connections=0; QJsonObject context,result; QString prompt;
    bool ready() const override{return true;}
    bool connected() const override{return true;}
    bool busy() const override{return running;}
    QString status() const override{return "Fixture";}
    QJsonArray models() const override{return {QJsonObject{{"model","fixture"},{"displayName","Fixture"}}};}
    void connectAccount(const QString&) override{++connections;}
    void login() override{}
    void logout() override{}
    void ask(const QString& text,const QJsonObject& value,const QString&) override{prompt=text;context=value;running=true;++asks;emit changed();}
    void cancel() override{running=false;emit changed();emit cancelled();}
    void newConversation() override{}
    void toolResult(const QString&,const QJsonObject& value,bool) override{result=value;}
    void done(){running=false;emit changed();emit answer({});}
};
inline int runCadAiFixture() {
    QTextStream input(stdin),output(stdout);
    const auto write=[&](QJsonObject value){output<<QJsonDocument(value).toJson(QJsonDocument::Compact)<<'\n';output.flush();};
    QString thread,turn;int serial=0;bool logged_in=true;
    while(!input.atEnd()) {
        const auto message=QJsonDocument::fromJson(input.readLine().toUtf8()).object();
        const auto method=message["method"].toString();const auto params=message["params"].toObject();
        const auto reply=[&](QJsonObject value){write({{"id",message["id"]},{"result",value}});};
        if(method=="initialize") {
            if(!params["capabilities"].toObject()["experimentalApi"].toBool())return 20;
            const auto args=QCoreApplication::arguments();
            if(!args.contains("features.shell_tool=false") || !args.contains("forced_login_method=\"chatgpt\""))return 21;
            reply({{"userAgent","cad-fixture"}});
        } else if(method=="account/read")reply({{"account",logged_in?QJsonValue(QJsonObject{{"type","chatgpt"}}):QJsonValue(QJsonValue::Null)}});
        else if(method=="model/list")reply({{"data",QJsonArray{QJsonObject{{"model","fixture"},{"displayName","Fixture"}}}}});
        else if(method=="thread/start") {
            if(params["sandbox"]!="read-only" || params["approvalPolicy"]!="never" || !params["ephemeral"].toBool()
                || params["dynamicTools"].toArray()!=CadAi::toolDefinitions() || params["developerInstructions"]!=CadAi::hostInstructions()
                || params["baseInstructions"]!=CadAi::instructions())return 22;
            // Export exact protocol data for validation against the installed CLI's schema.
            if(qEnvironmentVariableIsSet("ZIMA_AI_SCHEMA_EXPORT")){
                QFile file(qEnvironmentVariable("ZIMA_AI_SCHEMA_EXPORT"));if(!file.open(QIODevice::WriteOnly))return 23;
                file.write(QJsonDocument(params).toJson());
            }
            thread=QString("thread-%1").arg(++serial);reply({{"thread",QJsonObject{{"id",thread}}}});
        } else if(method=="turn/start") {
            turn=QString("turn-%1").arg(++serial);reply({{"turn",QJsonObject{{"id",turn}}}});
            const auto text=params["input"].toArray().first().toObject()["text"].toString();
            if(text.contains("INVALID_JSON")){output<<"not json\n";output.flush();continue;}
            if(text.contains("WAIT_FOREVER"))continue;
            if(text.contains("NATIVE_TOOL")){
                write({{"id","native"},{"method","item/commandExecution/requestApproval"},{"params",QJsonObject{}}});continue;
            }
            write({{"id","tool"},{"method","item/tool/call"},{"params",QJsonObject{{"threadId",thread},{"turnId",turn},
                {"callId","call"},{"tool","cad_context"},{"arguments",QJsonObject{}}}}});
        } else if(method=="thread/unsubscribe")reply({});
        else if(method=="account/logout"){logged_in=false;reply({});}
        else if(method=="account/login/start") {
            reply({{"authUrl","https://auth.openai.com/fixture"},{"loginId","login-fixture"}});
            logged_in=true;write({{"method","account/login/completed"},{"params",QJsonObject{{"loginId","login-fixture"},{"success",true}}}});
        } else if(method.isEmpty() && message["id"]=="tool") {
            if(!message["result"].toObject()["success"].toBool())return 24;
            write({{"method","item/completed"},{"params",QJsonObject{{"threadId",thread},{"turnId",turn},
                {"item",QJsonObject{{"type","agentMessage"},{"phase","final_answer"},{"text",thread}}}}}});
            write({{"method","turn/completed"},{"params",QJsonObject{{"threadId",thread},{"turn",QJsonObject{{"id",turn},{"status","completed"}}}}}});
        }
    }
    return 0;
}
