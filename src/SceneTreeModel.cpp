#include "SceneTreeModel.h"
#include "WorkbenchStyle.h"
#include <QApplication>
#include <QJsonDocument>
#include <QMimeData>
#include <QStyle>
#include <algorithm>
SceneTreeModel::SceneTreeModel(EditorController *e, QObject *p) : QAbstractItemModel(p), editor(e)
{
    refresh();
    connect(e, &EditorController::changed, this, [this](int c) {
        if (c == EditorController::Topology || c == EditorController::Organization)
            refresh();
        else
        {
            for (auto item : items)
                emit dataChanged(from(item), from(item, 2));
        }
    });
}
void SceneTreeModel::refresh()
{
    beginResetModel();
    items.clear();
    root.reset(new Item);
    root->id = "root";
    items["root"] = root.get();
    std::function<void(Item *)> build = [&](Item *parent) {
        QVector<QJsonObject> list;
        for (auto key : {"groups", "objects"})
            for (auto v : editor->document.root[key].toArray())
                if (v.toObject()["parent"] == parent->id)
                    list.append(v.toObject());
        std::stable_sort(list.begin(), list.end(), [](const QJsonObject &a, const QJsonObject &b) {
            return a["order"].toInt() < b["order"].toInt();
        });
        for (auto o : list)
        {
            std::unique_ptr<Item> item(new Item);
            item->id = o["id"].toString();
            item->parent = parent;
            items[item->id] = item.get();
            if (editor->isGroup(item->id))
                build(item.get());
            parent->children.push_back(std::move(item));
        }
    };
    build(root.get());
    endResetModel();
}
QModelIndex SceneTreeModel::from(Item *item, int c) const
{
    if (!item)
        return {};
    if (!item->parent)
        return createIndex(0, c, item);
    auto &a = item->parent->children;
    for (int i = 0; i < int(a.size()); ++i)
        if (a[i].get() == item)
            return createIndex(i, c, item);
    return {};
}
QModelIndex SceneTreeModel::index(int r, int c, const QModelIndex &p) const
{
    if (r < 0 || c < 0 || c > 2)
        return {};
    if (!p.isValid())
        return r == 0 ? from(root.get(), c) : QModelIndex();
    if (p.column() != 0)
        return {};
    auto item = static_cast<Item *>(p.internalPointer());
    return r < int(item->children.size()) ? from(item->children[r].get(), c) : QModelIndex();
}
QModelIndex SceneTreeModel::parent(const QModelIndex &i) const
{
    return i.isValid() ? from(static_cast<Item *>(i.internalPointer())->parent) : QModelIndex();
}
int SceneTreeModel::rowCount(const QModelIndex &p) const
{
    if (!p.isValid())
        return root ? 1 : 0;
    if (p.column() != 0)
        return 0;
    return int(static_cast<Item *>(p.internalPointer())->children.size());
}
QString SceneTreeModel::id(const QModelIndex &i) const
{
    return i.isValid() ? static_cast<Item *>(i.internalPointer())->id : QString();
}
QModelIndex SceneTreeModel::find(const QString &id) const
{
    return from(items.value(id));
}
QVariant SceneTreeModel::data(const QModelIndex &i, int role) const
{
    if (!i.isValid())
        return {};
    auto key = id(i);
    auto o = editor->node(key);
    bool group = editor->isGroup(key);
    if ((role == Qt::DisplayRole || role == Qt::EditRole) && i.column() == 0)
        return o["name"].toString();
    if (role == Qt::UserRole)
        return key;
    if (role == Qt::ToolTipRole)
        return group                  ? tr("组织组 · 选择后操作所有后代模型")
               : o["locked"].toBool() ? tr("已锁定 · 解除后可编辑")
                                      : o["name"].toString();
    if (role == Qt::CheckStateRole && i.column() > 0 && !group)
        return i.column() == 1 ? (o["visible"].toBool(true) ? Qt::Checked : Qt::Unchecked)
                               : (o["locked"].toBool() ? Qt::Checked : Qt::Unchecked);
    if (role == Qt::DecorationRole && i.column() == 0)
        return WorkbenchStyle::icon(group ? "open" : "object");
    if (role == Qt::ForegroundRole && !group && !o["visible"].toBool(true))
        return QColor("#78818c");
    return {};
}
QVariant SceneTreeModel::headerData(int s, Qt::Orientation o, int r) const
{
    if (o == Qt::Horizontal && r == Qt::DisplayRole)
        return QStringList{tr("对象"), tr("显示"), tr("锁定")}.value(s);
    return {};
}
Qt::ItemFlags SceneTreeModel::flags(const QModelIndex &i) const
{
    if (!i.isValid())
        return Qt::NoItemFlags;
    auto f = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    if (editor->busy || editor->renderLocked)
        return f;
    if (i.column() == 0)
    {
        f |= Qt::ItemIsEditable;
        if (id(i) != "root")
            f |= Qt::ItemIsDragEnabled;
        if (editor->isGroup(id(i)))
            f |= Qt::ItemIsDropEnabled;
    }
    else if (!editor->isGroup(id(i)))
        f |= Qt::ItemIsUserCheckable;
    return f;
}
bool SceneTreeModel::setData(const QModelIndex &i, const QVariant &v, int r)
{
    if (r == Qt::EditRole && i.column() == 0)
    {
        editor->rename(id(i), v.toString());
        return true;
    }
    if (r == Qt::CheckStateRole && i.column() > 0)
    {
        editor->setFlag(id(i), i.column() == 1 ? "visible" : "locked", v.toInt() == Qt::Checked);
        return true;
    }
    return false;
}
QStringList SceneTreeModel::mimeTypes() const
{
    return {"application/x-learnqt-scene-nodes"};
}
QMimeData *SceneTreeModel::mimeData(const QModelIndexList &indexes) const
{
    auto m = new QMimeData;
    QJsonArray ids;
    QSet<QString> seen;
    for (auto i : indexes)
        if (i.column() == 0 && !seen.contains(id(i)))
        {
            seen.insert(id(i));
            ids.append(id(i));
        }
    m->setData(mimeTypes()[0], QJsonDocument(ids).toJson(QJsonDocument::Compact));
    return m;
}
bool SceneTreeModel::canDropMimeData(const QMimeData *m, Qt::DropAction action, int, int,
                                     const QModelIndex &p) const
{
    if (action != Qt::MoveAction || !m->hasFormat(mimeTypes()[0]) || !editor->isGroup(id(p)))
        return false;
    for (auto v : QJsonDocument::fromJson(m->data(mimeTypes()[0])).array())
    {
        QString moved = v.toString(), ancestor = id(p);
        if (moved == "root")
            return false;
        while (!ancestor.isEmpty())
        {
            if (ancestor == moved)
                return false;
            ancestor = editor->node(ancestor)["parent"].toString();
        }
    }
    return true;
}
bool SceneTreeModel::dropMimeData(const QMimeData *m, Qt::DropAction a, int row, int column,
                                  const QModelIndex &p)
{
    if (!canDropMimeData(m, a, row, column, p))
        return false;
    QStringList ids;
    for (auto v : QJsonDocument::fromJson(m->data(mimeTypes()[0])).array())
        ids.append(v.toString());
    return editor->move(ids, id(p), row);
}
