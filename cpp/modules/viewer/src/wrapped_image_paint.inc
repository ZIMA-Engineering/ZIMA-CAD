    if(impl_->decals_dirty) {
        impl_->decals.clear();
        for(const auto* images:{&impl_->mesh.images,&impl_->transient_context_mesh.images})for(const auto& image:*images) {
            if(image.surface_vertices.empty()||image.surface_vertices.size()!=image.surface_uv.size())continue;
            QImage bitmap;
            if(image.format=="svg") {
                QSvgRenderer svg(QByteArray::fromBase64(QByteArray::fromStdString(image.data_base64)));
                if(svg.isValid()) {
                    auto size=svg.defaultSize();size.scale(2048,2048,Qt::KeepAspectRatio);
                    bitmap=QImage(size,QImage::Format_RGBA8888);bitmap.fill(Qt::transparent);
                    QPainter painter(&bitmap);svg.render(&painter);
                }
            } else bitmap=embedded_png(image.data_base64);
            if(bitmap.isNull())continue;
            auto decal=std::make_unique<Impl::Decal>();decal->vertices.create();decal->vertices.bind();
            std::vector<float> data;data.reserve(image.surface_vertices.size()*5);
            for(std::size_t i=0;i<image.surface_vertices.size();++i) {
                const auto& p=image.surface_vertices[i];const auto& uv=image.surface_uv[i];
                data.insert(data.end(),{float(p.x),float(p.y),float(p.z),float(uv[0]),float(uv[1])});
            }
            decal->vertices.allocate(data.data(),static_cast<int>(data.size()*sizeof(float)));decal->vertices.release();
            decal->texture=std::make_unique<QOpenGLTexture>(bitmap,QOpenGLTexture::GenerateMipMaps);
            decal->texture->setWrapMode(QOpenGLTexture::ClampToEdge);
            decal->texture->setMinMagFilters(QOpenGLTexture::LinearMipMapLinear,QOpenGLTexture::Linear);
            decal->count=static_cast<GLsizei>(image.surface_vertices.size());decal->opacity=float(image.opacity);
            impl_->decals.push_back(std::move(decal));
        }
        impl_->decals_dirty=false;
        setProperty("wrappedImageUploadCount",property("wrappedImageUploadCount").toULongLong()+1);
    }
    if(!impl_->decals.empty()&&(impl_->display_mode==DisplayMode::Shaded||impl_->display_mode==DisplayMode::ShadedWithEdges)) {
        impl_->program.release();impl_->decal_program.bind();
        impl_->decal_program.setUniformValue("mvp",impl_->projection(width(),height())*view);
        impl_->decal_program.setUniformValue("artwork",0);
        glEnable(GL_DEPTH_TEST);glDepthFunc(GL_LEQUAL);glDepthMask(GL_FALSE);
        glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
        glEnable(GL_POLYGON_OFFSET_FILL);glPolygonOffset(-1.F,-1.F);
        for(const auto& decal:impl_->decals) {
            decal->vertices.bind();decal->texture->bind(0);
            impl_->decal_program.enableAttributeArray(0);impl_->decal_program.setAttributeBuffer(0,GL_FLOAT,0,3,5*sizeof(float));
            impl_->decal_program.enableAttributeArray(1);impl_->decal_program.setAttributeBuffer(1,GL_FLOAT,3*sizeof(float),2,5*sizeof(float));
            impl_->decal_program.setUniformValue("opacity",decal->opacity);
            glDrawArrays(GL_TRIANGLES,0,decal->count);decal->texture->release();decal->vertices.release();
        }
        glDisable(GL_POLYGON_OFFSET_FILL);glDisable(GL_BLEND);glDepthMask(GL_TRUE);glDepthFunc(GL_LESS);
        impl_->decal_program.release();impl_->program.bind();
    }
