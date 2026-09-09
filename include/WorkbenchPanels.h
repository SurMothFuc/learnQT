#pragma once
#include "EditorController.h"
#include "RenderJob.h"
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QGraphicsPixmapItem>
#include <QGraphicsView>
#include <QLabel>
#include <QPixmap>
#include <QPushButton>
#include <QWidget>
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
