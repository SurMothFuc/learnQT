#include "EditorController.h"
#include "MaterialUi.h"
#include <QDebug>
#include "UiDiagnostics.h"
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonDocument>
#include <QUndoCommand>
#include <algorithm>
#include <cmath>

namespace
{
QJsonObject materialRelevanceState(QJsonObject material, const QJsonObject &values)
{
    for (auto it = values.begin(); it != values.end(); ++it) {
        if (it.key().startsWith("textures.")) {
            auto textures = material["textures"].toObject();
            textures[it.key().mid(9)] = it.value(); material["textures"] = textures;
        } else material[it.key()] = it.value();
    }
    return material;
}
void replaceNode(SceneDocument &d, const QString &id, const std::function<void(QJsonObject &)> &fn)
{
    for (auto key : {"groups", "objects"})
    {
        auto a = d.root[key].toArray();
        for (int i = 0; i < a.size(); ++i)
        {
            auto o = a[i].toObject();
            if (o["id"] == id)
            {
                fn(o);
                a[i] = o;
                d.root[key] = a;
                return;
            }
        }
    }
}
QStringList nodeChildren(const SceneDocument &d, const QString &parent)
{
    QVector<QJsonObject> all;
    for (auto key : {"groups", "objects"})
        for (auto v : d.root[key].toArray())
            if (v.toObject()["parent"] == parent)
                all.append(v.toObject());
    std::stable_sort(all.begin(), all.end(), [](const QJsonObject &a, const QJsonObject &b) {
        return a["order"].toInt() < b["order"].toInt();
    });
    QStringList ids;
    for (auto o : all)
        ids.append(o["id"].toString());
    return ids;
}
void prune(SceneDocument &d)
{
    QSet<QString> models, mats, tex;
    for (auto v : d.root["objects"].toArray())
    {
        models.insert(v.toObject()["model"].toString());
        mats.insert(v.toObject()["material"].toString());
    }
    // Keep imported material bindings: a resource may be duplicated again without reimporting.
    QJsonArray resources;
    for (auto v : d.root["models"].toArray())
        if (models.contains(v.toObject()["id"].toString()))
            resources.append(v);
    d.root["models"] = resources;
    for (auto v : resources)
    {
        auto o = v.toObject();
        mats.insert(o["material"].toString());
        auto b = o["materialBindings"].toObject();
        for (auto it = b.begin(); it != b.end(); ++it)
            mats.insert(it.value().toString());
    }
    QJsonArray materials;
    for (auto v : d.root["materials"].toArray())
        if (mats.contains(v.toObject()["id"].toString()))
        {
            materials.append(v);
            auto t = v.toObject()["textures"].toObject();
            for (auto it = t.begin(); it != t.end(); ++it)
                tex.insert(it.value().toString());
        }
    d.root["materials"] = materials;
    QJsonArray textures;
    for (auto v : d.root["textures"].toArray())
        if (tex.contains(v.toObject()["id"].toString()))
            textures.append(v);
    d.root["textures"] = textures;
}
} // namespace
class EditorCommand : public QUndoCommand
{
  public:
    EditorController *editor;
    SceneDocument before, after;
    EditorController::Change change;
    int key;
    std::shared_ptr<Scene> ready;
    Scene::AssetCache beforeCache, afterCache;
    EditorCommand(EditorController *e, SceneDocument d, QString label, EditorController::Change c, int k,
                  std::shared_ptr<Scene> s = {})
        : editor(e), before(e->document), after(d), change(c), key(k), ready(s), beforeCache(e->cache),
          afterCache(s ? s->assetCache : e->cache)
    {
        setText(label);
    }
    int id() const override
    {
        return key;
    }
    bool mergeWith(const QUndoCommand *other) override
    {
        auto c = dynamic_cast<const EditorCommand *>(other);
        if (!c || key < 0 || c->key != key || change != c->change)
            return false;
        after = c->after;
        return true;
    }
    void undo() override
    {
        editor->cache = beforeCache;
        editor->apply(before, change);
    }
    void redo() override
    {
        editor->cache = afterCache;
        editor->apply(after, change, ready);
        ready.reset();
    }
};
EditorController::EditorController(QObject *p) : QObject(p), undo(this)
{
    undo.setUndoLimit(100);
}
EditorController::~EditorController()
{
    if (worker)
    {
        worker->wait();
        delete worker;
    }
}
void EditorController::refreshBounds(const Scene &s)
{
    acceleration = std::make_shared<SceneAcceleration>(SceneAcceleration{s.instances, s.tlas});
    localBounds.clear();
    for (auto &i : s.instances)
        localBounds[i.id] = s.meshes[i.mesh]->bounds;
}
void EditorController::install(Scene &s)
{
    document = s.document;
    cache = s.assetCache;
    invalidateNodeCache();
    refreshBounds(s);
    selection.clear();
    active.clear();
    undo.clear();
    ++version;
    emit changed(Topology);
    emit selectionChanged();
}
void EditorController::invalidateNodeCache() const
{
    nodeCacheValid = false;
    nodeCache.clear();
    groupCache.clear();
}
void EditorController::rebuildNodeCache() const
{
    nodeCache.clear();
    groupCache.clear();
    for (auto key : {"groups", "objects"})
        for (auto value : document.root[key].toArray())
        {
            const auto object = value.toObject();
            const QString identifier = object["id"].toString();
            if (identifier.isEmpty())
                continue;
            nodeCache.insert(identifier, object);
            if (QLatin1String(key) == QLatin1String("groups"))
                groupCache.insert(identifier);
        }
    nodeCacheValid = true;
}
QJsonObject EditorController::node(const QString &id) const
{
    if (!nodeCacheValid)
        rebuildNodeCache();
    return nodeCache.value(id);
}
bool EditorController::isGroup(const QString &id) const
{
    if (!nodeCacheValid)
        rebuildNodeCache();
    return groupCache.contains(id);
}
void EditorController::select(const QSet<QString> &ids, const QString &a)
{
    selection.clear();
    for (auto id : ids)
        if (!node(id).isEmpty())
            selection.insert(id);
    active = selection.contains(a) ? a : (selection.isEmpty() ? QString() : *selection.begin());
    emit selectionChanged();
}
QStringList EditorController::selectedModels(bool editable) const
{
    QStringList result;
    for (auto v : document.root["objects"].toArray())
    {
        auto o = v.toObject();
        QString id = o["id"].toString(), p = id;
        bool selected = false;
        QSet<QString> visited;
        while (!p.isEmpty() && !visited.contains(p))
        {
            visited.insert(p);
            if (selection.contains(p))
            {
                selected = true;
                break;
            }
            p = node(p)["parent"].toString();
        }
        if (selected && (!editable || !o["locked"].toBool()))
            result.append(id);
    }
    return result;
}
QString EditorController::targetGroup() const
{
    return isGroup(active) ? active : "root";
}
SceneBounds EditorController::bounds(const QStringList &ids) const
{
    SceneBounds b;
    for (auto id : ids)
        b.include(localBounds.value(id).transformed(sceneMatrix(node(id)["transform"])));
    return b;
}
void EditorController::prepare(SceneDocument next, std::function<void(std::shared_ptr<Scene>)> done)
{
    if (busy)
        return;
    busy = true;
    emit busyChanged(true);
    auto result = std::make_shared<std::shared_ptr<Scene>>();
    auto error = std::make_shared<QString>();
    auto resources = cache;
    auto accelerationSnapshot = acceleration;
    worker = QThread::create([this, next, resources, accelerationSnapshot, result, error] {
        *result = Scene::prepareDocument(
            next, *error, resources, [this](const QString &s) { emit progress(s); }, accelerationSnapshot);
    });
    connect(worker, &QThread::finished, this, [this, result, error, done] {
        worker->deleteLater();
        worker = nullptr;
        busy = false;
        emit busyChanged(false);
        if (*result)
            done(*result);
        else
            emit failed(*error);
    });
    worker->start();
}
void EditorController::apply(const SceneDocument &next, Change change, std::shared_ptr<Scene> ready)
{
    auto candidate = next;
    candidate.filePath = document.filePath;
    candidate.root["portable"] = next.root["portable"].toBool() && document.root["portable"].toBool();
    if (change == Topology || change == Environment)
    {
        if (!ready)
        {
            prepare(candidate, [this, change](std::shared_ptr<Scene> s) { apply(s->document, change, s); });
            return;
        }
        ready->document.filePath = candidate.filePath;
        ready->document.root["portable"] = candidate.root["portable"];
        document = ready->document;
        cache = ready->assetCache;
        refreshBounds(*ready);
        emit prepared(ready);
    }
    else
        document = candidate;
    invalidateNodeCache();
    ++version;
    QSet<QString> kept;
    for (auto id : selection)
        if (!node(id).isEmpty())
            kept.insert(id);
    selection = kept;
    if (!selection.contains(active))
        active = selection.isEmpty() ? QString() : *selection.begin();
    // changed() 的所有槽函数是同步执行的，这里的耗时就是相机交互卡顿的直接来源。
    QElapsedTimer signalClock;
    signalClock.start();
    emit changed(change);
    UiDiagnostics::instance().signalMs += signalClock.nsecsElapsed() / 1e6;
    emit selectionChanged();
}
void EditorController::submit(SceneDocument next, const QString &label, Change c, int key)
{
    auto &diagnostics = UiDiagnostics::instance();
    QElapsedTimer submitClock, segmentClock;
    submitClock.start();
    segmentClock.start();
    const bool skip = busy || renderLocked || next.root == document.root;
    diagnostics.compareMs += segmentClock.nsecsElapsed() / 1e6;
    if (skip)
    {
        ++diagnostics.skipped;
        return;
    }
    ++diagnostics.submits;
    if (c == Topology || c == Environment)
    {
        prune(next);
        prepare(next, [this, label, c, key](std::shared_ptr<Scene> s) {
            undo.push(new EditorCommand(this, s->document, label, c, key, s));
        });
        diagnostics.maxSubmitMs = std::max(diagnostics.maxSubmitMs, segmentClock.nsecsElapsed() / 1e6);
    }
    else
    {
        QString error;
        segmentClock.restart();
        const bool valid = next.validate(error, false);
        diagnostics.validateMs += segmentClock.nsecsElapsed() / 1e6;
        if (!valid)
        {
            emit failed(error);
            return;
        }
        segmentClock.restart();
        undo.push(new EditorCommand(this, next, label, c, key));
        // 这一段只包含 undo.push（含其内部的 apply 与所有同步槽）。
        diagnostics.pushMs += segmentClock.nsecsElapsed() / 1e6;
        diagnostics.maxSubmitMs = std::max(diagnostics.maxSubmitMs, segmentClock.nsecsElapsed() / 1e6);
    }
    // 整次 submit：文档比较 + 校验 + undo.push，是相机交互卡顿的直接来源。
    const double totalMs = submitClock.nsecsElapsed() / 1e6;
    diagnostics.submitMs += totalMs;
    diagnostics.maxSubmitCallMs = std::max(diagnostics.maxSubmitCallMs, totalMs);
}
void EditorController::importFiles(const QStringList &paths, double scale)
{
    if (paths.isEmpty() || busy || renderLocked)
        return;
    auto next = document;
    auto models = next.root["models"].toArray();
    next.root["portable"] = false;
    for (auto path : paths)
    {
        QMatrix4x4 m;
        m.scale(float(scale));
        models.append(QJsonObject{{"id", sceneId()},
                                  {"source", QFileInfo(path).absoluteFilePath()},
                                  {"normalize", false},
                                  {"smoothNormals", true},
                                  {"transform", sceneMatrixJson(m)},
                                  {"parent", targetGroup()}});
    }
    next.root["models"] = models;
    prepare(next, [this](std::shared_ptr<Scene> s) {
        QSet<QString> previous;
        for (auto v : document.root["objects"].toArray())
            previous.insert(v.toObject()["id"].toString());
        QSet<QString> added;
        for (auto v : s->document.root["objects"].toArray())
            if (!previous.contains(v.toObject()["id"].toString()))
                added.insert(v.toObject()["id"].toString());
        undo.push(new EditorCommand(this, s->document, tr("导入模型"), Topology, -1, s));
        select(added);
    });
}
void EditorController::rename(const QString &id, const QString &name)
{
    if (name.trimmed().isEmpty())
        return;
    auto next = document;
    replaceNode(next, id, [&](QJsonObject &o) { o["name"] = name.trimmed(); });
    submit(next, tr("重命名"), Organization);
}
void EditorController::createGroup(const QString &parent)
{
    if (!isGroup(parent))
        return;
    auto d = document;
    auto g = d.root["groups"].toArray();
    auto id = sceneId();
    g.append(QJsonObject{
        {"id", id}, {"name", tr("新组")}, {"parent", parent}, {"order", nodeChildren(d, parent).size()}});
    d.root["groups"] = g;
    submit(d, tr("创建组"), Organization);
    select({id}, id);
}
void EditorController::remove(const QSet<QString> &ids, bool contents)
{
    if (ids.contains("root"))
        return;
    auto d = document;
    QSet<QString> removed = ids;
    if (contents)
    {
        bool more = true;
        while (more)
        {
            more = false;
            for (auto key : {"groups", "objects"})
                for (auto v : d.root[key].toArray())
                {
                    auto o = v.toObject();
                    if (removed.contains(o["parent"].toString()) && !removed.contains(o["id"].toString()))
                    {
                        removed.insert(o["id"].toString());
                        more = true;
                    }
                }
        }
    }
    for (auto id : removed)
        if (node(id)["locked"].toBool())
            return;
    // Dissolve groups in place, preserving interleaved group/model order.
    if (!contents)
    {
        // Dissolve deepest groups first so that nested selections never leave an orphan parent.
        auto groups = ids.values();
        auto depth = [this](QString id) {
            int n = 0;
            while (!id.isEmpty())
            {
                id = node(id)["parent"].toString();
                ++n;
            }
            return n;
        };
        std::stable_sort(groups.begin(), groups.end(),
                         [&](const QString &a, const QString &b) { return depth(a) > depth(b); });
        for (auto id : groups)
            if (isGroup(id))
            {
                auto parent = node(id)["parent"].toString();
                auto siblings = nodeChildren(d, parent);
                int at = siblings.indexOf(id);
                siblings.removeAll(id);
                auto nested = nodeChildren(d, id);
                for (int j = 0; j < nested.size(); ++j)
                    siblings.insert(at + j, nested[j]);
                for (int j = 0; j < siblings.size(); ++j)
                    replaceNode(d, siblings[j], [&](QJsonObject &o) {
                        o["parent"] = parent;
                        o["order"] = j;
                    });
            }
    }
    bool geometry = false;
    for (auto key : {"groups", "objects"})
    {
        QJsonArray kept;
        for (auto v : d.root[key].toArray())
            if (!removed.contains(v.toObject()["id"].toString()))
                kept.append(v);
            else if (QString(key) == "objects")
                geometry = true;
        d.root[key] = kept;
    }
    submit(d, contents ? tr("删除组及内容") : tr("删除 / 解散分组"), geometry ? Topology : Organization);
}
void EditorController::duplicate()
{
    auto d = document;
    auto a = d.root["objects"].toArray();
    for (auto id : selectedModels())
    {
        auto o = node(id);
        o["id"] = sceneId();
        o["name"] = o["name"].toString() + tr(" 副本");
        o["locked"] = false;
        o["order"] = nodeChildren(d, o["parent"].toString()).size();
        a.append(o);
    }
    d.root["objects"] = a;
    submit(d, tr("复制模型"), Topology);
}
void EditorController::setFlag(const QString &id, const QString &flag, bool value)
{
    auto d = document;
    auto old = selection;
    selection = {id};
    for (auto model : selectedModels())
        replaceNode(d, model, [&](QJsonObject &o) { o[flag] = value; });
    selection = old;
    submit(d, flag == "locked" ? tr("锁定状态") : tr("可见性"), flag == "locked" ? Organization : Transform);
}
bool EditorController::move(const QStringList &ids, const QString &parent, int row)
{
    if (!isGroup(parent) || ids.contains("root") || busy || renderLocked)
        return false;
    for (auto id : ids)
    {
        QString p = parent;
        while (!p.isEmpty())
        {
            if (p == id)
                return false;
            p = node(p)["parent"].toString();
        }
    }
    QStringList moving;
    for (auto id : ids)
    {
        QString p = node(id)["parent"].toString();
        bool nested = false;
        while (!p.isEmpty())
        {
            if (ids.contains(p))
            {
                nested = true;
                break;
            }
            p = node(p)["parent"].toString();
        }
        if (!nested)
            moving.append(id);
    }
    auto d = document;
    auto siblings = nodeChildren(d, parent);
    int at = row < 0 ? siblings.size() : std::min(row, siblings.size());
    for (auto id : moving)
    {
        int i = siblings.indexOf(id);
        if (i >= 0 && i < at)
            --at;
        siblings.removeAll(id);
    }
    for (int i = 0; i < moving.size(); ++i)
        siblings.insert(at + i, moving[i]);
    for (int i = 0; i < siblings.size(); ++i)
        replaceNode(d, siblings[i], [&](QJsonObject &o) {
            o["parent"] = parent;
            o["order"] = i;
        });
    submit(d, tr("移动分组 / 排序"), Organization);
    return true;
}
void EditorController::setMaterialField(const QString &field, const QJsonValue &value, int key)
{
    auto ids = selectedModels(true);
    if (!materialScope.isEmpty())
        for (auto it = ids.begin(); it != ids.end();)
            if (node(*it)["material"].toString() != materialScope)
                it = ids.erase(it);
            else
                ++it;
    setMaterialFields(QJsonObject{{field, value}}, ids, key);
}
void EditorController::setMaterialFields(const QJsonObject &values, const QStringList &targets, int key)
{
    if (busy || renderLocked || values.isEmpty()) return;
    auto editable = selectedModels(true);
    QStringList ids;
    for (const auto &id : targets) {
        if (!editable.contains(id)) continue;
        QJsonObject material;
        for (auto v : document.root["materials"].toArray())
            if (v.toObject()["id"] == node(id)["material"]) material = v.toObject();
        const auto relevant = materialRelevanceState(material, values);
        for (auto it = values.begin(); it != values.end(); ++it)
            if (materialFieldApplicable(relevant, it.key())) { ids << id; break; }
    }
    if (ids.isEmpty()) return;
    auto d = document;
    auto mats = d.root["materials"].toArray();
    QMap<QString, QString> copies;
    for (auto id : ids)
    {
        auto o = node(id);
        QString source = o["material"].toString(), dest = copies.value(source);
        if (dest.isEmpty())
        {
            bool shared = false;
            for (auto v : d.root["objects"].toArray())
                if (v.toObject()["material"] == source && !ids.contains(v.toObject()["id"].toString()))
                    shared = true;
            for (int j = 0; j < mats.size(); ++j)
                if (mats[j].toObject()["id"] == source)
                {
                    auto m = mats[j].toObject();
                    dest = shared ? sceneId() : source;
                    m["id"] = dest;
                    const auto relevant = materialRelevanceState(m, values);
                    for (auto it = values.begin(); it != values.end(); ++it)
                    {
                        const auto field = it.key(); const auto value = it.value();
                        if (!materialFieldApplicable(relevant, field)) continue;
                        if (field.startsWith("textures."))
                        {
                            auto t = m["textures"].toObject();
                            QString slot = field.mid(9);
                            if (value.isNull() || value.toString().isEmpty())
                                t.remove(slot);
                            else
                                t[slot] = value;
                            m["textures"] = t;
                        }
                        else if (field == "emissionStrength")
                        {
                            auto color = sceneVector(m["emissive"]);
                            float peak = std::max({color.x(), color.y(), color.z()});
                            color = peak > 0 ? emissionHue(color) : emissionHues.value(source, QVector3D(1,1,1));
                            emissionHues[dest] = color;
                            m["emissive"] = jsonVector(color * float(value.toDouble()));
                        }
                        else if (field == "emissionColor")
                        {
                            auto old = sceneVector(m["emissive"]);
                            const float strength = std::max({old.x(), old.y(), old.z()});
                            auto requested = sceneVector(value);
                            auto hue = emissionHue(requested);
                            emissionHues[dest] = hue;
                            m["emissive"] = jsonVector(requested.lengthSquared() > 0 ? hue * strength : QVector3D());
                        }
                        else
                            m[field] = value;
                    }
                    if (shared)
                        mats.append(m);
                    else
                        mats[j] = m;
                    break;
                }
            copies[source] = dest;
        }
        replaceNode(d, id, [&](QJsonObject &item) { item["material"] = dest; });
    }
    d.root["materials"] = mats;
    submit(d, tr("编辑材质"), MaterialChange, key);
}
QVector3D EditorController::materialEmissionHue(const QString &id) const
{
    for (auto v : document.root["materials"].toArray()) if (v.toObject()["id"].toString() == id) {
        auto c = sceneVector(v.toObject()["emissive"]);
        if (c.lengthSquared() > 0) return emissionHue(c);
    }
    return emissionHues.value(id, QVector3D(1,1,1));
}
void EditorController::setTransforms(const QMap<QString, QMatrix4x4> &matrices, bool, int key)
{
    auto d = document;
    for (auto it = matrices.begin(); it != matrices.end(); ++it)
        if (!node(it.key())["locked"].toBool())
            replaceNode(d, it.key(), [&](QJsonObject &o) { o["transform"] = sceneMatrixJson(it.value()); });
    submit(d, tr("变换模型"), Transform, key);
}
void EditorController::previewTransforms(const QMap<QString, QMatrix4x4> &matrices)
{
    if (busy || renderLocked)
        return;
    auto d = document;
    for (auto it = matrices.begin(); it != matrices.end(); ++it)
        if (!node(it.key())["locked"].toBool())
            replaceNode(d, it.key(), [&](QJsonObject &o) { o["transform"] = sceneMatrixJson(it.value()); });
    emit preview(d, false);
}
void EditorController::restorePreview()
{
    emit preview(document, true);
}
void EditorController::setCamera(const Camera &c)
{
    auto next = document;
    next.captureCamera(c);
    submit(next, tr("调整相机"), CameraChange, 0x10000000 + cameraCommand);
}
void EditorController::markSaved()
{
    undo.setClean();
}
