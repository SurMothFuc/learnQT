#include "glwidget.h"
#include "UiDiagnostics.h"
#include <QCoreApplication>
#include <QMouseEvent>
#include <QOffscreenSurface>
#include <QPainter>
#include <QPainterPath>
#include <QQuaternion>
#include <cmath>
QMutex param_mutex;
namespace
{
class ViewportOverlay : public QWidget
{
  public:
    std::function<void(QPainter &)> draw;
    explicit ViewportOverlay(QWidget *parent) : QWidget(parent)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
    }

  protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        if (draw)
            draw(painter);
    }
};
const float quad[] = {-1, -1, 0, 0, -1, 1, 0, 1, 1, -1, 1, 0, -1, 1, 0, 1, 1, 1, 1, 1, 1, -1, 1, 0};
float distanceSegment(QPointF p, QPointF a, QPointF b)
{
    auto d = b - a;
    double length = d.x() * d.x() + d.y() * d.y();
    double t = length > 1e-10 ? QPointF::dotProduct(p - a, d) / length : 0;
    return QLineF(p, a + d * qBound(0., t, 1.)).length();
}
} // namespace
GLWidget::GLWidget(QWidget *p) : QOpenGLWidget(p)
{
    // Probe without creating a window; preserve a GL 3.3 path on older devices.
    QSurfaceFormat requested = format();
    requested.setVersion(4, 3);
    requested.setProfile(QSurfaceFormat::CoreProfile);
    QOpenGLContext probe;
    probe.setFormat(requested);
    if (probe.create() && probe.format().version() >= qMakePair(4, 3))
        setFormat(probe.format());
    else
    {
        requested.setVersion(3, 3);
        setFormat(requested);
    }
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setMinimumSize(160, 120);
    Scene::getInstance().document.restoreCamera(camera);
    auto layer = new ViewportOverlay(this);
    layer->draw = [this](QPainter &painter) { drawOverlay(painter); };
    overlay = layer;
    overlay->show();
}
GLWidget::~GLWidget()
{
    delete thread;
    thread = nullptr;
    if (context())
    {
        makeCurrent();
        glDeleteVertexArrays(1, &vao);
        glDeleteBuffers(1, &vbo);
        glDeleteBuffers(1, &selectionBuffer);
        glDeleteTextures(1, &selectionTexture);
        program.reset();
        doneCurrent();
    }
}
void GLWidget::attachEditor(EditorController *e)
{
    editor = e;
    e->document.restoreCamera(camera);
    connect(e, &EditorController::prepared, this, &GLWidget::submitPrepared);
    connect(e, &EditorController::changed, this, [this](int change) {
        UiSlotTimer timer(UiSlotGlWidget);
        selectionDirty = true;
        if (change == EditorController::CameraChange)
            editor->document.restoreCamera(camera);
        if (change != EditorController::Topology && change != EditorController::Environment)
        {
            if (thread)
            {
                auto updated = editor->document;
                if (compositionMode)
                    updated.captureCamera(camera);
                thread->submitDocument(updated, change, ++version);
            }
            else
            {
                Scene::getInstance().document = editor->document;
            }
        }
        updateEditorOverlay();
    });
    connect(e, &EditorController::selectionChanged, this, [this] {
        selectionDirty = true;
        updateEditorOverlay();
    });
    connect(e, &EditorController::preview, this, [this](const SceneDocument &d, bool final) {
        if (thread)
            thread->submitDocument(d, EditorController::Transform, ++version, final);
        updateEditorOverlay();
    });
}
QRect GLWidget::compositionFrame() const
{
    if (!compositionMode || compositionAspect.width() <= 0 || compositionAspect.height() <= 0)
        return rect();
    const double aspect = double(compositionAspect.width()) / compositionAspect.height();
    QSize frame = size();
    if (double(frame.width()) / std::max(1, frame.height()) > aspect)
        frame.setWidth(std::max(1, int(std::round(frame.height() * aspect))));
    else
        frame.setHeight(std::max(1, int(std::round(frame.width() / aspect))));
    return QRect(QPoint((width() - frame.width()) / 2, (height() - frame.height()) / 2), frame);
}
void GLWidget::setCompositionMode(bool active, const Camera &draft, QSize aspect)
{
    compositionMode = active;
    compositionAspect = active ? aspect : QSize();
    if (active)
        camera = draft;
    else if (editor)
        editor->document.restoreCamera(camera);
    if (thread)
    {
        thread->setPreviewAspect(compositionAspect);
        if (editor)
        {
            auto updated = editor->document;
            if (compositionMode)
                updated.captureCamera(camera);
            thread->submitDocument(updated, EditorController::CameraChange, ++version);
        }
    }
    updateEditorOverlay();
}
void GLWidget::setCompositionAspect(QSize aspect)
{
    if (!compositionMode || compositionAspect == aspect)
        return;
    compositionAspect = aspect;
    if (thread)
        thread->setPreviewAspect(aspect);
    updateEditorOverlay();
}
void GLWidget::submitPrepared(std::shared_ptr<Scene> s)
{
    cancelDrag();
    s->document.restoreCamera(camera);
    if (thread)
    {
        thread->submitScene(s, ++version);
        minimumDisplayVersion = version;
    }
    else
        Scene::getInstance().adoptPrepared(*s);
    selectionDirty = true;
    updateEditorOverlay();
}
void GLWidget::replaceScene(Scene &s)
{
    auto ready = std::make_shared<Scene>(false);
    ready->adoptPrepared(s);
    submitPrepared(ready);
}
void GLWidget::markSceneDirty(SceneDirtyFlags flags)
{
    emit sceneEdited();
    if (thread)
        thread->markSceneDirty(flags);
}
void GLWidget::markSceneDirty(SceneDirtyFlag f)
{
    markSceneDirty(toSceneDirtyFlags(f));
}
void GLWidget::initializeGL()
{
    initializeOpenGLFunctions();
    program.reset(new QOpenGLShaderProgram);
    program->addShaderFromSourceCode(
        QOpenGLShader::Vertex,
        "#version 330 core\nlayout(location=0)in vec2 position;layout(location=1)in vec2 uv;out vec2 "
        "coord;void main(){gl_Position=vec4(position,0,1);coord=uv;}");
    program->addShaderFromSourceCode(QOpenGLShader::Fragment, R"(#version 330 core
        in vec2 coord;out vec4 color;uniform sampler2D beauty;uniform usampler2D ids;uniform samplerBuffer selection;
        uniform bool overlays;uniform int count;
        float state(ivec2 p){ivec2 size=textureSize(ids,0);uint id=texelFetch(ids,clamp(p,ivec2(0),size-1),0).r;return id<uint(count)?texelFetch(selection,int(id)).r:0;}
        void main(){color=vec4(texture(beauty,coord).rgb,1);if(!overlays)return;ivec2 p=ivec2(coord*vec2(textureSize(ids,0)));float s=state(p);bool edge=false;for(int y=-1;y<=1;y++)for(int x=-1;x<=1;x++)if(state(p+ivec2(x,y))!=s)edge=true;float n=max(max(state(p+ivec2(1,0)),state(p-ivec2(1,0))),max(state(p+ivec2(0,1)),state(p-ivec2(0,1))));if(s>0)color.rgb=mix(color.rgb,s>1.5?vec3(1,.64,.25):vec3(1,.38,.06),.13);if(edge&&(s>0||n>0))color.rgb=s>1.5?vec3(1,.76,.42):vec3(1,.46,.12);}
    )");
    program->link();
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    for (int i = 0; i < 2; ++i)
    {
        glVertexAttribPointer(i, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                              reinterpret_cast<void *>(size_t(i * 2 * sizeof(float))));
        glEnableVertexAttribArray(i);
    }
    glBindVertexArray(0);
    glGenBuffers(1, &selectionBuffer);
    glGenTextures(1, &selectionTexture);
    auto shared = context();
    auto mainSurface = shared->surface();
    auto surface = new QOffscreenSurface(nullptr, this);
    surface->setFormat(shared->format());
    surface->create();
    shared->doneCurrent();
    thread = new RenderThread(surface, shared, this);
    shared->makeCurrent(mainSurface);
    connect(thread, &RenderThread::imageReady, this, QOverload<>::of(&GLWidget::update),
            Qt::QueuedConnection);
    connect(
        thread, &RenderThread::picked, this,
        [this](QString id, quint64 serial, quint64 revision) {
            if (!editor || serial != pickSerial || revision != version || editor->renderLocked ||
                editor->busy)
                return;
            auto selected = pickCtrl ? editor->selection : QSet<QString>();
            if (!id.isEmpty())
            {
                if (pickCtrl && selected.contains(id))
                    selected.remove(id);
                else
                    selected.insert(id);
            }
            editor->select(selected, id);
        },
        Qt::QueuedConnection);
    thread->setNewSize(qRound(width() * devicePixelRatioF()), qRound(height() * devicePixelRatioF()));
    if (editor)
        thread->submitDocument(editor->document, EditorController::CameraChange, version);
    emit renderThreadReady();
    thread->start();
}
void GLWidget::paintGL()
{
    glViewport(0, 0, qRound(width() * devicePixelRatioF()), qRound(height() * devicePixelRatioF()));
    glDisable(GL_DEPTH_TEST);
    glClearColor(.075, .085, .10, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    const QRect frame = compositionFrame();
    glViewport(qRound(frame.x() * devicePixelRatioF()),
               qRound((height() - frame.y() - frame.height()) * devicePixelRatioF()),
               qRound(frame.width() * devicePixelRatioF()), qRound(frame.height() * devicePixelRatioF()));
    if (!program)
        return;
    if (selectionDirty)
    {
        QVector<float> states;
        states.append(0);
        auto selected = editor ? editor->selectedModels() : QStringList();
        hasSelection = !selected.isEmpty();
        if (editor)
            for (auto v : editor->document.root["objects"].toArray())
            {
                auto id = v.toObject()["id"].toString();
                states.append(selected.contains(id) ? id == editor->active ? 2 : 1 : 0);
            }
        glBindBuffer(GL_TEXTURE_BUFFER, selectionBuffer);
        glBufferData(GL_TEXTURE_BUFFER, states.size() * sizeof(float), states.constData(), GL_DYNAMIC_DRAW);
        glBindTexture(GL_TEXTURE_BUFFER, selectionTexture);
        glTexBuffer(GL_TEXTURE_BUFFER, GL_R32F, selectionBuffer);
        selectionDirty = false;
    }
    program->bind();
    program->setUniformValue("beauty", 0);
    program->setUniformValue("ids", 1);
    program->setUniformValue("selection", 2);
    program->setUniformValue("count", editor ? editor->document.root["objects"].toArray().size() + 1 : 1);
    // ID 图是延迟补绘的：只有「槽是当前版本」且「这版 ID 图就是这个版本画的」才允许画选中描边。
    // 两个条件缺一：版本匹配只能说明画面新，不能说明拾取已经跟上。
    program->setUniformValue("overlays", false);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_BUFFER, selectionTexture);
    glBindVertexArray(vao);
    if (TextureBuffer::instance()->ready())
    {
        bool fresh = false;
        if (TextureBuffer::instance()->drawTexture(context(), 6, version, minimumDisplayVersion,
                [&](bool current, bool pickFresh, quint64 serial) {
                    fresh = serial != lastPresentationSerial;
                    lastPresentationSerial = serial;
                    // 版本已是最新、但 ID 图还是上一次拾取重绘的结果：这说明延迟补绘与发布
                    // 之间出现了空档，此时若把描边放行就会画在旧位置上。
                    if (current && !pickFresh)
                    {
                        ++staleHiddenCount;
                        if (hasSelection && editor && !editor->renderLocked)
                            ++outstandingStaleCount;
                    }
                    if (current && pickFresh && hasSelection && editor && !editor->renderLocked)
                        ++overlayDrawnCount;
                    program->setUniformValue("overlays", current && pickFresh && hasSelection && editor &&
                                                        !editor->renderLocked);
                }))
        {
            emit framePresented();
            if (fresh) {
                ++displayedFrames; ++displayWindowFrames;
                if (!displayRateClock.isValid()) displayRateClock.start();
                if (displayRateClock.elapsed()>=1000) {
                    displayedFps=displayWindowFrames*1000.0/displayRateClock.elapsed();
                    displayWindowFrames=0;displayRateClock.restart();
                }
                emit freshFramePresented();
            }
        }
    }
    glBindVertexArray(0);
    program->release();
}
void GLWidget::resizeGL(int w, int h)
{
    overlay->setGeometry(rect());
    if (thread)
    {
        thread->setNewSize(qRound(w * devicePixelRatioF()), qRound(h * devicePixelRatioF()));
        if (editor && !editor->renderLocked && !thread->jobActive())
        {
            auto updated = editor->document;
            if (compositionMode)
                updated.captureCamera(camera);
            thread->submitDocument(updated, EditorController::Organization, ++version);
            minimumDisplayVersion = version;
        }
    }
}
QPointF GLWidget::project(const QVector3D &p, bool *visible) const
{
    QMatrix4x4 projection, view;
    const QRect frame = compositionFrame();
    projection.perspective(camera.zoom, float(frame.width()) / std::max(1, frame.height()), .0001f, 1e9f);
    view.lookAt(camera.position, camera.target, camera.up);
    auto q = projection * view * QVector4D(p, 1);
    if (visible)
        *visible = q.w() > 0;
    if (std::abs(q.w()) < 1e-8)
        return {};
    q /= q.w();
    return {frame.x() + (q.x() + 1) * frame.width() * .5,
            frame.y() + (1 - q.y()) * frame.height() * .5};
}
SceneBounds GLWidget::selectedBounds() const
{
    SceneBounds b;
    if (!editor)
        return b;
    for (auto id : editor->selectedModels())
    {
        auto m =
            dragCurrent.contains(id) ? dragCurrent.value(id) : sceneMatrix(editor->node(id)["transform"]);
        b.include(editor->localBounds.value(id).transformed(m));
    }
    return b;
}
float GLWidget::gizmoSize() const
{
    auto b = selectedBounds();
    return std::max(.00001f, (camera.position - b.center()).length() * 2 *
                                 std::tan(qDegreesToRadians(camera.zoom) * .5f) * 80.f /
                                 std::max(1, height()));
}
QVector3D GLWidget::axis(int i) const
{
    QVector3D a;
    a[i] = 1;
    if (localAxes && editor && editor->selectedModels().size() == 1)
        a = sceneMatrix(editor->node(editor->selectedModels().front())["transform"])
                .mapVector(a)
                .normalized();
    return a;
}
void GLWidget::drawOverlay(QPainter &p)
{
    if (compositionMode)
    {
        const QRect frame = compositionFrame();
        p.fillRect(QRect(0, 0, width(), frame.y()), QColor(0, 0, 0, 175));
        p.fillRect(QRect(0, frame.bottom() + 1, width(), height() - frame.bottom() - 1), QColor(0, 0, 0, 175));
        p.fillRect(QRect(0, frame.y(), frame.x(), frame.height()), QColor(0, 0, 0, 175));
        p.fillRect(QRect(frame.right() + 1, frame.y(), width() - frame.right() - 1, frame.height()),
                   QColor(0, 0, 0, 175));
        p.setPen(QPen(QColor("#71a9ff"), 2));
        p.drawRect(frame.adjusted(0, 0, -1, -1));
        return;
    }
    if (!editor || editor->renderLocked)
        return;
    p.setRenderHint(QPainter::Antialiasing);
    auto ids = editor->selectedModels();
    if (ids.isEmpty())
        return;
    p.setPen(QPen(QColor(241, 151, 61, 160), 1, Qt::DashLine));
    for (auto id : ids)
    {
        auto b = editor->localBounds.value(id).transformed(
            dragCurrent.contains(id) ? dragCurrent[id] : sceneMatrix(editor->node(id)["transform"]));
        QPointF corners[8];
        bool visible[8];
        for (int i = 0; i < 8; ++i)
            corners[i] = project(QVector3D(i & 1 ? b.maximum.x() : b.minimum.x(),
                                           i & 2 ? b.maximum.y() : b.minimum.y(),
                                           i & 4 ? b.maximum.z() : b.minimum.z()),
                                 &visible[i]);
        for (int i = 0; i < 8; ++i)
            for (int j = 0; j < 3; ++j)
                if (!(i & (1 << j)) && visible[i] && visible[i | (1 << j)])
                    p.drawLine(corners[i], corners[i | (1 << j)]);
    }
    if (tool == Select || editor->selectedModels(true).isEmpty())
        return;
    auto b = selectedBounds();
    bool visible;
    auto center = project(b.center(), &visible);
    if (!visible)
        return;
    float size = gizmoSize();
    QColor colors[] = {QColor("#ef6666"), QColor("#71d790"), QColor("#6caaff")};
    for (int i = 0; i < 3; ++i)
    {
        p.setPen(QPen(i == dragAxis ? QColor("#fff0af") : colors[i], i == dragAxis ? 4 : 3));
        if (tool == Rotate)
        {
            QVector3D u = axis((i + 1) % 3), v = axis((i + 2) % 3);
            QPainterPath path;
            for (int j = 0; j <= 64; ++j)
            {
                float a = 2 * PI * j / 64;
                auto point = project(b.center() + size * (u * std::cos(a) + v * std::sin(a)));
                if (j == 0)
                    path.moveTo(point);
                else
                    path.lineTo(point);
            }
            p.drawPath(path);
        }
        else
        {
            auto end = project(b.center() + axis(i) * size);
            p.drawLine(center, end);
            p.setBrush(colors[i]);
            if (tool == Scale)
                p.drawRect(QRectF(end - QPointF(4, 4), QSizeF(8, 8)));
            else
                p.drawEllipse(end, 4, 4);
            p.drawText(end + QPointF(6, -6), QString(QString("XYZ")[i]));
        }
    }
    p.setPen(Qt::NoPen);
    p.setBrush(QColor("#e8ebf0"));
    p.drawEllipse(center, 4, 4);
}
void GLWidget::cancelDrag()
{
    if (dragAxis >= 0 && editor)
        editor->restorePreview();
    dragAxis = -1;
    dragBefore.clear();
    dragCurrent.clear();
    updateEditorOverlay();
}
void GLWidget::publishCamera()
{
    ++pickSerial;
    if (compositionMode && editor)
    {
        auto updated = editor->document;
        updated.captureCamera(camera);
        if (thread)
            thread->submitDocument(updated, EditorController::CameraChange, ++version);
        emit compositionCameraChanged();
        updateEditorOverlay();
    }
    else if (editor)
        editor->setCamera(camera);
    else
        markSceneDirty(SceneDirtyFlag::Camera);
}
void GLWidget::frameSelection()
{
    if (!editor || editor->renderLocked)
        return;
    auto b = selectedBounds();
    if (!b.valid)
        return;
    ++editor->cameraCommand;
    float radius = std::max(.01f, (b.maximum - b.minimum).length() * .5f);
    auto forward = (camera.position - camera.target).normalized();
    camera.restoreState(b.center() +
                            forward * (radius / std::sin(qDegreesToRadians(camera.zoom) * .5f) * 1.15f),
                        b.center(), QVector3D(0, 1, 0), camera.zoom);
    publishCamera();
}
void GLWidget::keyPressEvent(QKeyEvent *e)
{
    if (editor && (editor->busy || editor->renderLocked))
        return;
    if (compositionMode)
    {
        if (e->key() == Qt::Key_F)
            frameSelection();
        else
            QOpenGLWidget::keyPressEvent(e);
        return;
    }
    switch (e->key())
    {
    case Qt::Key_Q:
        setTool(Select);
        break;
    case Qt::Key_W:
        setTool(Translate);
        break;
    case Qt::Key_E:
        setTool(Rotate);
        break;
    case Qt::Key_R:
        setTool(Scale);
        break;
    case Qt::Key_F:
        frameSelection();
        break;
    case Qt::Key_Escape:
        cancelDrag();
        break;
    case Qt::Key_Delete:
        if (editor)
            editor->remove(editor->selection);
        break;
    default:
        QOpenGLWidget::keyPressEvent(e);
        return;
    }
    e->accept();
}
void GLWidget::keyReleaseEvent(QKeyEvent *e)
{
    e->ignore();
}
void GLWidget::mousePressEvent(QMouseEvent *e)
{
    setFocus();
    if (!editor || editor->busy || editor->renderLocked)
        return;
    lastPos = e->pos();
    orbit = e->button() == Qt::LeftButton && e->modifiers().testFlag(Qt::AltModifier);
    pan = e->button() == Qt::MiddleButton;
    if (orbit || pan)
    {
        ++editor->cameraCommand;
        return;
    }
    if (compositionMode)
        return;
    if (e->button() != Qt::LeftButton)
        return;
    auto b = selectedBounds();
    float nearest = 10;
    int pickedAxis = -1;
    if (tool != Select && b.valid && !editor->selectedModels(true).isEmpty())
        for (int i = 0; i < 3; ++i)
        {
            float d = 1e9;
            if (tool == Rotate)
            {
                for (int j = 0; j < 64; ++j)
                {
                    float a = 2 * PI * j / 64, beta = 2 * PI * (j + 1) / 64;
                    auto u = axis((i + 1) % 3), v = axis((i + 2) % 3);
                    d = std::min(
                        d,
                        distanceSegment(
                            e->pos(), project(b.center() + gizmoSize() * (u * std::cos(a) + v * std::sin(a))),
                            project(b.center() + gizmoSize() * (u * std::cos(beta) + v * std::sin(beta)))));
                }
            }
            else
                d = distanceSegment(e->pos(), project(b.center()),
                                    project(b.center() + axis(i) * gizmoSize()));
            if (d < nearest)
            {
                nearest = d;
                pickedAxis = i;
            }
        }
    if (pickedAxis >= 0)
    {
        dragAxis = pickedAxis;
        dragStart = e->pos();
        dragCenter = b.center();
        dragDirection = axis(dragAxis);
        dragWorldSize = gizmoSize();
        for (auto id : editor->selectedModels(true))
            dragBefore[id] = sceneMatrix(editor->node(id)["transform"]);
        dragCurrent = dragBefore;
        return;
    }
    pickCtrl = e->modifiers().testFlag(Qt::ControlModifier);
    if (thread)
        thread->pick(
            QPoint(qRound(e->pos().x() * devicePixelRatioF()), qRound(e->pos().y() * devicePixelRatioF())),
            ++pickSerial, version);
}
void GLWidget::mouseMoveEvent(QMouseEvent *e)
{
    if (!editor || editor->renderLocked || editor->busy)
        return;
    auto delta = e->pos() - lastPos;
    lastPos = e->pos();
    if (orbit)
    {
        camera.processMouseMovement(delta.x(), -delta.y());
        publishCamera();
        return;
    }
    if (pan)
    {
        camera.processMousePan(delta.x(), -delta.y());
        publishCamera();
        return;
    }
    if (dragAxis < 0)
        return;
    auto center = project(dragCenter), end = project(dragCenter + dragDirection * dragWorldSize);
    auto axisScreen = end - center;
    double length = std::max(10., QLineF(center, end).length());
    double amount = QPointF::dotProduct(e->pos() - dragStart, axisScreen) / (length * length);
    QMatrix4x4 change;
    if (tool == Translate)
    {
        double distance = amount * dragWorldSize;
        if (snap)
            distance = std::round(distance / moveStep) * moveStep;
        change.translate(dragDirection * float(distance));
    }
    else if (tool == Rotate)
    {
        auto a = QPointF(dragStart) - center, b = QPointF(e->pos()) - center;
        // Screen Y points down; convert to Y-up before computing a right-handed rotation.
        double angle = qRadiansToDegrees(std::atan2(-b.y(), b.x()) - std::atan2(-a.y(), a.x()));
        if (QVector3D::dotProduct(dragDirection, camera.position - dragCenter) < 0)
            angle = -angle;
        if (snap)
            angle = std::round(angle / rotateStep) * rotateStep;
        change.translate(dragCenter);
        change.rotate(float(angle), dragDirection);
        change.translate(-dragCenter);
    }
    else
    {
        double scale = std::max(.001, 1 + amount);
        if (snap)
            scale = std::max(scaleStep, std::round(scale / scaleStep) * scaleStep);
        change.translate(dragCenter);
        QMatrix4x4 stretch;
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 3; ++c)
                stretch(r, c) = (r == c ? 1.f : 0.f) + float(scale - 1) * dragDirection[r] * dragDirection[c];
        change *= stretch;
        change.translate(-dragCenter);
    }
    for (auto it = dragBefore.begin(); it != dragBefore.end(); ++it)
        dragCurrent[it.key()] = change * it.value();
    editor->previewTransforms(dragCurrent);
    updateEditorOverlay();
}
void GLWidget::mouseReleaseEvent(QMouseEvent *e)
{
    orbit = pan = false;
    if (e->button() == Qt::LeftButton && dragAxis >= 0)
    {
        auto transformed = dragCurrent;
        dragAxis = -1;
        dragBefore.clear();
        dragCurrent.clear();
        editor->restorePreview();
        editor->setTransforms(transformed);
        updateEditorOverlay();
    }
}
void GLWidget::wheelEvent(QWheelEvent *e)
{
    if (editor && (editor->busy || editor->renderLocked))
        return;
    camera.processMouseScroll(e->angleDelta().y());
    publishCamera();
}

void GLWidget::updateEditorOverlay()
{
    overlay->update();
    QOpenGLWidget::update();
}
