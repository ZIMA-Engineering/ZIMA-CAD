// DrawingCanvas balloon interaction, sharing its annotation and reference lists.
public:
    void set_balloon_properties(std::function<void(const std::string&)> callback){balloon_properties_=std::move(callback);}
    void set_balloon_command(DrawingBalloonDialog* dialog){
        balloon_command_=dialog;selected_.clear();selected_field_.clear();selected_dimension_id_.clear();
        selected_annotation_.reset();hovered_annotation_.reset();offered_annotations_.clear();measurement_offered_.clear();balloon_views_.clear();
        if(dialog)dialog->set_changed([this]{
            if(!balloon_command_)return;
            const auto id=balloon_command_->selected_id();
            selected_annotation_=id.empty()?std::optional<AnnotationKey>{}:AnnotationKey{AnnotationKind::Balloon,balloon_command_->view_id(),id,0};
            selected_=balloon_command_->inspecting_view()?balloon_command_->view_id():std::string{};
            offer_measurements(measurement_pointer_);update();
        });
        update();
    }
    std::optional<QPointF> balloon_handle(const std::string& id,int end)const{
        for(const auto& h:annotation_handles_)if(h.key.kind==AnnotationKind::Balloon&&h.key.id==id&&h.key.end==end)return h.point;return {};
    }
private:
    QPointer<DrawingBalloonDialog> balloon_command_;
    std::function<void(const std::string&)> balloon_properties_;
    std::optional<drawing::DrawingBalloon> balloon_drag_original_;
    QPointF balloon_drag_start_;
    int balloon_drag_end_{};
    bool balloon_drag_valid_{};
    std::vector<std::string> balloon_views_;
    std::size_t balloon_view_index_{};
    const std::vector<drawing::DrawingBalloon>* pending_balloons()const override {return balloon_command_?&balloon_command_->staged_sheet().balloons:nullptr;}
    drawing::DrawingBalloon* editable_balloon(const std::string& id){
        if(balloon_command_)return balloon_command_->balloon(id);
        if(!sheet_)return nullptr;auto i=std::ranges::find(sheet_->balloons,id,&drawing::DrawingBalloon::id);return i==sheet_->balloons.end()?nullptr:&*i;
    }
    std::string balloon_reference_view()const{
        if(balloon_drag_original_&&balloon_drag_end_==1)return balloon_drag_original_->view_id;
        return balloon_command_&&balloon_command_->entering()?balloon_command_->view_id():std::string{};
    }
    void offer_balloon_views(QPointF point){
        std::vector<std::string> next;
        for(auto it=sheet_->views.rbegin();it!=sheet_->views.rend();++it)if(view_bounds(*it).contains(point))next.push_back(it->id);
        if(next!=balloon_views_)balloon_view_index_=0;balloon_views_=std::move(next);
        hovered_=balloon_views_.empty()?std::string{}:balloon_views_[balloon_view_index_];
    }
    void begin_balloon_drag(const AnnotationHandle& h,QPointF at){
        if(auto* b=editable_balloon(h.key.id)){balloon_drag_original_=*b;balloon_drag_start_=at;balloon_drag_end_=h.key.end;balloon_drag_valid_=h.key.end==0;}
    }
    bool balloon_press(QMouseEvent* event){
        if(!balloon_command_)return false;
        if(balloon_command_->choosing_view()) {
            if(!balloon_views_.empty())balloon_command_->select_view(balloon_views_[balloon_view_index_]);
        }else if(balloon_command_->entering()) {
            if(!measurement_offered_.empty()){const auto candidate=measurement_offered_[measurement_index_];balloon_command_->accept_candidate(candidate.view,candidate.candidate);}
        }else if(balloon_command_->placing()) {
            for(const auto& v:sheet_->views)if(v.id==balloon_command_->view_id()){auto p=measurement_point(v,event->position());balloon_command_->position({p.x*v.scale,p.y*v.scale},true);}
        }else {
            offer_annotations(event->position());
            if(!offered_annotations_.empty()) {
                const auto c=offered_annotations_[offered_annotation_index_];
                if(c.key.kind==AnnotationKind::Balloon){balloon_command_->select_balloon(c.key.id);if(QLineF(c.point,event->position()).length()<=8)begin_balloon_drag(c,event->position());}
            }else balloon_command_->select_balloon({});
        }
        update();event->accept();return true;
    }
    bool balloon_move(QMouseEvent* event){
        if(view_panning_||(event->buttons()&Qt::MiddleButton))return false;
        if(balloon_drag_original_&&(event->buttons()&Qt::LeftButton)) {
            auto* b=editable_balloon(balloon_drag_original_->id);if(!b)return true;
            if(balloon_drag_end_==0) {
                const auto delta=(event->position()-balloon_drag_start_)/canvas_zoom();
                b->position={balloon_drag_original_->position.x+delta.x(),balloon_drag_original_->position.y-delta.y()};
            }else {
                offer_measurements(event->position());balloon_drag_valid_=!measurement_offered_.empty();
                if(balloon_drag_valid_){b->attachment=measurement_offered_[measurement_index_].candidate.attachment;drawing::refresh_balloon(*sheet_,*b);}
                else *b=*balloon_drag_original_;
            }
            if(balloon_command_)balloon_command_->changed();update();event->accept();return true;
        }
        if(!balloon_command_)return false;
        if(balloon_command_->choosing_view())offer_balloon_views(event->position());
        else if(balloon_command_->entering())offer_measurements(event->position());
        else if(balloon_command_->placing()) {
            for(const auto& v:sheet_->views)if(v.id==balloon_command_->view_id()){auto p=measurement_point(v,event->position());balloon_command_->position({p.x*v.scale,p.y*v.scale},false);}
        }else offer_annotations(event->position());
        update();event->accept();return true;
    }
    bool balloon_release(QMouseEvent* event){
        if(!balloon_drag_original_||event->button()!=Qt::LeftButton)return false;
        auto* b=editable_balloon(balloon_drag_original_->id);
        if(b&&balloon_drag_end_==1&&!measurement_offered_.empty()) {
            b->attachment=measurement_offered_[measurement_index_].candidate.attachment;
            drawing::refresh_balloon(*sheet_,*b);balloon_drag_valid_=true;
        }
        if(b&&!balloon_drag_valid_)*b=*balloon_drag_original_;
        const bool changed=b&&*b!=*balloon_drag_original_;
        balloon_drag_original_.reset();measurement_offered_.clear();
        if(balloon_command_)balloon_command_->changed();else if(changed&&changed_)changed_();
        update();event->accept();return true;
    }
    bool balloon_escape(){
        if(balloon_drag_original_){if(auto* b=editable_balloon(balloon_drag_original_->id))*b=*balloon_drag_original_;balloon_drag_original_.reset();measurement_offered_.clear();if(balloon_command_)balloon_command_->changed();update();return true;}
        if(!balloon_command_)return false;
        if(balloon_command_->entering()||balloon_command_->choosing_view())balloon_command_->end_entry();else balloon_command_->reject();return true;
    }
    void erase_balloon(const std::string& id){
        if(!sheet_)return;workspace::erase_drawing_balloon(*sheet_,id);selected_annotation_.reset();hovered_annotation_.reset();offered_annotations_.clear();
        if(changed_)changed_();if(selection_changed_)selection_changed_();update();
    }
    bool balloon_context(QContextMenuEvent* event){
        if(balloon_command_||balloon_drag_original_) {
            if(!balloon_views_.empty()&&balloon_command_&&balloon_command_->choosing_view()){
                balloon_view_index_=(balloon_view_index_+1)%balloon_views_.size();hovered_=balloon_views_[balloon_view_index_];
            }else if(!measurement_offered_.empty()){
                measurement_index_=(measurement_index_+1)%measurement_offered_.size();
                if(balloon_drag_original_&&balloon_drag_end_==1)if(auto* b=editable_balloon(balloon_drag_original_->id)) {
                    b->attachment=measurement_offered_[measurement_index_].candidate.attachment;drawing::refresh_balloon(*sheet_,*b);
                }
            }
            else if(balloon_command_&&!balloon_command_->placing()){offer_annotations(event->pos());if(!offered_annotations_.empty()){offered_annotation_index_=(offered_annotation_index_+1)%offered_annotations_.size();hovered_annotation_=offered_annotations_[offered_annotation_index_].key;}}
            update();event->accept();return true;
        }
        if(!selected_annotation_||selected_annotation_->kind!=AnnotationKind::Balloon)return false;
        offer_annotations(event->pos());
        const auto id=selected_annotation_->id;
        if(std::ranges::none_of(offered_annotations_,[&](const auto& h){return h.key.kind==AnnotationKind::Balloon&&h.key.id==id;}))return false;
        auto* menu=new QMenu(this);menu->setAttribute(Qt::WA_DeleteOnClose);
        auto* properties=menu->addAction(tr("Vlastnosti pozice…"),this,[this,id]{if(balloon_properties_)balloon_properties_(id);});properties->setObjectName("drawingBalloonPropertiesAction");
        menu->addAction(tr("Skrýt"),this,[this,id]{if(auto* b=editable_balloon(id))b->visible=false;selected_annotation_.reset();if(changed_)changed_();update();});
        menu->addAction(tr("Odstranit"),this,[this,id]{erase_balloon(id);});menu->popup(event->globalPos());event->accept();return true;
    }
