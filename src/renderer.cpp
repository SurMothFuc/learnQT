#include "renderer.h"
#include <QFile>
#include <QTextStream>
#include <regex>
#include <QFileInfo> 
#include <QDir> 

extern QMutex param_mutex;

std::string processIncludes(const std::string& source, const std::string& shaderPath) {
    static std::unordered_map<std::string, std::string> includeCache;
    static std::unordered_map<std::string, bool> processing; // 防止循环包含

    //主文件缓存检查
    if (includeCache.find(shaderPath) != includeCache.end()) {
        return includeCache[shaderPath];
    }
    processing[shaderPath] = true;

    QDir dir = QFileInfo(QString::fromStdString(shaderPath)).dir().path();
    std::regex includeRegex(R"(^\s*#include\s*\"([^\"]+)\")", std::regex::ECMAScript);
    std::smatch match;
    std::string result = source;

    while (std::regex_search(result, match, includeRegex)) {
        std::string includeFile = match[1].str();
        std::string includePath = dir.filePath(QString::fromStdString(includeFile)).toStdString();

        // 检查循环包含
        if (processing[includePath]) {
            qWarning() << "循环包含检测: " << QString::fromStdString(includePath);
            result = match.prefix().str() + match.suffix().str();
            continue;
        }

        // 读取包含文件
        std::string includeContent;
        if (includeCache.find(includePath) != includeCache.end()) {
            includeContent = includeCache[includePath];
        } else {
            QFile file(QString::fromStdString(includePath));
            if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
                qWarning() << "无法打开包含文件: " << QString::fromStdString(includePath);
                result = match.prefix().str() + match.suffix().str();
                includeCache[includePath] = ""; // 设置缓存空结果避免重复尝试
                continue;
            }
            includeContent = QTextStream(&file).readAll().toStdString();
            file.close();

            // 递归处理包含文件中的#include
            //processing[includePath] = true;
            includeContent = processIncludes(includeContent, includePath);
            //processing[includePath] = false;
           // includeCache[includePath] = includeContent;
        }

        // 替换#include指令为文件内容
        result = match.prefix().str() + includeContent + match.suffix().str();
    }

    // 缓存主文件处理结果
    includeCache[shaderPath] = result;
    processing[shaderPath] = false;

    return result;
}

// 注入动态#define，确保在#version之后添加
std::string injectDefines(const std::string& source, const std::unordered_map<std::string, std::string>& defines) {
    std::string definesStr;
    for (const auto& key_value : defines) {
        definesStr += "#define " + key_value.first + " " + key_value.second + "\n";
    }
    
    // 查找#version位置并在其后插入定义
    size_t versionPos = source.find("#version");
    if (versionPos != std::string::npos) {
        // 找到#version行的结束位置
        size_t lineEnd = source.find("\n", versionPos);
        if (lineEnd == std::string::npos) {
            lineEnd = source.size();
        }
        // 在#version行之后插入定义
        return source.substr(0, lineEnd + 1) + definesStr + source.substr(lineEnd + 1);
    } else {
        // 没有找到#version，仍按原方式添加
        return definesStr + source;
    }
}


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



QOpenGLShaderProgram* Renderer::getShaderProgram(std::string fshader, std::string vshader, const std::unordered_map<std::string, std::string>& defines_Vertex, const std::unordered_map<std::string, std::string>& defines_Fragment) {
    QOpenGLShaderProgram* shaderProgram = new QOpenGLShaderProgram;
    // 加载并处理顶点着色器
    QFile vFile(QString::fromStdString(vshader));
    if (!vFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qDebug() << "顶点着色器打开失败: " << vshader.c_str();
        return shaderProgram;
    }
    std::string vSource = QTextStream(&vFile).readAll().toStdString();
    vFile.close();
    vSource = processIncludes(vSource, vshader);
    vSource = injectDefines(vSource, defines_Vertex);

    bool success = shaderProgram->addShaderFromSourceCode(QOpenGLShader::Vertex, vSource.c_str());
    if (!success) {
        qDebug() << "顶点着色器编译失败:\n" << shaderProgram->log();
        return shaderProgram;
    }

    // 加载并处理片段着色器
    QFile fFile(QString::fromStdString(fshader));
    if (!fFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qDebug() << "片段着色器打开失败: " << fshader.c_str();
        return shaderProgram;
    }
    std::string fSource = QTextStream(&fFile).readAll().toStdString();
    fFile.close();
    fSource = processIncludes(fSource, fshader);
    fSource = injectDefines(fSource, defines_Fragment);

    success = shaderProgram->addShaderFromSourceCode(QOpenGLShader::Fragment, fSource.c_str());
    if (!success) {
        qDebug() << "片段着色器编译失败:\n" << shaderProgram->log();
        return shaderProgram;
    }

    success = shaderProgram->link();
    if (!success) {
        qDebug() << "着色器链接失败:\n" << shaderProgram->log();
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
    init(width, height); 
    initOIDN();
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
    
    adjustScreenResolution(width,height);
    
    int nowtime = clock();
    if (nowtime - lasttime >200) {
        printf("\r                                                     ");
        std::cout << "\rframeCounter: " << frameCounter<<" FPS: "<<int((frameCounter-lastframeCounter)/(1.0*(nowtime - lasttime)/1000.0));
        lastframeCounter = frameCounter;
        lasttime = nowtime;
    }

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    auto sobel_number = getSobelRandomNumber(frameCounter, 12);
    
    // 根据渲染模式选择渲染方法
    if (useTileRendering) {
        // 分块渲染模式
        if (currentTileX < tilesX && currentTileY < tilesY) {
            // 计算当前块的宽度和高度
            int tileWidth = std::min(tileSize, render_width - currentTileX * tileSize);
            int tileHeight = std::min(tileSize, render_height - currentTileY * tileSize);
            
            // 渲染当前块
            renderTile(currentTileX * tileSize, currentTileY * tileSize, tileWidth, tileHeight);
            
            // 更新下一个要渲染的块的位置
            currentTileX++;
            if (currentTileX >= tilesX) {
                currentTileX = 0;
                currentTileY++;
                
                // 如果所有块都渲染完成，标记为完成一轮渲染
                if (currentTileY >= tilesY) {
                    renderComplete = true;
                    // 重置为第一个块，准备下一轮渲染
                    currentTileX = 0;
                    currentTileY = 0;
                }
            }
        }
    } else {
        // 完整图像渲染模式
        renderFullImage();
    }
    

    // 只有在完整渲染模式或者分块渲染完成一轮后才保存历史帧
    if (!useTileRendering || renderComplete) {

        // 更新帧计数器
        frameCounter++;
        historysave_program->bind(); 
        {
            glBindFramebuffer(GL_FRAMEBUFFER, historysave_fbo);

            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, RenderColorTex);
            historysave_program->setUniformValue("RenderColor", 0);

            glViewport(m_viewportX, m_viewportY, render_width, render_height);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

            glDrawArrays(GL_TRIANGLES, 0, 6);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
        }
        historysave_program->release();
        
        // 重置渲染完成标志
        if (useTileRendering) {
            renderComplete = false;
        }
    }

    if(updateDenoise || (denoise && (frameCounter%100==0 || frameCounter ==1)))
    {
        updateDenoise = false;
        // 使用PBO异步读取数据
        GLenum formats[] = { GL_RGB, GL_RGB, GL_RGB };
        GLuint textures[] = { normal_texture, baseColorTex,RenderColorTex };

        float* srcPtrs[3];
        for (int i = frameCounter == 1 ?0:2; i < 3; i++) {
            glBindBuffer(GL_PIXEL_PACK_BUFFER, pboIds[i]);
            glBindTexture(GL_TEXTURE_2D, textures[i]);
            glGetTexImage(GL_TEXTURE_2D, 0, formats[i], GL_FLOAT, 0);
            srcPtrs[i] = (float*)glMapBuffer(GL_PIXEL_PACK_BUFFER, GL_READ_ONLY);
        }

        glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
        glFinish(); // 确保所有异步操作完成


        // 直接拷贝到OIDN缓冲区（避免格式转换）
        if (frameCounter == 1) {
            std::memcpy(oidnNormalBuf.getData(), srcPtrs[0], oidnNormalBuf.getSize());
            std::memcpy(oidnAlbedoBuf.getData(), srcPtrs[1], oidnAlbedoBuf.getSize());
        }
        std::memcpy(oidnColorBuf.getData(), srcPtrs[2], oidnColorBuf.getSize());

        // 解绑PBO
        for (int i = frameCounter == 1 ? 0 : 2; i < 3; i++) {
            glBindBuffer(GL_PIXEL_PACK_BUFFER, pboIds[i]);
            glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
            glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
        }

        // ======= 预过滤辅助特征 =======
        if (frameCounter == 1) {
            // 1. 预过滤反照率
            oidnAlbedoFilter.execute();

            // 2. 预过滤法线
            oidnNormalFilter.execute();
        }

        // 3. 使用预过滤后的辅助特征降噪主颜色
        oidnMainFilter.execute();

        // 错误检查
        const char* errorMessage;
        if (oidnDevice.getError(errorMessage) != oidn::Error::None) {
            std::cerr << "OIDN Error: " << errorMessage << std::endl;
        }

        // 直接写回纹理（避免中间拷贝）
        glBindTexture(GL_TEXTURE_2D, RenderColorTexfiltered);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, render_width, render_height,
            GL_RGB, GL_FLOAT, oidnOutputBuf.getData());


    }

    m_program->bind();
    {        
        
        ///////////////////////////////////////////
        glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);

        glActiveTexture(GL_TEXTURE5);
        if(denoise)
            glBindTexture(GL_TEXTURE_2D, RenderColorTexfiltered);
        else
            glBindTexture(GL_TEXTURE_2D, RenderColorTex);
        m_program->setUniformValue("texPass1", 5);
        glActiveTexture(GL_TEXTURE6);
        //glBindVertexArray(VAO);

        // 渲染到屏幕尺寸 所以使用屏幕渲染尺寸
        glViewport(m_viewportX, m_viewportY, m_width, m_height);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glDrawArrays(GL_TRIANGLES, 0, 6);
    }
    m_program->release();

    //glFinish();
}

void Renderer::setTileRendering(bool enable)
{ 
        useTileRendering = enable; 
        // 重置渲染状态
        currentTileX = 0;
        currentTileY = 0;
        renderComplete = false;
        frameCounter = 0;
}

void Renderer::init(int width, int height)
{
    batchTextureSettings.clear();
    m_width = width;
    m_height = height;
    calResolution();
    m_viewportX = 0;
    m_viewportY = 0;
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
    std::unordered_map<std::string, std::string> defines_Fragment={};
    std::unordered_map<std::string, std::string> defines_Vertex ={};
    if(Scene::getInstance().useEnvironmentMap){
        defines_Fragment.insert({"USEENVIRONMENTMAP", ""});
    }    
    pathtrace_program.reset(getShaderProgram(getShaderPath("pathtrace.frag"), getShaderPath("triangle.vert"), defines_Vertex, defines_Fragment));   
    //pathtrace_texture = getTextureRGB32F(render_width, render_height);
    preRenderColorTex = getTextureRGB32F(render_width, render_height);
    RenderColorTex = getTextureRGB32F(render_width, render_height);
    normal_texture = getTextureRGB32F(render_width, render_height);
    baseColorTex = getTextureRGB32F(render_width, render_height);
    batchTextureSettings.insert(batchTextureSettings.end(),
        { preRenderColorTex,RenderColorTex,normal_texture,baseColorTex }
    );

	pathtrace_fbo = bindData(std::vector<GLuint>{
        RenderColorTex, normal_texture,
        baseColorTex});

    historysave_program.reset(getShaderProgram(getShaderPath("historysave.frag"), getShaderPath("triangle.vert")));
    historysave_fbo= bindData(std::vector<GLuint>{preRenderColorTex});

    RenderColorTexfiltered= getTextureRGB32F(render_width, render_height); 
    batchTextureSettings.insert(batchTextureSettings.end(),
        { RenderColorTexfiltered }
    );
    /*mixframe_program.reset(getShaderProgram("./mixframe.frag", "./triangle.vert"));
    filteredTexture_ping = getTextureRGB32F(render_width, render_height);
    filteredTexture_pong = getTextureRGB32F(render_width, render_height);
    directLightTexfiltered = getTextureRGB32F(render_width, render_height);
    indirectLightTexfiltered = getTextureRGB32F(render_width, render_height);
    batchTextureSettings.insert(batchTextureSettings.end(), 
        { filteredTexture_ping ,filteredTexture_pong,directLightTexfiltered,
        indirectLightTexfiltered });*/

   /* mixframe_fbo_ping = bindData(std::vector<GLuint>{filteredTexture_ping});
    mixframe_fbo_pong = bindData(std::vector<GLuint>{filteredTexture_pong});
    directLight_fbo_filtered = bindData(std::vector<GLuint>{directLightTexfiltered});
    indirectLight_fbo_filtered = bindData(std::vector<GLuint>{indirectLightTexfiltered});*/


    m_program.reset(getShaderProgram(getShaderPath("triangle.frag"), getShaderPath("triangle.vert")));
    m_texture = getTextureRGB32F(m_width, m_height);
    m_fbo = bindData(std::vector<GLuint>{m_texture});





   
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

void Renderer::initOIDN()
{
    oidnDevice = oidn::newDevice();
    oidnDevice.commit();

    // 为辅助特征创建专用过滤器
    oidnAlbedoFilter = oidnDevice.newFilter("RT");
    oidnNormalFilter = oidnDevice.newFilter("RT");

    // 主颜色过滤器
    oidnMainFilter = oidnDevice.newFilter("RT");
    oidnMainFilter.set("hdr", true);
    oidnMainFilter.set("cleanAux", true); // 启用cleanAux参数

    /*oidnAlbedoFilter.set("quality", oidn::Quality::Fast); 
    oidnNormalFilter.set("quality", oidn::Quality::Fast);
    oidnMainFilter.set("quality", oidn::Quality::Fast);*/
 
    // 创建PBO
    glGenBuffers(3, pboIds);

    updateOIDNBuffers();
}

void Renderer::uninit()
{
    //glDeleteRenderbuffers(1, &m_rbo);
    glDeleteFramebuffers(1, &m_fbo);
    glDeleteFramebuffers(1, &pathtrace_fbo);
    glDeleteFramebuffers(1, &historysave_fbo);
    /*
    * .........还应该把剩余的添加进来
    */
    // 删除所有纹理
    glDeleteTextures(1, &m_texture);
    //glDeleteTextures(1, &pathtrace_texture);
    for (auto& per_tex : batchTextureSettings) {
        glDeleteTextures(1, &per_tex);
        per_tex = 0;
    }

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

    //删除 OIDN使用的 OpenGL 的 PBO 缓冲区
    if (pboIds[0] != 0 || pboIds[1] != 0 || pboIds[2] != 0) {
        glDeleteBuffers(3, pboIds);
        std::fill(std::begin(pboIds), std::end(pboIds), 0);
    }

    // 重置标识符防止重复删除
    historysave_fbo= m_fbo = pathtrace_fbo  = 0;
    m_texture = 0;
    hdrMap = hdrCache = trianglesTextureBuffer = nodesTextureBuffer = 0;
    VAO = VBO = tbo0 = tbo1 = 0;
}

void Renderer::updateOIDNBuffers()
{
    ///update OIDN
    size_t bufferSize = render_width * render_height * 3 * sizeof(float);

    oidnColorBuf = oidnDevice.newBuffer(bufferSize);
    oidnAlbedoBuf = oidnDevice.newBuffer(bufferSize);
    oidnNormalBuf = oidnDevice.newBuffer(bufferSize);
    oidnOutputBuf = oidnDevice.newBuffer(bufferSize);

    // 重新配置过滤器
    oidnAlbedoFilter.setImage("albedo", oidnAlbedoBuf, oidn::Format::Float3, render_width, render_height);
    oidnAlbedoFilter.setImage("output", oidnAlbedoBuf, oidn::Format::Float3, render_width, render_height);
    oidnAlbedoFilter.commit();

    oidnNormalFilter.setImage("normal", oidnNormalBuf, oidn::Format::Float3, render_width, render_height);
    oidnNormalFilter.setImage("output", oidnNormalBuf, oidn::Format::Float3, render_width, render_height);
    oidnNormalFilter.commit();

    oidnMainFilter.setImage("color", oidnColorBuf, oidn::Format::Float3, render_width, render_height);
    oidnMainFilter.setImage("albedo", oidnAlbedoBuf, oidn::Format::Float3, render_width, render_height);
    oidnMainFilter.setImage("normal", oidnNormalBuf, oidn::Format::Float3, render_width, render_height);
    oidnMainFilter.setImage("output", oidnOutputBuf, oidn::Format::Float3, render_width, render_height);
    oidnMainFilter.commit();

    // 配置PBO
    for (int i = 0; i < 3; i++) {
        glBindBuffer(GL_PIXEL_PACK_BUFFER, pboIds[i]);
        glBufferData(GL_PIXEL_PACK_BUFFER, bufferSize, NULL, GL_STREAM_READ);
    }
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);

}


void Renderer::adjustSize()
{
    glBindTexture(GL_TEXTURE_2D, m_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F,  m_width, m_height, 0, GL_RGBA, GL_FLOAT, NULL);
    glBindTexture(GL_TEXTURE_2D, 0);


    for (auto& per_tex : batchTextureSettings) {
        glBindTexture(GL_TEXTURE_2D, per_tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, render_width, render_height, 0, GL_RGBA, GL_FLOAT, NULL);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    
    updateOIDNBuffers();
    /*glBindRenderbuffer(GL_RENDERBUFFER, m_rbo);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, m_width, m_height);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);*/

    m_viewportX = 0;
    m_viewportY = 0;
   
}

void Renderer::calResolution()
{
    if (renderLow) {
        // 如果原始分辨率已经小于等于最大低分辨率，则直接使用原始分辨率
        if (m_width <= MAX_LOW_RESOLUTION && m_height <= MAX_LOW_RESOLUTION) {
            render_width = m_width;
            render_height = m_height;
        } else {
            // 计算缩放比例，保持宽高比
            double scaleWidth = static_cast<double>(MAX_LOW_RESOLUTION) / m_width;
            double scaleHeight = static_cast<double>(MAX_LOW_RESOLUTION) / m_height;
            
            // 选择较小的缩放比例，确保两个维度都不超过最大低分辨率
            double scale = std::min(scaleWidth, scaleHeight);
            
            // 计算新的渲染尺寸
            render_width = static_cast<int>(std::round(m_width * scale));
            render_height = static_cast<int>(std::round(m_height * scale));            
        }
    } else {
        // 不使用低分辨率渲染时，直接使用原始分辨率
        render_width = m_width;
        render_height = m_height;
    }
    
    // 重新计算分块渲染的参数
    tilesX = (render_width + tileSize - 1) / tileSize;
    tilesY = (render_height + tileSize - 1) / tileSize;
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
        glTexBuffer(GL_TEXTURE_BUFFER, GL_RGBA32F, tbo0);

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
        pathtrace_program->setUniformValue("width", render_width);
        pathtrace_program->setUniformValue("height", render_height);
        pathtrace_program->setUniformValue("hdrResolution", Scene::getInstance().hdrResolution);
        pathtrace_program->release();

        /*mixframe_program->bind();
        mixframe_program->setUniformValue("width", render_width);
        mixframe_program->setUniformValue("height", render_height);
        mixframe_program->release();*/

        lasttime = clock();
        lastframeCounter = 0;
        frameCounter = 0;

        first_render = true;
        
        // 重置分块渲染状态
        currentTileX = 0;
        currentTileY = 0;
        renderComplete = false;
    }
    param_mutex.unlock();
}

// 渲染单个块
void Renderer::renderTile(int tileX, int tileY, int tileWidth, int tileHeight)
{
    auto sobel_number = getSobelRandomNumber(frameCounter, 12);
    
    pathtrace_program->bind(); 
    {
        GLint fl_loca = pathtrace_program->uniformLocation("frameCounter");
        glUniform1ui(fl_loca, frameCounter);
        GLint sobel_loca = pathtrace_program->uniformLocation("sobelNumber"); 
        glUniform1fv(sobel_loca, 24, sobel_number.data());

        glBindFramebuffer(GL_FRAMEBUFFER, pathtrace_fbo);
        
        pathtrace_program->setUniformValue("triangles", 0);
        pathtrace_program->setUniformValue("nodes", 1);
        pathtrace_program->setUniformValue("hdrMap", 2);
        pathtrace_program->setUniformValue("hdrCache", 3);
        
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_BUFFER, trianglesTextureBuffer);

        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_BUFFER, nodesTextureBuffer);

        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, hdrMap);

        glActiveTexture(GL_TEXTURE3);
        glBindTexture(GL_TEXTURE_2D, hdrCache);

        pathtrace_program->setUniformValue("preRenderColor", 4);
        glActiveTexture(GL_TEXTURE4);
        glBindTexture(GL_TEXTURE_2D, preRenderColorTex);
        
        // 设置视口为当前块的区域
        glViewport(tileX, tileY, tileWidth, tileHeight);
        
        // 设置渲染区域（使用剪裁测试限制渲染区域）
        glEnable(GL_SCISSOR_TEST);
        glScissor(tileX, tileY, tileWidth, tileHeight);
        
        // 只清除当前块区域
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        
        // 渲染
        glDrawArrays(GL_TRIANGLES, 0, 6);
        
        // 禁用剪裁测试
        glDisable(GL_SCISSOR_TEST);
        
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
    pathtrace_program->release();
}

// 渲染完整图像
void Renderer::renderFullImage()
{
    auto sobel_number = getSobelRandomNumber(frameCounter, 12);
    
    pathtrace_program->bind(); 
    {
        GLint fl_loca = pathtrace_program->uniformLocation("frameCounter");
        glUniform1ui(fl_loca, frameCounter);
        GLint sobel_loca = pathtrace_program->uniformLocation("sobelNumber"); 
        glUniform1fv(sobel_loca, 24, sobel_number.data());

        glBindFramebuffer(GL_FRAMEBUFFER, pathtrace_fbo);
        
        pathtrace_program->setUniformValue("triangles", 0);
        pathtrace_program->setUniformValue("nodes", 1);
        pathtrace_program->setUniformValue("hdrMap", 2);
        pathtrace_program->setUniformValue("hdrCache", 3);
        
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_BUFFER, trianglesTextureBuffer);

        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_BUFFER, nodesTextureBuffer);

        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, hdrMap);

        glActiveTexture(GL_TEXTURE3);
        glBindTexture(GL_TEXTURE_2D, hdrCache);

        pathtrace_program->setUniformValue("preRenderColor", 4);
        glActiveTexture(GL_TEXTURE4);
        glBindTexture(GL_TEXTURE_2D, preRenderColorTex);

        // 设置视口为整个渲染区域
        glViewport(m_viewportX, m_viewportY, render_width, render_height);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glDrawArrays(GL_TRIANGLES, 0, 6);

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
    pathtrace_program->release();
    
    // 完整渲染模式下，每次渲染都视为完成一轮
    renderComplete = true;
}


void Renderer::adjustScreenResolution(int width, int height)
{
    int oldWidth = m_width;
    int oldHeight = m_height;
    int oldRenderWidth = render_width;
    int oldRenderHeight = render_height;
    m_width = width;
    m_height = height;
    calResolution();
    if(oldRenderWidth != render_width || oldRenderHeight != render_height||oldWidth != m_width || oldHeight != m_height)
    {
        qDebug() << "Adjust frame size to:" << width << height <<"Adjust render size to:" << render_width << render_height;
        adjustSize(); 
        updateSizeParam();

        lasttime = clock();
        lastframeCounter = 0;
        frameCounter = 0;

        currentTileX = 0;
        currentTileY = 0;
        renderComplete = false;
    }
}

void Renderer::updateSizeParam()
{
    pathtrace_program->bind();
    pathtrace_program->setUniformValue("width", render_width);
    pathtrace_program->setUniformValue("height", render_height);
    pathtrace_program->release();   
}
