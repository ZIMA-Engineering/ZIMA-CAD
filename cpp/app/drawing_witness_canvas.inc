// Witness-only interaction consumes the renderer's exact displayed geometry.
    std::optional<drawing::WitnessEditKind> witness_tool_;
    std::string witness_owner_;
    std::vector<WitnessHandle> witness_offered_;
    std::size_t witness_offer_index_{};
    std::optional<WitnessHandle> witness_active_;
    std::optional<drawing::DrawingDimension> witness_draft_;
    std::size_t witness_edit_index_{};
    int witness_drag_end_{-1};
    bool witness_first_ready_{};
    QPointF witness_middle_origin_;
    std::optional<std::pair<WitnessHandle,drawing_render::WitnessGeometry::Grip>> witness_grip_offer_;
    void offer_witness_grip(QPointF point) {
        witness_grip_offer_.reset();double best=7;
        for(const auto& handle:witness_handles_)if(entity_selected(handle.key))for(const auto& grip:handle.geometry.grips) {
            const auto distance=QLineF(grip.point,point).length();
            if(distance<=best){best=distance;witness_grip_offer_=std::make_pair(handle,grip);}
        }
    }
    void start_witness_tool(drawing::WitnessEditKind kind,const std::string& id={}) {
        cancel_witness();witness_tool_=kind;witness_owner_=id;setFocus();update();
    }
    void cancel_witness() {
        witness_tool_.reset();witness_owner_.clear();witness_offered_.clear();witness_offer_index_=0;
        witness_active_.reset();witness_draft_.reset();witness_drag_end_=-1;witness_first_ready_=false;update();
        witness_grip_offer_.reset();
    }
    void offer_witness(QPointF point) {
        std::vector<std::pair<double,WitnessHandle>> offered;
        for(const auto& handle:witness_handles_) {
            if(!witness_owner_.empty()&&handle.key.id!=witness_owner_)continue;
            if(dimension_command_&&handle.key.id!=dimension_command_->value().id)continue;
            double distance=1e100;
            for(const auto& curve:handle.geometry.curves)for(int i=1;i<curve.size();++i) {
                const auto d=curve[i]-curve[i-1];const auto n=viewer::dimension_screen_dot(d,d);
                const auto t=n>1e-12?std::clamp(viewer::dimension_screen_dot(point-curve[i-1],d)/n,0.,1.):0.;
                distance=std::min(distance,QLineF(point,curve[i-1]+d*t).length());
            }
            if(distance<=7)offered.push_back({distance,handle});
        }
        std::stable_sort(offered.begin(),offered.end(),[](const auto& a,const auto& b){return a.first<b.first;});
        std::vector<WitnessHandle> next;for(const auto& item:offered)next.push_back(item.second);
        bool same=next.size()==witness_offered_.size();
        for(std::size_t i=0;same&&i<next.size();++i)same=next[i].key==witness_offered_[i].key&&next[i].geometry.side==witness_offered_[i].geometry.side;
        if(!same)witness_offer_index_=0;witness_offered_=std::move(next);
    }
    double witness_fraction(QPointF point) const {
        const auto& line=witness_active_->geometry;const auto d=line.last-line.first;
        return viewer::dimension_screen_dot(point-line.first,d)/viewer::dimension_screen_dot(d,d);
    }
    void move_witness(QPointF point) {
        if(!witness_draft_||!witness_active_)return;
        for(const auto& handle:witness_handles_)if(handle.key.id==witness_active_->key.id&&handle.segment==witness_active_->segment&&handle.geometry.side==witness_active_->geometry.side){witness_active_->geometry=handle.geometry;break;}
        auto& edits=witness_draft_->segments[witness_active_->segment].witness_edits;
        auto& edit=edits[witness_edit_index_];double low=.001,high=.999;
        for(std::size_t i=0;i<edits.size();++i)if(i!=witness_edit_index_&&edits[i].side==edit.side) {
            if(edits[i].last<=edit.first)low=std::max(low,edits[i].last+.001);
            if(edits[i].first>=edit.last)high=std::min(high,edits[i].first-.001);
        }
        const auto t=witness_fraction(point);
        if(witness_drag_end_==0){if(edit.last-low<.0001)return;edit.first=std::clamp(t,low,edit.last-.0001);}
        else {
            if(high-edit.first<.0001)return;
            edit.last=std::clamp(t,edit.first+.0001,high);
            if(edit.kind==drawing::WitnessEditKind::Jog) {
                const auto& line=witness_active_->geometry;
                const auto u=viewer::dimension_screen_unit(line.last-line.first);const QPointF normal(-u.y(),u.x());
                double previous=0;for(const auto& other:edits)if(other.side==edit.side&&other.kind==drawing::WitnessEditKind::Jog&&other.last<=edit.first)previous+=other.offset;
                edit.offset=viewer::dimension_screen_dot(point-line.first,normal)/canvas_zoom()-previous;
            }
        }
        update();
    }
    void commit_witness() {
        if(!witness_draft_)return;
        drawing::validate_drawing_dimension(*witness_draft_);
        const auto value=*witness_draft_;
        if(dimension_command_)dimension_command_->change_presentation(value);
        else if(sheet_) {
            const auto it=std::ranges::find(sheet_->dimensions,value.id,&drawing::DrawingDimension::id);
            if(it!=sheet_->dimensions.end()){*it=value;workspace::synchronize_dimension_chain(*sheet_,value);if(changed_)changed_();}
        }
        cancel_witness();
    }
    bool witness_press(QMouseEvent* event) {
        if(event->button()==Qt::MiddleButton)witness_middle_origin_=event->position();
        if((event->buttons()&Qt::MiddleButton)&&(event->buttons()&Qt::RightButton))return false;
        if(event->button()!=Qt::LeftButton)return event->button()!=Qt::MiddleButton&&witness_tool_.has_value();
        if(witness_tool_) {
            if(witness_owner_.empty()&&*witness_tool_==drawing::WitnessEditKind::Break) {
                if(!offered_annotations_.empty()){const auto key=offered_annotations_[offered_annotation_index_].key;witness_owner_=key.id;select_entity(key,false);hovered_annotation_.reset();update();}return true;
            }
            if(witness_first_ready_){move_witness(event->position());commit_witness();return true;}
            if(witness_offered_.empty())return true;
            const auto offered=witness_offered_[witness_offer_index_];
            const auto* value=dimension_command_?&dimension_command_->value():visible_dimension(offered.key.id);
            if(!value)return true;
            witness_active_=offered;witness_owner_=value->id;witness_draft_=*value;
            auto& edits=witness_draft_->segments[offered.segment].witness_edits;
            const double first=std::clamp(witness_fraction(event->position()),.001,.998);
            double high=.999;
            for(const auto& edit:edits)if(edit.side==offered.geometry.side) {
                if(first>=edit.first&&first<=edit.last){witness_draft_.reset();witness_active_.reset();return true;}
                if(edit.first>first)high=std::min(high,edit.first-.001);
            }
            if(high-first<.0002){witness_draft_.reset();witness_active_.reset();return true;}
            witness_edit_index_=edits.size();edits.push_back({kernel::make_stable_id(),*witness_tool_,offered.geometry.side,first,std::min(first+.05,high),0});
            witness_first_ready_=true;select_entity(offered.key,false);update();return true;
        }
        if(!ordinary_selection())return false;
        if(witness_grip_offer_) {
                const auto& [handle,grip]=*witness_grip_offer_;
                const auto* value=visible_dimension(handle.key.id);if(!value)return false;
                witness_active_=handle;witness_draft_=*value;witness_drag_end_=grip.end;
                const auto& edits=value->segments[handle.segment].witness_edits;
                witness_edit_index_=std::ranges::find(edits,grip.id,&drawing::WitnessEdit::id)-edits.begin();
                update();return true;
        }
        return false;
    }
    void paint_witness(QPainter& painter) {
        painter.save();
        if(witness_tool_&&!witness_first_ready_&&!witness_offered_.empty()) {
            painter.setPen(QPen(QColor(255,140,12),2));
            for(const auto& curve:witness_offered_[witness_offer_index_].geometry.curves)painter.drawPolyline(curve);
        }
        painter.setPen(QPen(QColor("#E040FB"),1));painter.setBrush(QColor("#E040FB"));
        for(const auto& handle:witness_handles_)if(entity_selected(handle.key)||
            (dimension_command_&&dimension_command_->value().id==handle.key.id))
            for(const auto& grip:handle.geometry.grips)painter.drawEllipse(grip.point,4,4);
        if(dimension_command_&&!dimension_command_->inspected_witness().empty()) {
            painter.setPen(QPen(QColor("#4DD811"),2));
            for(const auto& handle:witness_handles_)if(handle.key.id==dimension_command_->value().id)
                for(const auto& grip:handle.geometry.grips)if(grip.id==dimension_command_->inspected_witness())painter.drawEllipse(grip.point,6,6);
        }
        painter.restore();
        if(witness_grip_offer_&&!witness_tool_){painter.save();painter.setPen(QPen(QColor(255,140,12),2));painter.setBrush(QColor(255,140,12));painter.drawEllipse(witness_grip_offer_->second.point,4,4);painter.restore();}
    }
