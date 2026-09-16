#pragma once
#include <QMutex>
#include <QOpenGLContext>
#include <QOpenGLFunctions_3_3_Core>
#include <atomic>
#include <functional>
class TextureBuffer
{
  public:
    static TextureBuffer *instance();
    bool ready() const
    {
        return m_ready.load();
    }
    void createTexture(QOpenGLContext *);
    void deleteTexture(QOpenGLContext *);
    bool updateTexture(QOpenGLContext *, int width, int height, GLuint pickFbo = 0, quint64 version = 0,
                       GLuint beautyFbo = 0, quint64 minimumVersion = ~quint64(0),
                       quint64 carriedPickVersion = ~quint64(0));
    bool drawTexture(QOpenGLContext *, int count, quint64 version,
                     quint64 minimumVersion = ~quint64(0),
                     const std::function<void(bool, bool, quint64)> &beforeDraw = {});

  private:
    struct Slot
    {
        GLuint texture = 0, ids = 0;
        GLsync producer = nullptr, consumer = nullptr;
        int width = 0, height = 0;
        quint64 serial = 0, version = 0;
        // 这张 ID 图是否与 beauty 属于同一次拾取重绘（版本相同）。
        // 拾取是延迟补绘的，版本匹配并不代表 ID 图是这个版本画的。
        bool pickFresh = false;
        bool writing = false, reading = false, pending = false;
    };
    Slot buffers[3];
    QMutex mutex;
    int displayed = -1;
    quint64 serial = 0;
    std::atomic_bool m_ready{false};
};
