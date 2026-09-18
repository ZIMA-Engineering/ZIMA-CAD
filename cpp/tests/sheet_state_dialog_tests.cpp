#include "../app/sheet_state_dialog.hpp"
#include "../app/sheet_state_selection.hpp"
#include <QApplication>
#include <QDialogButtonBox>
#include <iostream>
using namespace zima;
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main(int argc,char** argv) {
    QApplication application(argc,argv);QWidget parent;parent.resize(1000,700);parent.show();
    try {
        kernel::ViewerMesh input;input.vertices={{0,0,0},{10,0,0},{0,10,0},{20,0,0},{30,0,0},{20,10,0}};
        input.triangles={0,1,2,3,4,5};
        kernel::FaceReference a{"unbend","child-a"},b{"unbend","child-b"};a.sheet_owner="a";b.sheet_owner="b";
        input.triangle_references={a,b};app::SheetStateSelection regions(input);
        check(regions.resolve({viewer::CandidateKind::Container,0,0,"unbend"}).empty(),"A multi-region state was offered as one ambiguous source feature.");
        for(const auto& [point,expected]:{std::pair{kernel::Vec3{1,1,5},std::string{"a"}},std::pair{kernel::Vec3{21,1,5},std::string{"b"}}}) {
            const auto candidates=viewer::ordered_viewer_candidates(input,{},point,{0,0,-1},.01,true,false);
            auto selected=viewer::filter_candidates(candidates,{viewer::CandidateKind::Face},[&](const auto& candidate){return regions.resolve(candidate)==expected;});
            check(selected.size()==1&&selected.front().owner_id=="unbend","Common picker lost the derived feature's source material region.");
            check(viewer::matches_selection_filter(selected.front(),viewer::SelectionFilter::Faces)&&
                !viewer::matches_selection_filter(selected.front(),viewer::SelectionFilter::Axes),"Sheet state selection bypassed the user's filter.");
        }
        for(bool unfold:{true,false}) {
            document::HistoryContainer feature;feature.id="state";feature.name="State";
            feature.feature_kind=unfold?document::FeatureKind::Unbend:document::FeatureKind::BendBack;
            int commits=0;document::HistoryContainer committed;
            auto* dialog=new app::SheetStateDialog(feature,{{"a","A"},{"b","B"}},[&](auto value){++commits;committed=std::move(value);},&parent);
            dialog->show();application.processEvents();
            check(dialog->windowFlags().testFlag(Qt::SubWindow),"Sheet state did not use the internal properties presentation.");
            auto* all=dialog->findChild<QCheckBox*>("sheetStateAll");auto* table=dialog->findChild<QTableWidget*>("sheetStateElements");
            check(all&&table&&all->isChecked()&&!table->isEnabled(),"Initial all-elements selection is inconsistent.");
            all->setChecked(false);dialog->toggle("a");dialog->toggle("b");dialog->toggle("a");
            check(dialog->selected()==std::vector<std::string>{"b"}&&table->rowCount()==2,"Second click did not remove the exact selected element.");
            all->setChecked(true);check(dialog->selected().size()==2,"All did not select all eligible regions.");
            dialog->end_entry();check(dialog->highlighted().empty()&&dialog->selected().size()==2,"Ending all-region inspection erased selection or retained highlights.");
            all->setChecked(false);check(dialog->selected()==std::vector<std::string>{"b"},"All erased the manual selection.");
            auto* remove=table->cellWidget(0,0)->findChild<QPushButton*>();check(remove,"Remove control is not in the first cell.");remove->click();
            check(dialog->selected().empty()&&table->rowCount()==1,"Remove control retained its selection.");
            dialog->toggle("b");dialog->end_entry();
            check(!dialog->selecting()&&dialog->highlighted().empty()&&dialog->selected()==std::vector<std::string>{"b"},"Ending reference entry changed pending values or kept picking active.");
            dialog->toggle("a");check(dialog->selected()==std::vector<std::string>{"b"},"Inactive entry accepted a View pick.");
            table->cellWidget(0,0)->findChild<QPushButton*>()->click();
            all->setChecked(true);all->setChecked(false);
            dialog->toggle("a");dialog->buttons()->button(QDialogButtonBox::Ok)->click();
            check(commits==1&&!committed.sheet_state.all&&committed.sheet_state.owners==std::vector<std::string>{"a"},"OK lost the pending selection.");
            application.processEvents();
            auto* cancelled=new app::SheetStateDialog(feature,{{"a","A"}},[&](auto){++commits;},&parent);
            cancelled->show();cancelled->reject();application.processEvents();check(commits==1,"Cancel committed pending changes.");
        }
        std::cout<<"Sheet state properties dialog contract passed.\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
