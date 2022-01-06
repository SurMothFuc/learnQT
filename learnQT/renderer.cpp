#include "renderer.h"
//#include "debug.h"


extern Pass_parameters param;
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
GLuint Renderer::bindData(std::unique_ptr<QOpenGLShaderProgram> shaderProgram, std::vector<GLuint> colorAttachments) {//colorAttachments为颜色缓冲 函数返回值为FBO
    GLuint FBO;
    glGenFramebuffers(1, &FBO);
    glBindFramebuffer(GL_FRAMEBUFFER, FBO);


    // 不是 finalPass 则生成帧缓冲的颜色附件 关键
    if (!finalPass) {
        std::vector<GLuint> attachments;
        for (int i = 0; i < colorAttachments.size(); i++) {
            glBindTexture(GL_TEXTURE_2D, colorAttachments[i]);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + i, GL_TEXTURE_2D, colorAttachments[i], 0);// 将颜色纹理绑定到 i 号颜色附件
            attachments.push_back(GL_COLOR_ATTACHMENT0 + i);
        }
        glDrawBuffers(attachments.size(), &attachments[0]);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

Renderer::Renderer(QObject *parent)
    : QObject(parent)
{  
    init();
}

Renderer::~Renderer()
{
    uninit();
}

void Renderer::render(int width, int height)
{
   

   // RAIITimer t("Renderer::render");
    if (m_width != width || m_height != height)
    {
        m_width = width;
        m_height = height;
        adjustSize();      
    }


    QMutexLocker lock(&param_mutex);//对于全局变量的访问使用互斥锁
    {
        //绑定三角形到texture
        
        //更新的时候要把旧的纹理缓冲删除，不然会导致显存泄漏
        glDeleteBuffers(1, &tbo0);
        glDeleteTextures(1, &trianglesTextureBuffer);
        glDeleteBuffers(1, &tbo1);
        glDeleteTextures(1, &nodesTextureBuffer);

        glGenBuffers(1, &tbo0);
        glBindBuffer(GL_TEXTURE_BUFFER, tbo0);
        glBufferData(GL_TEXTURE_BUFFER, param.triangles_encoded.size() * sizeof(Triangle_encoded), &param.triangles_encoded[0], GL_STATIC_DRAW);
        glGenTextures(1, &trianglesTextureBuffer);
        glBindTexture(GL_TEXTURE_BUFFER, trianglesTextureBuffer);
        glTexBuffer(GL_TEXTURE_BUFFER, GL_RGB32F, tbo0);

        glGenBuffers(1, &tbo1);
        glBindBuffer(GL_TEXTURE_BUFFER, tbo1);
        glBufferData(GL_TEXTURE_BUFFER, param.nodes_encoded.size() * sizeof(BVHNode_encoded), &param.nodes_encoded[0], GL_STATIC_DRAW);
        glGenTextures(1, &nodesTextureBuffer);
        glBindTexture(GL_TEXTURE_BUFFER, nodesTextureBuffer);
        glTexBuffer(GL_TEXTURE_BUFFER, GL_RGB32F, tbo1);


        glBindTexture(GL_TEXTURE_2D, 0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);






        m_program->bind();
      /*  QMatrix4x4 projection;
        float store[16];
        projection.copyDataTo(store);*/
       // projection.perspective(param.camera.zoom, 1.0f * m_width / m_height, 0.1f, 100.f);
       // m_program->setUniformValue("projection", projection);
      
        QMatrix4x4 view= param.camera.getViewMatrix();
        float viewStore[16];
        view.copyDataTo(viewStore);
        Eigen::Matrix4f viewM(viewStore);
        viewM = Eigen::Matrix4f(viewM.inverse());//因为是要求光线的方向，所以求逆矩阵
        float* newViewStore = viewM.data();
        view = QMatrix4x4(newViewStore);
        m_program->setUniformValue("view", view);
      //  QMatrix4x4 transform;
      //  transform.translate(QVector3D(0.0f, -0.0f, -1.0f));
        //transform.rotate(param.offx, QVector3D(0.0f, 1.0f, 1.0f));
        //m_program->setUniformValue("model", transform);
        m_program->setUniformValue("eye", param.camera.position);
        m_program->setUniformValue("nTriangles", (int)param.triangles.size());
        m_program->setUniformValue("nNodes", (int)param.nodes_encoded.size());
        m_program->setUniformValue("width", m_width);
        m_program->setUniformValue("height", m_height);
        m_program->release();
    }

   /* static float degree = 0.0f;
    degree += 1.0f;

    QMatrix4x4 rotate;
    rotate.setToIdentity();
    rotate.rotate(degree, 0, 0, 1);*/

    glViewport(m_viewportX, m_viewportY, m_viewportWidth, m_viewportHeight);

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

   
    
    m_program->bind();
    {        
        glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_BUFFER, trianglesTextureBuffer);
        m_program->setUniformValue("triangles", 0);

        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_BUFFER, nodesTextureBuffer);
        m_program->setUniformValue("nodes", 1);

        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, m_texture);
        m_program->setUniformValue("lastFrame", 2);
       
        m_program->setUniformValue("frameCounter", frameCounter++);

        //glBindVertexArray(VAO);
        glDrawArrays(GL_TRIANGLES, 0, 6);
    }
    m_program->release();
    glFinish();
}

void Renderer::init()
{
    initializeOpenGLFunctions();

//    glEnable(GL_DEBUG_OUTPUT);
//    glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
//    glDebugMessageCallback(glDebugOutput, nullptr);

    qDebug() << reinterpret_cast<const char *>(glGetString(GL_VERSION));

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

    glGenFramebuffers(1, &m_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    
    m_texture = getTextureRGB32F(m_width,m_height);
   /* glGenTextures(1, &m_texture);
    glBindTexture(GL_TEXTURE_2D, m_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 0);*/

    glGenRenderbuffers(1, &m_rbo);

    adjustSize();

    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_texture, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, m_rbo);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    QOpenGLShaderProgram* s = new QOpenGLShaderProgram;
    m_program.reset(getShaderProgram("./triangle.frag", "./triangle.vert"));

   
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
    glDeleteRenderbuffers(1, &m_rbo);
    glDeleteTextures(1, &m_texture);
    glDeleteFramebuffers(1, &m_fbo);
}

void Renderer::adjustSize()
{
    glBindTexture(GL_TEXTURE_2D, m_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, m_width, m_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glBindTexture(GL_TEXTURE_2D, 0);

    glBindRenderbuffer(GL_RENDERBUFFER, m_rbo);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, m_width, m_height);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);


    m_viewportX = 0;
    m_viewportY = 0;
    m_viewportWidth = m_width;
    m_viewportHeight = m_height;
   
}
