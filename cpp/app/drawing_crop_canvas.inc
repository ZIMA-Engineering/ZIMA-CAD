// Inline interaction owned by DrawingCanvas. All geometry is stored projection data.
    std::optional<drawing::DrawingView> crop_edit_;
    drawing::ViewCrop crop_value_;
    std::function<void(std::optional<drawing::ViewCrop>,bool)> crop_finished_;
    bool crop_anchor_ready_{},crop_modifying_{};
    int crop_grip_{-1};QPointF crop_pointer_;drawing::Point2 crop_drag_start_;
    drawing::ViewCrop crop_drag_initial_;
    std::optional<drawing::Point2> crop_offer_;QString crop_error_;
    drawing::Point2 crop_point(QPointF p) const {
        const auto o=raw_view_origin(*crop_edit_);const auto scale=canvas_zoom()*crop_edit_->scale;
        return {(p.x()-o.x())/scale,(o.y()-p.y())/scale};
    }
    QPointF crop_screen(drawing::Point2 p) const {
        return raw_view_origin(*crop_edit_)+QPointF(p.x,-p.y)*canvas_zoom()*crop_edit_->scale;
    }
    void offer_crop_anchor(QPointF point) {
        crop_offer_.reset();drawing::MeasurementPickRequest request;request.mode=int(drawing::DimensionAttachmentKind::CurvePoint);
        const auto candidates=drawing::measurement_candidates(*crop_edit_,measurement_point(*crop_edit_,point),8/(canvas_zoom()*crop_edit_->scale),request);
        if(!candidates.empty())crop_offer_=drawing::break_map(*crop_edit_,candidates.front().position);
    }
    void finish_crop(bool accepted) {
        if(!crop_edit_)return;
        if(accepted){if(!crop_anchor_ready_)return;auto check=*crop_edit_;check.crop=crop_value_;try{drawing::validate_view_crop(check);}catch(...){crop_error_=tr("Invalid drawing crop boundary.");update();return;}}
        const auto crop=accepted?std::optional(crop_value_):std::nullopt;crop_edit_.reset();crop_offer_.reset();crop_grip_=-1;unsetCursor();
        auto callback=std::move(crop_finished_);if(callback)callback(crop,accepted);update();
    }
public:
    void begin_crop(drawing::DrawingView view,int mode,std::function<void(std::optional<drawing::ViewCrop>,bool)> finished,std::optional<drawing::Point2> anchor={}) {
        select_entity({},false);selected_.clear();selected_annotation_.reset();
        crop_error_.clear();crop_modifying_=mode==3;crop_anchor_ready_=crop_modifying_;
        crop_value_=crop_modifying_?view.crop.value_or(drawing::ViewCrop{}):drawing::ViewCrop{};
        if(!crop_modifying_)crop_value_.shape=static_cast<drawing::ViewCropShape>(mode);
        if(anchor&&!crop_modifying_){crop_value_.anchor=*anchor;crop_anchor_ready_=true;}
        view.crop.reset();crop_edit_=view;preview_=view;crop_finished_=std::move(finished);crop_grip_=-1;crop_offer_.reset();
        setFocus();setCursor(Qt::CrossCursor);update();
    }
private:
    std::vector<drawing::Point2> crop_grips() const {
        auto result=crop_value_.points;
        if(crop_value_.shape!=drawing::ViewCropShape::Spline){result.clear();if(!crop_value_.points.empty()){const auto r=crop_value_.points.front();result={{crop_value_.anchor.x+r.x,crop_value_.anchor.y},{crop_value_.anchor.x,crop_value_.anchor.y+r.y}};}}
        result.insert(result.begin(),crop_value_.anchor);return result;
    }
    bool crop_press(QMouseEvent* event) {
        if(!crop_edit_)return false;event->accept();if(event->button()!=Qt::LeftButton)return true;
        crop_pointer_=event->position();
        if(!crop_anchor_ready_){offer_crop_anchor(event->position());if(crop_offer_){crop_value_.anchor=*crop_offer_;crop_anchor_ready_=true;crop_offer_.reset();}update();return true;}
        const auto p=crop_point(event->position());
        if(crop_modifying_){const auto grips=crop_grips();for(int i=0;i<int(grips.size());++i)if(QLineF(crop_screen(grips[i]),event->position()).length()<=8){crop_grip_=i;crop_drag_initial_=crop_value_;crop_drag_start_=p;break;}return true;}
        if(crop_value_.shape==drawing::ViewCropShape::Spline){
            if(crop_value_.points.size()>=3&&QLineF(crop_screen(crop_value_.points.front()),event->position()).length()<8){finish_crop(true);return true;}
            if(crop_value_.points.size()<256&&(crop_value_.points.empty()||QLineF(crop_screen(crop_value_.points.back()),event->position()).length()>2))crop_value_.points.push_back(p);
        }else{update_crop_radius(p);finish_crop(true);}update();return true;
    }
    void update_crop_radius(drawing::Point2 p) {
        double x=std::abs(p.x-crop_value_.anchor.x),y=std::abs(p.y-crop_value_.anchor.y);
        if(crop_value_.shape==drawing::ViewCropShape::Circle)x=y=std::hypot(x,y);
        crop_value_.points={{x,y}};
    }
    bool crop_move(QMouseEvent* event) {
        if(!crop_edit_)return false;crop_error_.clear();crop_pointer_=event->position();
        if(!crop_anchor_ready_)offer_crop_anchor(event->position());
        else if(crop_modifying_&&crop_grip_>=0&&(event->buttons()&Qt::LeftButton)){
            const auto p=crop_point(event->position());crop_value_=crop_drag_initial_;
            if(crop_grip_==0){const drawing::Point2 delta{p.x-crop_drag_start_.x,p.y-crop_drag_start_.y};crop_value_.anchor.x+=delta.x;crop_value_.anchor.y+=delta.y;if(crop_value_.shape==drawing::ViewCropShape::Spline)for(auto& q:crop_value_.points){q.x+=delta.x;q.y+=delta.y;}}
            else if(crop_value_.shape==drawing::ViewCropShape::Spline)crop_value_.points[crop_grip_-1]=p;
            else {auto& radius=crop_value_.points.front();if(crop_grip_==1)radius.x=std::max(1e-5,std::abs(p.x-crop_value_.anchor.x));else radius.y=std::max(1e-5,std::abs(p.y-crop_value_.anchor.y));if(crop_value_.shape==drawing::ViewCropShape::Circle){if(crop_grip_==1)radius.y=radius.x;else radius.x=radius.y;}}
        }else if(!crop_modifying_&&crop_value_.shape!=drawing::ViewCropShape::Spline&&crop_anchor_ready_)update_crop_radius(crop_point(event->position()));
        update();event->accept();return true;
    }
    void paint_crop(QPainter& painter) {
        if(!crop_edit_)return;
        painter.save();painter.setPen(QPen(QColor("#00D1FF"),2));painter.setBrush(Qt::NoBrush);
        if(crop_anchor_ready_){auto shown=*crop_edit_;shown.crop=crop_value_;painter.drawPath(drawing_render::crop_screen_path(shown,raw_view_origin(shown),canvas_zoom()*shown.scale));
            for(auto p:crop_grips()){painter.setBrush(QColor("#E040FB"));painter.drawEllipse(crop_screen(p),4,4);}
        }else if(crop_offer_){painter.setBrush(QColor("#00D1FF"));painter.drawEllipse(crop_screen(*crop_offer_),4,4);}
        const auto instruction=!crop_error_.isEmpty()?crop_error_:!crop_anchor_ready_?tr("Vyberte kotevní bod na geometrii pohledu."):crop_modifying_?tr("Přesuňte body hranice. Enter potvrdí, Esc zruší."):crop_value_.shape==drawing::ViewCropShape::Spline?tr("Zadejte body spline. Kliknutím na první bod nebo Enter hranici uzavřete."):tr("Kliknutím určete velikost hranice. Esc zruší.");
        const auto area=QFontMetricsF(font()).boundingRect(instruction).adjusted(-8,-6,8,6);painter.fillRect(QRectF(QPointF(10,10),area.size()),QColor("#171A1D"));painter.drawText(QPointF(18,28),instruction);painter.restore();
    }
