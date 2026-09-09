#pragma once
#include "Scene.h"
#include <QObject>
#include <QSet>
#include <QThread>
#include <QUndoStack>

class EditorCommand;
class EditorController : public QObject
{
    Q_OBJECT
  public:
    enum Change
    {
        Organization = 0,
        Transform = 1,
        MaterialChange = 2,
        Topology = 3,
        Environment = 4,
        CameraChange = 5,
        Display = 6,
        Lighting = 7
    };
    explicit EditorController(QObject *parent = nullptr);
    ~EditorController();
    SceneDocument document;
    Scene::AssetCache cache;
    std::shared_ptr<const SceneAcceleration> acceleration;
    QUndoStack undo;
    QSet<QString> selection;
    QString active;
    QString materialScope;
    bool busy = false, renderLocked = false;
    quint64 version = 1;
    int cameraCommand = 1;
    QMap<QString, SceneBounds> localBounds;
    void install(Scene &scene);
    void select(const QSet<QString> &ids, const QString &activeId = QString());
    QStringList selectedModels(bool editable = false) const;
    QJsonObject node(const QString &id) const;
    bool isGroup(const QString &id) const;
    QString targetGroup() const;
    SceneBounds bounds(const QStringList &ids) const;
    void submit(SceneDocument next, const QString &label, Change change, int mergeKey = -1);
    void importFiles(const QStringList &paths, double scale = 1.0);
    void rename(const QString &id, const QString &name);
    void createGroup(const QString &parent);
    void remove(const QSet<QString> &ids, bool contents = false);
    void duplicate();
    void setFlag(const QString &id, const QString &flag, bool value);
    bool move(const QStringList &ids, const QString &parent, int row = -1);
    void setMaterialField(const QString &field, const QJsonValue &value, int mergeKey = -1);
    void setTransforms(const QMap<QString, QMatrix4x4> &transforms, bool final = true, int mergeKey = -1);
    void previewTransforms(const QMap<QString, QMatrix4x4> &transforms);
    void restorePreview();
    void setCamera(const Camera &camera);
    void markSaved();
  signals:
    void changed(int change);
    void selectionChanged();
    void busyChanged(bool busy);
    void failed(const QString &message);
    void progress(const QString &message);
    void prepared(std::shared_ptr<Scene> scene);
    void preview(const SceneDocument &document, bool final);

  private:
    friend class EditorCommand;
    QThread *worker = nullptr;
    void apply(const SceneDocument &next, Change change, std::shared_ptr<Scene> ready = {});
    void prepare(SceneDocument next, std::function<void(std::shared_ptr<Scene>)> completed);
    void refreshBounds(const Scene &scene);
};
