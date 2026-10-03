#pragma once
#include "EditorController.h"
#include "RenderJob.h"
#include "RenderParams.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QGraphicsPixmapItem>
#include <QGraphicsView>
#include <QLabel>
#include <QPixmap>
#include <QPushButton>
#include <QSpinBox>
#include <QWidget>
#include <functional>
class MixedSpin : public QDoubleSpinBox
{
  public:
    explicit MixedSpin(QWidget *parent = nullptr);
    void showValue(double value, bool mixed = false);
    void resetMixed();
};
class ObjectInspector : public QWidget
{
  public:
    ObjectInspector(EditorController *editor, QWidget *parent = nullptr);
    void refresh();
    void setMaterialPage(bool enabled);
    void browseMaterial(const QString &id);
    std::function<void()> openMaterialPage;

  private:
    EditorController *editor;
    QLabel *summary;
    MixedSpin *transform[9];
    QMap<QString, MixedSpin *> fields;
    QMap<QString, QComboBox *> choices;
    QComboBox *materialList;
    QPushButton *color, *emission, *mediumColor;
    QMap<QString, QPushButton *> textures;
    int epoch = 1;
    bool restoring = false;
    QString materialId() const;
    QString browsedMaterial;
    QWidget *transformSection = nullptr;
    QWidget *materialSlotLabel = nullptr;
    QWidget *materialSection = nullptr;
    QLabel *materialSummary = nullptr;
    QWidget *materialOverview = nullptr;
    void editTransform(int component, double value);
    void chooseColor(const QString &field);
    void chooseTexture(const QString &slot);
};
class PerformancePanel : public QWidget
{
  public:
    explicit PerformancePanel(QWidget *parent = nullptr) : QWidget(parent)
    {
        setMinimumSize(280, 230);
        setAttribute(Qt::WA_OpaquePaintEvent);
    }
    void append(const RenderStats &stats);

  protected:
    void paintEvent(QPaintEvent *) override;

  private:
    QVector<RenderStats> history;
    QPixmap chart;
    bool chartDirty = true;
    void drawChart(QPainter &p);
};
class LightInspector : public QWidget
{
  public:
    LightInspector(EditorController *editor, QWidget *parent = nullptr);

  private:
    EditorController *editor;
    QComboBox *list;
    MixedSpin *values[7];
    QLineEdit *name;
    bool restoring = false;
    void refresh();
    void edit(int field, double value);
};
// 交互预览预算的紧凑编辑面板。视口顶部 chrome 条的弹出菜单用它，
// 顶栏弹层与非模态详情窗共用它，两边读写同一份 SceneDocument 设置。
class PreviewSettingsPanel : public QWidget
{
    Q_OBJECT
  public:
    explicit PreviewSettingsPanel(QWidget *parent = nullptr);
    // 用当前设置填充控件，不发出 changed()。
    void setValues(const RenderParams::Snapshot &settings);
    RenderParams::Snapshot values() const;
  signals:
    void changed(const RenderParams::Snapshot &settings);
  private:
    QSpinBox *samples;
    QSpinBox *bounces;
    QSpinBox *rrMinDepth;
    QSpinBox *tile;
    QCheckBox *tiled;
    QCheckBox *lowResolution;
    QComboBox *denoise;
    QCheckBox *antialiasing;
    QComboBox *interaction;
    QCheckBox *rasterLock;
    QSpinBox *idle;
    bool syncing = false;
    RenderParams::Snapshot originalSettings;
};
class ResultView : public QGraphicsView
{
  public:
    explicit ResultView(QWidget *parent = nullptr);
    void setImage(const QImage &image);
    void fit();
    void actualSize();
    QImage image;

  protected:
    void drawForeground(QPainter *, const QRectF &) override;
    void wheelEvent(QWheelEvent *) override;
    void resizeEvent(QResizeEvent *) override;

  private:
    QGraphicsScene canvas;
    QGraphicsPixmapItem *pixmap;
    bool fitting = true;
};
