        // FEM may use both the Part Origin and its source Body Origin. Exercise
        // the actual Tree picker, in particular XY as the second reference.
        {
            const auto cad_count=kernel::body_calculation_count.load();
            const auto original=session.document().body_history;
            const auto part_origin=part.document_id+":origin";
            const auto body_origin=a+":origin";
            const auto click_plane=[&](const std::string& owner,const char* plane) {
                QTreeWidgetItem* item{};
                for(QTreeWidgetItemIterator it(tree);*it;++it)
                    if((*it)->parent()&&(*it)->parent()->data(0,Qt::UserRole).toString().toStdString()==owner&&
                        (((*it)->data(0,Qt::UserRole+5).toString()==QString("origin:plane:")+plane)||
                         ((*it)->data(0,Qt::UserRole+5).toString()==QString("plane:")+plane)))item=*it;
                check(item,"FEM Origin plane is missing from the Tree");
                for(auto* parent=item->parent();parent;parent=parent->parent())parent->setExpanded(true);
                tree->scrollToItem(item);flush();const QPointF at=tree->visualItemRect(item).center();
                for(auto type:{QEvent::MouseButtonPress,QEvent::MouseButtonRelease}) {
                    QMouseEvent event(type,at,QPointF(tree->viewport()->mapToGlobal(at.toPoint())),Qt::LeftButton,
                        type==QEvent::MouseButtonPress?Qt::LeftButton:Qt::NoButton,Qt::NoModifier);
                    QApplication::sendEvent(tree->viewport(),&event);flush();
                }
            };
            for(auto kind:{document::FemConditionKind::Constraint,document::FemConditionKind::Force})
            for(const auto& second_origin:{part_origin,body_origin}) {
                window.show_fem_condition_properties(kind);flush();
                auto* condition=dynamic_cast<FemConditionDialog*>(window.properties_dialog_);
                check(condition&&window.pending_primitive_reference_index_==0,"FEM did not start placement entry");
                click_plane(body_origin,"xz");
                check(window.pending_primitive_reference_index_==1,"First FEM reference did not advance entry");
                click_plane(second_origin,"xy");
                check(condition->first_empty_position_index()==2,"FEM rejected second Origin XY reference");
                click_plane(body_origin,"yz");
                const auto value=condition->pending_value();
                check(value.placement.reference_valid&&value.placement.references[1].owner_id==second_origin&&
                    value.placement.references[1].semantic_key=="origin:plane:xy","FEM changed second Origin identity");
                condition->buttons()->button(QDialogButtonBox::Ok)->click();flush();
                check(!window.properties_dialog_,"Placed FEM condition did not commit");
                const auto* saved=workspace::find_fem_condition(session.document(),fem,value.id);
                check(saved&&saved->placement.reference_valid&&saved->placement.references==value.placement.references,
                    "FEM Origin references did not persist");
                const auto saved_value=*saved;
                window.show_fem_condition_properties(kind,value.id);flush();
                condition=dynamic_cast<FemConditionDialog*>(window.properties_dialog_);
                check(condition&&condition->pending_value().placement.references==saved_value.placement.references,
                    "FEM edit lost Origin references");
                window.start_primitive_reference_selection(1);flush();
                click_plane(second_origin==part_origin?body_origin:part_origin,"xy");
                check(condition->pending_value().placement.references[1].owner_id!=second_origin,
                    "FEM second Origin reference could not be replaced");
                condition->reject();flush();
                check(*workspace::find_fem_condition(session.document(),fem,value.id)==saved_value,"FEM Cancel changed placement");
                const auto native=directory/"fem-origin-references.prtz";
                session.document().save(native,session.calculated_boundaries());
                const auto reopened=document::PartDocument::load(native);
                check(*workspace::find_fem_condition(reopened,fem,value.id)==saved_value,"FEM Origin native reopen changed placement");
                check(session.undo()&&!workspace::find_fem_condition(session.document(),fem,value.id),"FEM placement Undo failed");
                check(session.redo()&&*workspace::find_fem_condition(session.document(),fem,value.id)==saved_value,"FEM placement Redo failed");
                check(session.undo(),"FEM placement fixture cleanup failed");
                window.refresh_scene();flush();
            }
            check(session.document().body_history==original&&kernel::body_calculation_count.load()==cad_count,
                "FEM reference entry calculated CAD or changed the restored fixture");
            check(!window.placement_origin_allowed(part_origin),"FEM exception leaked outside its condition dialog");
            std::cout<<"FEM Origin GUI: second Part/source XY reference, replacement, Cancel, OK, Undo/Redo and native reopen passed\n";
            if(qEnvironmentVariableIsSet("ZIMA_VERIFY_FEM_ORIGIN_ONLY"))return 0;
        }
