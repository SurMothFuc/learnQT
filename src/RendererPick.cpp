#include "renderer.h"
#include <QCoreApplication>
bool Renderer::updatePick(int w, int h, quint64 version)
{
    if (pickVersion == version && pickSize == QSize(w, h))
        return false;
    if (!pickProgram)
        pickProgram.reset(getShaderProgram(getShaderPath("pick.frag"), getShaderPath("triangle.vert"), {},
                                           {{"INSTANCED_SCENE", "1"},{"PACKED_SURFACE_PDF","1"},
                                            {"PICKING_PASS","1"},{"LEGACY_SAMPLER","1"},
                                            {"MATERIAL_TEXTURE_POOL_COUNT",std::to_string(materialTexturePoolCapacity)}}));
    if (!pickProgram->isLinked())
        throw std::runtime_error("GPU picking shader could not link");
    // 拾取 pass 是整屏 BVH 遍历，和光栅化预览是两笔独立开销，所以单独计时。
    // GL 规范只允许一个活动的 GL_TIME_ELAPSED 查询，这里依赖 Renderer::render() 在返回前
    // 已经结束它自己的查询；若将来把 render 的计时改成跨帧进行，必须同时改掉这一点。
    pollGpuTimers();
    if (!pickTimerQuery)
        glGenQueries(1, &pickTimerQuery);
    const bool pickTimed = !pickTimerPending;
    if (pickTimed)
        glBeginQuery(GL_TIME_ELAPSED, pickTimerQuery);
    GLint previous = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previous);
    if (!pickFbo)
    {
        glGenFramebuffers(1, &pickFbo);
        glGenTextures(2, pickTextures);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, pickFbo);
    if (pickSize != QSize(w, h))
    {
        for (int i = 0; i < 2; ++i)
        {
            glBindTexture(GL_TEXTURE_2D, pickTextures[i]);
            glTexImage2D(GL_TEXTURE_2D, 0, i ? GL_R32F : GL_R32UI, w, h, 0, i ? GL_RED : GL_RED_INTEGER,
                         i ? GL_FLOAT : GL_UNSIGNED_INT, nullptr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + i, GL_TEXTURE_2D, pickTextures[i],
                                   0);
        }
        pickSize = {w, h};
    }
    GLenum draw[] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};
    glDrawBuffers(2, draw);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        throw std::runtime_error("GPU picking framebuffer incomplete");
    pickProgram->bind();
    auto p = pickProgram.get();
    bindInstanceBuffers(p);
    p->setUniformValue("picking", true);
    p->setUniformValue("width", w);
    p->setUniformValue("height", h);
    auto &s = m_scene;
    p->setUniformValue("eye", s.camera.position);
    p->setUniformValue("view", s.camera.getViewMatrix().inverted());
    p->setUniformValue("cameraFov", s.camera.zoom);
    p->setUniformValue("nNodes", int(s.nodes_encoded.size()));
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_BUFFER, trianglesTextureBuffer);
    p->setUniformValue("triangles", 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_BUFFER, nodesTextureBuffer);
    p->setUniformValue("nodes", 1);
    glActiveTexture(GL_TEXTURE6);
    glBindTexture(GL_TEXTURE_2D_ARRAY, materialTextureArray);
    p->setUniformValue("materialTextures", 6);
    glActiveTexture(GL_TEXTURE7);
    glBindTexture(GL_TEXTURE_BUFFER, materialTextureInfoTexture);
    p->setUniformValue("materialTextureInfo", 7);
    p->setUniformValue("materialTextureInfoStride",4);
    p->setUniformValue("materialTextureCount", materialTextureLayerCount);
    bindMaterialTextureInputs(p,6,7);
    glBindVertexArray(VAO);
    glDisable(GL_DEPTH_TEST);
    glViewport(0, 0, w, h);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glEnable(GL_DEPTH_TEST);
    p->release();
    pickVersion = version;
    glBindFramebuffer(GL_FRAMEBUFFER, previous);
    ++stats.pickPasses;
    if (pickTimed)
    {
        glEndQuery(GL_TIME_ELAPSED);
        pickTimerPending = true;
    }
    pollGpuTimers();
    return true;
}
void Renderer::requestPick(QPoint pixel, quint64 request)
{
    if (pickFence)
        glDeleteSync(pickFence);
    if (!pickPbo)
        glGenBuffers(1, &pickPbo);
    GLint previous = 0;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previous);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, pickFbo);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, pickPbo);
    glBufferData(GL_PIXEL_PACK_BUFFER, sizeof(unsigned), nullptr, GL_STREAM_READ);
    glReadPixels(qBound(0, pixel.x(), pickSize.width() - 1),
                 qBound(0, pickSize.height() - 1 - pixel.y(), pickSize.height() - 1), 1, 1, GL_RED_INTEGER,
                 GL_UNSIGNED_INT, nullptr);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, previous);
    pickFence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    glFlush();
    pickRequest = request;
    readVersion = pickVersion;
}
bool Renderer::pollPick(quint64 &request, unsigned &id, quint64 &version)
{
    if (!pickFence)
        return false;
    auto status = glClientWaitSync(pickFence, 0, 0);
    if (status != GL_ALREADY_SIGNALED && status != GL_CONDITION_SATISFIED)
        return false;
    glBindBuffer(GL_PIXEL_PACK_BUFFER, pickPbo);
    auto data = static_cast<const unsigned *>(
        glMapBufferRange(GL_PIXEL_PACK_BUFFER, 0, sizeof(unsigned), GL_MAP_READ_BIT));
    bool ok = data != nullptr;
    if (ok)
    {
        id = *data;
        glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
    }
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    glDeleteSync(pickFence);
    pickFence = nullptr;
    request = pickRequest;
    version = readVersion;
    return ok;
}
