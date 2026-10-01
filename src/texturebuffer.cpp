#include "texturebuffer.h"
TextureBuffer *TextureBuffer::instance()
{
    static TextureBuffer b;
    return &b;
}
void TextureBuffer::createTexture(QOpenGLContext *c)
{
    auto f = c->versionFunctions<QOpenGLFunctions_3_3_Core>();
    f->initializeOpenGLFunctions();
    QMutexLocker lock(&mutex);
    for (auto &b : buffers)
    {
        f->glGenTextures(1, &b.texture);
        f->glGenTextures(1, &b.ids);
    }
}
void TextureBuffer::deleteTexture(QOpenGLContext *c)
{
    auto f = c->versionFunctions<QOpenGLFunctions_3_3_Core>();
    QMutexLocker lock(&mutex);
    m_ready = false;
    for (auto &b : buffers)
    {
        if (b.producer)
            f->glDeleteSync(b.producer);
        if (b.consumer)
            f->glDeleteSync(b.consumer);
        f->glDeleteTextures(1, &b.texture);
        f->glDeleteTextures(1, &b.ids);
        b = {};
    }
    displayed = -1;
}
bool TextureBuffer::updateTexture(QOpenGLContext *c, int w, int h, GLuint pickFbo, quint64 version,
                                  GLuint beautyFbo, quint64 minimumVersion, quint64 carriedPickVersion, quint64 contentRevision)
{
    auto f = c->versionFunctions<QOpenGLFunctions_3_3_Core>();
    auto signaled = [&](GLsync s) {
        if (!s)
            return true;
        auto result = f->glClientWaitSync(s, 0, 0);
        return result == GL_ALREADY_SIGNALED || result == GL_CONDITION_SATISFIED;
    };
    int index = -1;
    for (int i = 0; i < 3; ++i)
    {
        {
            QMutexLocker lock(&mutex);
            if (i == displayed || buffers[i].reading || buffers[i].writing ||
                (buffers[i].pending && (buffers[i].version == version ||
                                        buffers[i].version >= minimumVersion)))
                continue;
            buffers[i].writing = true;
        }
        // Only ownership metadata is locked; driver calls and full-resolution copies are outside it.
        if (signaled(buffers[i].producer) && signaled(buffers[i].consumer))
        {
            index = i;
            break;
        }
        QMutexLocker lock(&mutex);
        buffers[i].writing = false;
    }
    if (index < 0)
        return false;
    auto &b = buffers[index];
    if (b.producer)
        f->glDeleteSync(b.producer);
    if (b.consumer)
        f->glDeleteSync(b.consumer);
    b.producer = b.consumer = nullptr;
    f->glActiveTexture(GL_TEXTURE0);
    f->glBindTexture(GL_TEXTURE_2D, b.texture);
    if (b.width != w || b.height != h)
    {
        f->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        f->glBindTexture(GL_TEXTURE_2D, b.ids);
        f->glTexImage2D(GL_TEXTURE_2D, 0, GL_R32UI, w, h, 0, GL_RED_INTEGER, GL_UNSIGNED_INT, nullptr);
        f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        f->glBindTexture(GL_TEXTURE_2D, b.texture);
        b.width = w;
        b.height = h;
        f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    }
    GLint read = 0;
    f->glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &read);
    if (beautyFbo)
    {
        f->glBindFramebuffer(GL_READ_FRAMEBUFFER, beautyFbo);
        f->glReadBuffer(GL_COLOR_ATTACHMENT0);
    }
    f->glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, w, h);
    if (pickFbo)
    {
        f->glBindFramebuffer(GL_READ_FRAMEBUFFER, pickFbo);
        f->glReadBuffer(GL_COLOR_ATTACHMENT0);
        f->glBindTexture(GL_TEXTURE_2D, b.ids);
        f->glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, w, h);
    }
    f->glBindFramebuffer(GL_READ_FRAMEBUFFER, read);
    f->glBindTexture(GL_TEXTURE_2D, 0);
    b.producer = f->glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    f->glFlush();
    QMutexLocker lock(&mutex);
    b.serial = ++serial;
    b.contentRevision = contentRevision ? contentRevision : b.serial;
    b.version = version;
    // 只有拾取缓冲恰好是这个版本重绘的，ID 图才与 beauty 配对。拾取是延迟补绘的，
    // 版本追平并不代表 ID 图已经跟上，所以这个标记必须随拷贝一起记录。
    b.pickFresh = pickFbo != 0 && carriedPickVersion == version;
    b.pending = true;
    b.writing = false;
    m_ready = true;
    return true;
}
bool TextureBuffer::drawTexture(QOpenGLContext *c, int count, quint64 version, quint64 minimumVersion,
                                const std::function<void(bool, bool, quint64)> &beforeDraw)
{
    auto f = c->versionFunctions<QOpenGLFunctions_3_3_Core>();
    int index;
    bool reserved[3] = {};
    {
        QMutexLocker lock(&mutex);
        index = displayed;
        for (int i = 0; i < 3; ++i)
            if (!buffers[i].writing && buffers[i].serial)
                reserved[i] = buffers[i].reading = true;
    }
    auto release = [&] {
        QMutexLocker lock(&mutex);
        displayed = index;
        for (int i = 0; i < 3; ++i)
            if (reserved[i])
            {
                buffers[i].reading = false;
                // Preserve unread frames even when their GPU fence has already completed.
                if (index >= 0 && buffers[i].serial <= buffers[index].serial)
                    buffers[i].pending = false;
            }
    };
    quint64 newest = index < 0 ? 0 : buffers[index].serial;
    for (int i = 0; i < 3; ++i)
        if (reserved[i] && buffers[i].serial > newest && buffers[i].producer)
        {
            auto status = f->glClientWaitSync(buffers[i].producer, 0, 0);
            if (status == GL_ALREADY_SIGNALED || status == GL_CONDITION_SATISFIED)
            {
                index = i;
                newest = buffers[i].serial;
            }
        }
    if (index < 0 || (buffers[index].version != version &&
                     (buffers[index].version < minimumVersion || buffers[index].version > version)))
    {
        release();
        return false;
    }
    auto &b = buffers[index];
    if (beforeDraw)
        beforeDraw(b.version == version, b.pickFresh, b.contentRevision);
    f->glActiveTexture(GL_TEXTURE0);
    f->glBindTexture(GL_TEXTURE_2D, b.texture);
    f->glActiveTexture(GL_TEXTURE1);
    f->glBindTexture(GL_TEXTURE_2D, b.ids);
    f->glActiveTexture(GL_TEXTURE0);
    f->glDrawArrays(GL_TRIANGLES, 0, count);
    f->glBindTexture(GL_TEXTURE_2D, 0);
    if (b.consumer)
        f->glDeleteSync(b.consumer);
    b.consumer = f->glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    f->glFlush();
    release();
    return true;
}
