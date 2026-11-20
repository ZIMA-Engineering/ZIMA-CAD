void verify_cursor_orbit_view(QApplication& application,AssemblyWorkspaceWindow& window) {
    using namespace zima;
    const auto check=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
    auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QWidget*>("modelWorkspace"));
    auto* action=window.findChild<QAction*>("cursorOrbitAction");
    auto* fit=window.findChild<QAction*>("fitViewAction");
    check(view&&action&&fit&&!action->isChecked(),"Cursor orbit must start disabled");
    bool beside_fit=false;
    for(auto* bar:window.findChildren<QToolBar*>()) {
        const auto actions=bar->actions();const auto index=actions.indexOf(fit);
        beside_fit|=index>=0&&index+1<actions.size()&&actions[index+1]==action;
    }
    check(beside_fit&&!action->icon().isNull(),"Cursor orbit button must follow Restore View");
    const auto original_mesh=view->mesh();const auto original_camera=view->camera_state();
    const auto original_projection=view->projection_mode();
    kernel::ViewerMesh mesh;mesh.vertices={{-10,-10,-10},{10,10,10}};
    kernel::ViewerEdge edge;edge.points={{-10,-10,-10},{10,10,10}};mesh.edges.push_back(edge);
    view->set_mesh(mesh);view->fit_all();application.processEvents();
    const auto mouse=[&](QEvent::Type type,QPointF at,Qt::MouseButton button,Qt::MouseButtons buttons) {
        QMouseEvent event(type,at,QPointF(view->mapToGlobal(at.toPoint())),button,buttons,Qt::NoModifier);
        QApplication::sendEvent(view,&event);application.processEvents();
    };
    const auto rotation=[](const auto& c){return QQuaternion(c[0],c[1],c[2],c[3]);};
    const auto center=[](const auto& c){return QVector3D(c[8],c[9],c[10]);};
    const auto pivot_at=[&](QPointF at) {
        const auto ray=view->ray_at(at);check(ray.has_value(),"Orbit pivot ray missing");
        const QVector3D origin(ray->first.x,ray->first.y,ray->first.z),direction(ray->second.x,ray->second.y,ray->second.z);
        const auto camera=view->camera_state();const auto normal=rotation(camera).inverted().rotatedVector(QVector3D(0,0,1));
        return origin+direction*(QVector3D::dotProduct(center(camera)-origin,normal)/QVector3D::dotProduct(direction,normal));
    };
    for(const auto projection:{viewer::ProjectionMode::Orthographic,viewer::ProjectionMode::Perspective}) {
        view->set_projection_mode(projection);view->set_camera_state({1,0,0,0,20,12,-7,20,3,1,-2});
        action->setChecked(true);check(view->cursor_orbit_enabled(),"Orbit action did not enable View navigation");
        for(const auto press:{QPointF(view->width()*.62,view->height()*.38),QPointF(view->width()*.29,view->height()*.68)}) {
            const auto pivot=pivot_at(press);const auto before=view->camera_state();
            mouse(QEvent::MouseMove,press,Qt::NoButton,Qt::NoButton);
            mouse(QEvent::MouseButtonPress,press,Qt::MiddleButton,Qt::MiddleButton);
            check(view->camera_state()==before,"Middle press alone changed the camera");
            for(const auto delta:{QPointF(24,15),QPointF(190,-105),QPointF(-200,130)}) {
                mouse(QEvent::MouseMove,press+delta,Qt::NoButton,Qt::MiddleButton);
                const auto after=view->camera_state();const auto ray=view->ray_at(press);
                check(ray.has_value(),"Rotated camera lost the pivot ray");
                const QVector3D origin(ray->first.x,ray->first.y,ray->first.z),direction(ray->second.x,ray->second.y,ray->second.z);
                check(QVector3D::crossProduct(pivot-origin,direction.normalized()).length()<.004F,
                    "Cursor pivot moved on screen during orbit");
                check((rotation(after).rotatedVector(pivot-center(after))-rotation(before).rotatedVector(pivot-center(before))).length()<.001F,
                    "Orbit changed pivot camera depth or position");
                for(int i=4;i<8;++i)check(after[i]==before[i],"Cursor orbit changed zoom, pan or reference scale");
            }
            mouse(QEvent::MouseButtonRelease,press+QPointF(-200,130),Qt::MiddleButton,Qt::NoButton);
        }
        const QPointF press(view->width()*.45,view->height()*.45),delta(31,-18);
        const auto before_pan=view->camera_state();
        mouse(QEvent::MouseButtonPress,press,Qt::MiddleButton,Qt::MiddleButton);
        mouse(QEvent::MouseButtonPress,press,Qt::RightButton,Qt::MiddleButton|Qt::RightButton);
        mouse(QEvent::MouseMove,press+delta,Qt::NoButton,Qt::MiddleButton|Qt::RightButton);
        mouse(QEvent::MouseButtonRelease,press+delta,Qt::RightButton,Qt::MiddleButton);
        mouse(QEvent::MouseButtonRelease,press+delta,Qt::MiddleButton,Qt::NoButton);
        const auto after_pan=view->camera_state();
        check(after_pan[5]==before_pan[5]+31&&after_pan[6]==before_pan[6]-18,"Cursor orbit broke middle/right panning");
        for(int i:{0,1,2,3,4,7,8,9,10})check(after_pan[i]==before_pan[i],"Panning changed orientation or orbit center");
        action->setChecked(false);const auto before_default=view->camera_state();
        mouse(QEvent::MouseButtonPress,press,Qt::MiddleButton,Qt::MiddleButton);
        mouse(QEvent::MouseMove,press+delta,Qt::NoButton,Qt::MiddleButton);
        mouse(QEvent::MouseButtonRelease,press+delta,Qt::MiddleButton,Qt::NoButton);
        const auto after_default=view->camera_state();
        check((rotation(after_default).rotatedVector(-center(after_default))-rotation(before_default).rotatedVector(-center(before_default))).length()<.001F,
            "Disabling cursor orbit did not restore the model-center pivot");
        action->setChecked(true);const auto before_click=view->camera_state();
        mouse(QEvent::MouseButtonPress,press,Qt::MiddleButton,Qt::MiddleButton);
        mouse(QEvent::MouseButtonRelease,press,Qt::MiddleButton,Qt::NoButton);
        check(view->camera_state()==before_click,"A short middle click moved the camera");
        action->setChecked(true);fit->trigger();application.processEvents();
        check(center(view->camera_state()).isNull()&&action->isChecked(),"Restore View did not reset framing while keeping navigation mode");
        view->set_fly_navigation_enabled(true);const auto before_fly=view->camera_state();
        mouse(QEvent::MouseButtonPress,press,Qt::MiddleButton,Qt::MiddleButton);
        mouse(QEvent::MouseMove,press+delta,Qt::NoButton,Qt::MiddleButton);
        mouse(QEvent::MouseButtonRelease,press+delta,Qt::MiddleButton,Qt::NoButton);
        check(center(view->camera_state())==center(before_fly),"Cursor pivot leaked into fly navigation");
        view->set_fly_navigation_enabled(false);action->setChecked(false);
    }
    view->set_mesh(original_mesh);view->fit_all();view->set_camera_state(original_camera);view->set_projection_mode(original_projection);
    std::cout<<"Cursor orbit: Ortho/Perspective, fixed depth, successive gestures, pan, model-center mode, short click, Restore View and fly navigation passed\n";
}
