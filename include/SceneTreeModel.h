#pragma once
#include "EditorController.h"
#include <QAbstractItemModel>
#include <memory>
class SceneTreeModel : public QAbstractItemModel
{
  public:
    explicit SceneTreeModel(EditorController *editor, QObject *parent = nullptr);
    QModelIndex index(int row, int column, const QModelIndex &parent = {}) const override;
    QModelIndex parent(const QModelIndex &index) const override;
    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex & = {}) const override
    {
        return 3;
    }
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role) override;
    QStringList mimeTypes() const override;
    QMimeData *mimeData(const QModelIndexList &indexes) const override;
    bool canDropMimeData(const QMimeData *, Qt::DropAction, int, int, const QModelIndex &) const override;
    bool dropMimeData(const QMimeData *, Qt::DropAction, int, int, const QModelIndex &) override;
    Qt::DropActions supportedDropActions() const override
    {
        return Qt::MoveAction;
    }
    QModelIndex find(const QString &id) const;
    QString id(const QModelIndex &index) const;
    void refresh();
    int itemCount() const
    {
        return items.size();
    }
    mutable quint64 dataCalls = 0;

  private:
    struct Item
    {
        QString id;
        Item *parent = nullptr;
        // 在父节点 children 中的下标。视图布局会大量调用 from()，
        // 沿用扫描兄弟列表的实现会退化成 O(N²)（实测 71 个条目产生 560 万次 data()）。
        int row = 0;
        std::vector<std::unique_ptr<Item>> children;
    };
    EditorController *editor;
    std::unique_ptr<Item> root;
    QMap<QString, Item *> items;
    QModelIndex from(Item *item, int column = 0) const;
};
