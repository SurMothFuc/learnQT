#include "renderer.h"
//#include "debug.h"


extern QMutex param_mutex;

GLuint Renderer::getTextureRGB32F(int width, int height) {
    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, width, height, 0, GL_RGBA, GL_FLOAT, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return tex;
}



QOpenGLShaderProgram* Renderer::getShaderProgram(std::string fshader, std::string vshader) {
    QOpenGLShaderProgram* shaderProgram = new QOpenGLShaderProgram;
    bool success = shaderProgram->addShaderFromSourceFile(QOpenGLShader::Vertex, vshader.c_str());
    if (!success) {
        qDebug() << "shaderProgram addShaderFromSourceFile failed!" << shaderProgram->log();
        return shaderProgram;
    }

    success = shaderProgram->addShaderFromSourceFile(QOpenGLShader::Fragment, fshader.c_str());
    if (!success) {
        qDebug() << "shaderProgram addShaderFromSourceFile failed!" << shaderProgram->log();
        return shaderProgram;
    }

    success = shaderProgram->link();
    if (!success) {
        qDebug() << "shaderProgram link failed!" << shaderProgram->log();
    }
    return shaderProgram;
}
GLuint Renderer::bindData(std::vector<GLuint> colorAttachments) {//colorAttachments为颜色缓冲 函数返回值为FBO
    GLuint FBO=0;
    glGenFramebuffers(1, &FBO);
    glBindFramebuffer(GL_FRAMEBUFFER, FBO);

    // 不是 finalPass 则生成帧缓冲的颜色附件 关键
    //if (!finalPass) {
    if (colorAttachments.size() != 0) {
        std::vector<GLenum> attachments;
        for (int i = 0; i < colorAttachments.size(); i++) {
            glBindTexture(GL_TEXTURE_2D, colorAttachments[i]);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + i, GL_TEXTURE_2D, colorAttachments[i], 0);// 将颜色纹理绑定到 i 号颜色附件
            attachments.push_back(GL_COLOR_ATTACHMENT0 + i);
        }
        glDrawBuffers(attachments.size(), &attachments[0]);
        //glDrawBuffer(GL_COLOR_ATTACHMENT0);
    }
    //}
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        qDebug() << "ERROR::FRAMEBUFFER:: Framebuffer is not complete!" << glCheckFramebufferStatus(GL_FRAMEBUFFER);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return FBO;
}

Renderer::Renderer(int width, int height, QObject *parent)
    : QObject(parent)
{  
    init(width,height);
}

Renderer::~Renderer()
{
    uninit();
}

void Renderer::render(int width, int height)
{   
    if (needupdate) {
        updateparam();
        needupdate = false;
    }

   // RAIITimer t("Renderer::render");
    if (m_width != width || m_height != height)
    {
        qDebug() << "Adjust offscreen frame size to:" << width << height ;
        m_width = width;
        m_height = height;
        adjustSize();      
        updateparam();
    }

    int nowtime = clock();
    if (nowtime - lasttime >200) {
        printf("\r                                                     ");
        std::cout << "\rframeCounter: " << frameCounter<<" FPS: "<<int((frameCounter-lastframeCounter)/(1.0*(nowtime - lasttime)/1000.0));
        lastframeCounter = frameCounter;
        lasttime = nowtime;
    }

   /* static float degree = 0.0f;
    degree += 1.0f;

    QMatrix4x4 rotate;
    rotate.setToIdentity();
    rotate.rotate(degree, 0, 0, 1);*/


    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    //glViewport(m_viewportX, m_viewportY, m_viewportWidth, m_viewportHeight);
    //glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

   
    
    pathtrace_program->bind(); 
    {
        GLint fl_loca = pathtrace_program->uniformLocation("frameCounter");
        glUniform1ui(fl_loca, frameCounter++);

        glBindFramebuffer(GL_FRAMEBUFFER, pathtrace_fbo);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_BUFFER, trianglesTextureBuffer);
        pathtrace_program->setUniformValue("triangles", 0);

        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_BUFFER, nodesTextureBuffer);
        pathtrace_program->setUniformValue("nodes", 1);

        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D,mixframe_texture);
        pathtrace_program->setUniformValue("lastFrame", 2);

        glActiveTexture(GL_TEXTURE3);
        glBindTexture(GL_TEXTURE_2D, hdrMap);
        pathtrace_program->setUniformValue("hdrMap", 3);

        glActiveTexture(GL_TEXTURE4);
        glBindTexture(GL_TEXTURE_2D, hdrCache);
        pathtrace_program->setUniformValue("hdrCache", 4);


        //glBindVertexArray(VAO);

        glViewport(m_viewportX, m_viewportY, m_viewportWidth, m_viewportHeight);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glDrawArrays(GL_TRIANGLES, 0, 6);

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
    pathtrace_program->release();

    mixframe_program->bind();
    {
        glBindFramebuffer(GL_FRAMEBUFFER, mixframe_fbo);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, pathtrace_texture);
        mixframe_program->setUniformValue("texPass0", 0);
        //glBindVertexArray(VAO);

        glViewport(m_viewportX, m_viewportY, m_viewportWidth, m_viewportHeight);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glDrawArrays(GL_TRIANGLES, 0, 6);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
    mixframe_program->release();

    m_program->bind();
    {        
        glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, mixframe_texture);
        m_program->setUniformValue("texPass0", 0);
        //glBindVertexArray(VAO);

        glViewport(m_viewportX, m_viewportY, m_viewportWidth, m_viewportHeight);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glDrawArrays(GL_TRIANGLES, 0, 6);
    }
    m_program->release();

    glFinish();
}

void Renderer::init(int width, int height)
{
    m_width = width;
    m_height = height;
    m_viewportX = 0;
    m_viewportY = 0;
    m_viewportWidth = m_width;
    m_viewportHeight = m_height;
    initializeOpenGLFunctions();

//    glEnable(GL_DEBUG_OUTPUT);
//    glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
//    glDebugMessageCallback(glDebugOutput, nullptr);

    qDebug() << reinterpret_cast<const char *>(glGetString(GL_VERSION));

    
    
   /* glGenTextures(1, &m_texture);
    glBindTexture(GL_TEXTURE_2D, m_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 0);*/

   // glGenRenderbuffers(1, &m_rbo);
    

    //glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_texture, 0);
    //glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, m_rbo);//暂时用不到深度缓冲信息

   // glBindFramebuffer(GL_FRAMEBUFFER, 0);



    pathtrace_program.reset(getShaderProgram("./pathtrace.frag", "./triangle.vert"));
    pathtrace_texture = getTextureRGB32F(m_width, m_height);
    pathtrace_fbo = bindData(std::vector<GLuint>{ pathtrace_texture});


    mixframe_program.reset(getShaderProgram("./mixframe.frag", "./triangle.vert"));
    mixframe_texture = getTextureRGB32F(m_width, m_height);
    mixframe_fbo = bindData(std::vector<GLuint>{ mixframe_texture});


    m_program.reset(getShaderProgram("./triangle.frag", "./triangle.vert"));
    m_texture = getTextureRGB32F(m_width, m_height);
    m_fbo = bindData(std::vector<GLuint>{m_texture});
    //adjustSize();



   
    glEnable(GL_DEPTH_TEST);//开启深度缓冲

    //VAO，VBO数据部分
    std::vector<QVector3D> square = { QVector3D(-1, -1, 0), QVector3D(1, -1, 0), QVector3D(-1, 1, 0), QVector3D(1, 1, 0), QVector3D(-1, 1, 0), QVector3D(1, -1, 0) };

    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);
    // bind the Vertex Array Object first, then bind and set vertex buffer(s), and then configure vertex attributes(s).
    glBindVertexArray(VAO);

    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(QVector3D) * square.size(), NULL, GL_STATIC_DRAW);  //顶点数据复制到缓冲
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(QVector3D) * square.size(), &square[0]);


    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, (GLvoid*)0);
    glEnableVertexAttribArray(0);

    glBindBuffer(GL_ARRAY_BUFFER, 0);//取消VBO的绑定, glVertexAttribPointer已经把顶点属性关联到顶点缓冲对象了

//    remember: do NOT unbind the EBO while a VAO is active as the bound element buffer object IS stored in the VAO; keep the EBO bound.
//    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

//    You can unbind the VAO afterwards so other VAO calls won't accidentally modify this VAO, but this rarely happens. Modifying other
//    VAOs requires a call to glBindVertexArray anyways so we generally don't unbind VAOs (nor VBOs) when it's not directly necessary.
    //glBindVertexArray(0);   //取消VAO绑定

   

}

void Renderer::uninit()
{
    //glDeleteRenderbuffers(1, &m_rbo);
    glDeleteFramebuffers(1, &m_fbo);
    glDeleteFramebuffers(1, &pathtrace_fbo);
    glDeleteFramebuffers(1, &mixframe_fbo);
    /*
    * .........还应该把剩余的添加进来
    */
    // 删除所有纹理
    glDeleteTextures(1, &m_texture);
    glDeleteTextures(1, &pathtrace_texture);
    glDeleteTextures(1, &mixframe_texture);
    glDeleteTextures(1, &hdrMap);
    glDeleteTextures(1, &hdrCache);
    glDeleteTextures(1, &trianglesTextureBuffer);
    glDeleteTextures(1, &nodesTextureBuffer);

    // 删除顶点数组和缓冲对象
    if (VAO) glDeleteVertexArrays(1, &VAO);
    if (VBO) glDeleteBuffers(1, &VBO);

    // 删除纹理缓冲对象
    if (tbo0) glDeleteBuffers(1, &tbo0);
    if (tbo1) glDeleteBuffers(1, &tbo1);

    // 重置标识符防止重复删除
    m_fbo = pathtrace_fbo = mixframe_fbo = 0;
    m_texture = pathtrace_texture = mixframe_texture = 0;
    hdrMap = hdrCache = trianglesTextureBuffer = nodesTextureBuffer = 0;
    VAO = VBO = tbo0 = tbo1 = 0;
}

void Renderer::adjustSize()
{
    glBindTexture(GL_TEXTURE_2D, m_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, m_width, m_height, 0, GL_RGBA, GL_FLOAT, NULL);
    glBindTexture(GL_TEXTURE_2D, 0);

    glBindTexture(GL_TEXTURE_2D, pathtrace_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, m_width, m_height, 0, GL_RGBA, GL_FLOAT, NULL);
    glBindTexture(GL_TEXTURE_2D, 0);

    glBindTexture(GL_TEXTURE_2D, mixframe_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, m_width, m_height, 0, GL_RGBA, GL_FLOAT, NULL);
    glBindTexture(GL_TEXTURE_2D, 0);

    /*glBindRenderbuffer(GL_RENDERBUFFER, m_rbo);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, m_width, m_height);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);*/


    m_viewportX = 0;
    m_viewportY = 0;
    m_viewportWidth = m_width;
    m_viewportHeight = m_height;
   
}

void Renderer::updateparam()
{
    param_mutex.lock();
    {
        //绑定三角形到texture

        //更新的时候要把旧的纹理缓冲删除，不然会导致显存泄漏
        glDeleteBuffers(1, &tbo0);
        glDeleteTextures(1, &trianglesTextureBuffer);
        glDeleteBuffers(1, &tbo1);
        glDeleteTextures(1, &nodesTextureBuffer);
        glDeleteTextures(1, &hdrMap);
        glDeleteTextures(1, &hdrCache);


        glGenBuffers(1, &tbo0);
        glBindBuffer(GL_TEXTURE_BUFFER, tbo0);
        glBufferData(GL_TEXTURE_BUFFER, Scene::getInstance().triangles_encoded.size() * sizeof(Triangle_encoded), &Scene::getInstance().triangles_encoded[0], GL_STATIC_DRAW);
        glGenTextures(1, &trianglesTextureBuffer);
        glBindTexture(GL_TEXTURE_BUFFER, trianglesTextureBuffer);
        glTexBuffer(GL_TEXTURE_BUFFER, GL_RGB32F, tbo0);

        glGenBuffers(1, &tbo1);
        glBindBuffer(GL_TEXTURE_BUFFER, tbo1);
        glBufferData(GL_TEXTURE_BUFFER, Scene::getInstance().nodes_encoded.size() * sizeof(BVHNode_encoded), &Scene::getInstance().nodes_encoded[0], GL_STATIC_DRAW);
        glGenTextures(1, &nodesTextureBuffer);
        glBindTexture(GL_TEXTURE_BUFFER, nodesTextureBuffer);
        glTexBuffer(GL_TEXTURE_BUFFER, GL_RGB32F, tbo1);

        hdrMap = getTextureRGB32F(Scene::getInstance().hdrRes.width, Scene::getInstance().hdrRes.height);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB32F, Scene::getInstance().hdrRes.width, Scene::getInstance().hdrRes.height, 0, GL_RGB, GL_FLOAT, Scene::getInstance().hdrRes.cols);

        hdrCache = getTextureRGB32F(Scene::getInstance().hdrRes.width, Scene::getInstance().hdrRes.height);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB32F, Scene::getInstance().hdrRes.width, Scene::getInstance().hdrRes.height, 0, GL_RGB, GL_FLOAT, Scene::getInstance().cache);

        glBindTexture(GL_TEXTURE_2D, 0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);


        pathtrace_program->bind();
        /*  QMatrix4x4 projection;
          float store[16];
          projection.copyDataTo(store);*/
          // projection.perspective(Pass_parameters::getInstance().camera.zoom, 1.0f * m_width / m_height, 0.1f, 100.f);
          // m_program->setUniformValue("projection", projection);

        QMatrix4x4 view = Scene::getInstance().camera.getViewMatrix();
        float viewStore[16];
        view.copyDataTo(viewStore);
        Eigen::Matrix4f viewM(viewStore);
        viewM = Eigen::Matrix4f(viewM.inverse());//因为是要求光线的方向，所以求逆矩阵
        float* newViewStore = viewM.data();
        view = QMatrix4x4(newViewStore);
        pathtrace_program->setUniformValue("view", view);
        //  QMatrix4x4 transform;
        //  transform.translate(QVector3D(0.0f, -0.0f, -1.0f));
          //transform.rotate(Pass_parameters::getInstance().offx, QVector3D(0.0f, 1.0f, 1.0f));
          //m_program->setUniformValue("model", transform);
        pathtrace_program->setUniformValue("eye", Scene::getInstance().camera.position);
        pathtrace_program->setUniformValue("nTriangles", (int)Scene::getInstance().triangles.size());
        pathtrace_program->setUniformValue("nNodes", (int)Scene::getInstance().nodes_encoded.size());
        pathtrace_program->setUniformValue("width", m_width);
        pathtrace_program->setUniformValue("height", m_height);
        pathtrace_program->setUniformValue("height", m_height);
        pathtrace_program->setUniformValue("hdrResolution", Scene::getInstance().hdrResolution);
        pathtrace_program->release();

        lasttime = clock();
        lastframeCounter = 0;
        frameCounter = 0;
    }
    param_mutex.unlock();
}
