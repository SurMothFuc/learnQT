#include "WorkbenchPanels.h"
#include "UiDiagnostics.h"
#include <QColorDialog>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QImageReader>
#include <QLineEdit>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QQuaternion>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <cmath>
MixedSpin::MixedSpin(QWidget *p) : QDoubleSpinBox(p)
{
    setRange(-1e8, 1e8);
    setDecimals(4);
    setSingleStep(.1);
    setKeyboardTracking(false);
    setMinimumWidth(58);
}
void MixedSpin::showValue(double value, bool mixed)
{
    QSignalBlocker block(this);
    setValue(value);
    lineEdit()->setPlaceholderText(mixed ? tr("混合") : QString());
    if (mixed)
        lineEdit()->clear();
}
void MixedSpin::resetMixed()
{
    lineEdit()->setPlaceholderText(QString());
}
namespace
{
QVector3D scaleOf(const QMatrix4x4 &m)
{
    QVector3D s(m.column(0).toVector3D().length(), m.column(1).toVector3D().length(),
                m.column(2).toVector3D().length());
    if (m.determinant() < 0)
        s[0] = -s[0];
    return s;
}
QVector3D rotationOf(const QMatrix4x4 &m)
{
    auto scale = scaleOf(m);
    QMatrix3x3 r;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            r(i, j) = m(i, j) / scale[j];
    return QQuaternion::fromRotationMatrix(r).toEulerAngles();
}
QJsonObject findMaterial(const SceneDocument &d, const QString &id)
{
    for (auto v : d.root["materials"].toArray())
        if (v.toObject()["id"] == id)
            return v.toObject();
    return {};
}
double materialValue(const QJsonObject &m, const QString &field)
{
    if (field == "emissionStrength")
    {
        const auto color = sceneVector(m["emissive"]);
        return std::max({color.x(), color.y(), color.z()});
    }
    if (m[field].isBool())
        return m[field].toBool() ? 1 : 0;
    return m[field].toDouble();
}
} // namespace
ObjectInspector::ObjectInspector(EditorController *e, QWidget *p) : QWidget(p), editor(e)
{
    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 10, 12, 12);
    summary = new QLabel(tr("选择模型以编辑属性"));
    summary->setWordWrap(true);
    summary->setObjectName("muted");
    layout->addWidget(summary);
    auto transformBox = new QGroupBox(tr("变换"));
    transformSection = transformBox;
    auto form = new QFormLayout(transformBox);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    form->setRowWrapPolicy(QFormLayout::WrapLongRows);
    for (int row = 0; row < 3; ++row)
    {
        auto line = new QHBoxLayout;
        line->setSpacing(4);
        for (int c = 0; c < 3; ++c)
        {
            int i = row * 3 + c;
            transform[i] = new MixedSpin;
            transform[i]->setObjectName(QString("transform%1").arg(i));
            transform[i]->setButtonSymbols(QAbstractSpinBox::NoButtons);
            transform[i]->setMinimumWidth(80);
            transform[i]->setPrefix(QString("XYZ")[c] + QString(" "));
            line->addWidget(transform[i]);
            connect(transform[i], QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
                    [this, i](double v) {
                        if (!restoring)
                            editTransform(i, v);
                    });
            connect(transform[i], &QDoubleSpinBox::editingFinished, this, [this] { ++epoch; });
        }
        form->addRow(QStringList{tr("位置"), tr("旋转 °"), tr("缩放")}[row], line);
    }
    layout->addWidget(transformBox);
    auto materialBox = new QGroupBox(tr("材质"));
    materialSection = materialBox;
    materialBox->setObjectName("materialSection");
    auto materialLayout = new QVBoxLayout(materialBox);
    auto materialForm = new QFormLayout;
    materialLayout->addLayout(materialForm);
    auto textureBox = new QGroupBox(tr("纹理贴图"));
    auto textureForm = new QFormLayout(textureBox);
    auto advancedBox = new QGroupBox(tr("高级参数"));
    auto advancedForm = new QFormLayout(advancedBox);
    for (auto f : {materialForm, textureForm, advancedForm}) {
        f->setRowWrapPolicy(QFormLayout::WrapLongRows);
        f->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    }
    materialList = new QComboBox;
    materialList->setMinimumContentsLength(10);
    materialList->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    materialForm->addRow(tr("材质槽"), materialList);
    materialSlotLabel = materialForm->labelForField(materialList);
    connect(materialList, QOverload<int>::of(&QComboBox::activated), this, [this] { refresh(); });
    auto colorButton = [&](const QString &label, const QString &field) {
        auto b = new QPushButton(tr("选择颜色…"));
        (field == "mediumColor" ? advancedForm : materialForm)->addRow(label, b);
        connect(b, &QPushButton::clicked, this, [this, field] { chooseColor(field); });
        return b;
    };
    color = colorButton(tr("基础色"), "baseColor");
    emission = colorButton(tr("发光颜色"), "emissive");
    mediumColor = colorButton(tr("介质颜色"), "mediumColor");
    const QStringList names = {"roughness",      "metallic",      "transmission",     "IOR",
                               "opacity",        "alphaMode",     "alphaCutoff",      "normalScale",
                               "normalMapFlipY", "subsurface",    "specularTint",     "anisotropic",
                               "sheen",          "sheenTint",     "clearcoat",        "clearcoatGloss",
                               "mediumtype",     "mediumDensity", "mediumAnisotropy", "emissionStrength"};
    const QStringList labels = {tr("粗糙度"),       tr("金属度"),           tr("透射"),
                                tr("折射率"),       tr("不透明度"),         tr("透明模式 0/1/2/3"),
                                tr("裁切阈值"),     tr("法线强度"),         tr("法线 Y 翻转 0/1"),
                                tr("次表面"),       tr("高光染色"),         tr("各向异性"),
                                tr("绒光"),         tr("绒光染色"),         tr("清漆"),
                                tr("清漆光泽"),     tr("介质类型 0/1/2/3"), tr("介质密度"),
                                tr("介质各向异性"), tr("发光强度")};
    for (int i = 0; i < names.size(); ++i)
    {
        auto key = names[i];
        if (key == "alphaMode" || key == "mediumtype")
        {
            auto box = new QComboBox;
            choices[key] = box;
            box->addItem(tr("混合"), -1);
            auto options =
                key == "alphaMode"
                    ? QStringList{tr("不透明"), tr("透明边界（兼容）"), tr("裁切 Mask"), tr("混合 Blend")}
                    : QStringList{tr("无介质"), tr("吸收"), tr("散射"), tr("发光介质")};
            for (int j = 0; j < options.size(); ++j)
                box->addItem(options[j], j);
            (key == "mediumtype" ? advancedForm : materialForm)->addRow(key == "alphaMode" ? tr("透明模式") : tr("介质类型"), box);
            connect(box, QOverload<int>::of(&QComboBox::activated), this, [this, key, box](int index) {
                int value = box->itemData(index).toInt();
                if (value >= 0 && !restoring)
                {
                    editor->materialScope = materialId();
                    editor->setMaterialField(key, value);
                }
            });
            continue;
        }
        auto spin = new MixedSpin;
        spin->setRange(key == "anisotropic" || key == "mediumAnisotropy" ? -.99 : 0,
                       key == "IOR"                                     ? 10
                       : key == "normalScale" || key == "mediumDensity" ? 1000
                       : key == "alphaMode"                             ? 3
                       : key == "mediumtype"                            ? 3
                                                                        : 1);
        if (key == "IOR")
            spin->setMinimum(1);
        if (key == "emissionStrength")
            spin->setMaximum(1000000);
        spin->setSingleStep(.05);
        if (key == "alphaMode" || key == "mediumtype" || key == "normalMapFlipY")
        {
            spin->setDecimals(0);
            spin->setSingleStep(1);
        }
        fields[key] = spin;
        spin->setObjectName(key);
        (i >= 7 ? advancedForm : materialForm)->addRow(labels[i], spin);
        connect(spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this, key, i](double v) {
            if (!restoring)
            {
                editor->materialScope = materialId();
                editor->setMaterialField(key, key == "normalMapFlipY" ? QJsonValue(v != 0) : QJsonValue(v),
                                         epoch * 100 + i + 20);
            }
        });
        connect(spin, &QDoubleSpinBox::editingFinished, this, [this] { ++epoch; });
    }
    for (auto slot : QStringList{"baseColor", "normal", "metallic", "roughness", "emissive", "opacity"})
    {
        auto b = new QPushButton;
        textures[slot] = b;
        const QMap<QString, QString> textureLabels{{"baseColor", tr("基础色")}, {"normal", tr("法线")}, {"metallic", tr("金属度")}, {"roughness", tr("粗糙度")}, {"emissive", tr("发光")}, {"opacity", tr("不透明度")}};
        textureForm->addRow(textureLabels.value(slot), b);
        connect(b, &QPushButton::clicked, this, [this, slot] { chooseTexture(slot); });
    }
    materialLayout->addWidget(textureBox);
    materialLayout->addWidget(advancedBox);
    layout->addWidget(materialBox);
    auto overview = new QGroupBox(tr("当前材质"));
    materialOverview = overview;
    auto overviewLayout = new QVBoxLayout(overview);
    materialSummary = new QLabel;
    materialSummary->setObjectName("objectMaterialSummary");
    materialSummary->setWordWrap(true);
    overviewLayout->addWidget(materialSummary);
    auto openMaterial = new QPushButton(tr("进入材质页编辑…"));
    openMaterial->setObjectName("openObjectMaterial");
    overviewLayout->addWidget(openMaterial);
    connect(openMaterial, &QPushButton::clicked, this, [this] {
        if (openMaterialPage) openMaterialPage();
    });
    layout->addWidget(overview);
    layout->addStretch();
    connect(editor, &EditorController::selectionChanged, this, [this] { refresh(); });
    connect(editor, &EditorController::changed, this, [this](int) {
        UiSlotTimer timer(UiSlotObjectInspector);
        refresh();
    });
    connect(editor, &EditorController::busyChanged, this, [this] { refresh(); });
    refresh();
}
QString ObjectInspector::materialId() const
{
    return browsedMaterial.isEmpty() ? materialList->currentData().toString() : browsedMaterial;
}
void ObjectInspector::refresh()
{
    restoring = true;
    auto ids = editor->selectedModels();
    auto editable = editor->selectedModels(true);
    bool scopeEditable = browsedMaterial.isEmpty();
    QStringList affected;
    for (auto id : editable) {
        const auto node = editor->node(id);
        if (browsedMaterial.isEmpty() || node["material"].toString() == browsedMaterial) {
            scopeEditable = true;
            affected << node["name"].toString(id);
        }
    }
    bool enabled = !editable.isEmpty() && scopeEditable && !editor->busy && !editor->renderLocked;
    for (auto s : transform)
        s->setEnabled(enabled);
    for (auto s : fields)
        s->setEnabled(enabled);
    for (auto box : choices)
        box->setEnabled(enabled);
    color->setEnabled(enabled);
    emission->setEnabled(enabled);
    mediumColor->setEnabled(enabled);
    for (auto b : textures)
        b->setEnabled(enabled);
    summary->setText(ids.isEmpty() ? tr("选择模型以编辑属性")
                                   : tr("已选择 %1 个模型 · %2 个可编辑\n%3")
                                         .arg(ids.size())
                                         .arg(editable.size())
                                         .arg(ids.size() > 1 ? tr("多选旋转 / 缩放围绕整体中心按增量应用")
                                                             : editor->node(ids.front())["name"].toString()));
    QStringList materialNames;
    QSet<QString> materialIds;
    for (const auto &id : ids)
        materialIds.insert(editor->node(id)["material"].toString());
    for (const auto &id : materialIds) {
        const auto definition = findMaterial(editor->document, id);
        materialNames << definition["name"].toString(id);
    }
    materialSummary->setText(materialNames.isEmpty() ? tr("未选择模型") : materialNames.join("\n"));
    materialOverview->findChild<QPushButton *>()->setEnabled(!ids.isEmpty());
    if (ids.isEmpty() && browsedMaterial.isEmpty())
    {
        materialList->clear();
        for (auto field : fields) field->clear();
        for (auto texture : textures) texture->setText(tr("未选择材质"));
        restoring = false;
        return;
    }
    if (!browsedMaterial.isEmpty())
        summary->setText(enabled ? tr("影响 %1 个可编辑对象：%2\n共享材质按需隔离，修改不影响未选中对象")
                                      .arg(affected.size()).arg(affected.mid(0, 4).join("、") + (affected.size() > 4 ? "…" : ""))
                                : editor->renderLocked ? tr("只读 · 正式任务锁定编辑")
                                : !ids.isEmpty() && editable.isEmpty() ? tr("只读 · 所选对象已锁定")
                                : tr("只读浏览 · 选择使用此材质的对象后可编辑"));
    summary->setToolTip(affected.join("\n"));
    auto m = sceneMatrix(editor->node(ids.isEmpty() ? QString() : ids.front())["transform"]);
    auto position = ids.size() > 1 ? editor->bounds(ids).center() : m.column(3).toVector3D();
    auto rotation = ids.size() > 1 ? QVector3D() : rotationOf(m);
    auto scale = ids.size() > 1 ? QVector3D(1, 1, 1) : scaleOf(m);
    for (int i = 0; i < 9; ++i)
        transform[i]->showValue(i < 3 ? position[i] : i < 6 ? rotation[i - 3] : scale[i - 6]);
    auto previous = materialId();
    QSignalBlocker block(materialList);
    materialList->clear();
    QSet<QString> materials;
    for (auto id : ids)
        materials.insert(editor->node(id)["material"].toString());
    materialList->addItem(tr("所有选中材质"), QString());
    for (auto id : materials)
        materialList->addItem(id, id);
    if (materialList->findData(previous) >= 0)
        materialList->setCurrentIndex(materialList->findData(previous));
    if (!browsedMaterial.isEmpty())
        materials = QSet<QString>{browsedMaterial};
    QList<QJsonObject> definitions;
    for (auto id : materials)
        if (materialId().isEmpty() || id == materialId())
            definitions.append(findMaterial(editor->document, id));
    if (definitions.isEmpty())
    {
        restoring = false;
        return;
    }
    for (auto it = choices.begin(); it != choices.end(); ++it)
    {
        int value = definitions.front()[it.key()].toInt();
        for (auto d : definitions)
            if (d[it.key()].toInt() != value)
            {
                value = -1;
                break;
            }
        QSignalBlocker blocker(it.value());
        it.value()->setCurrentIndex(it.value()->findData(value));
    }
    for (auto it = fields.begin(); it != fields.end(); ++it)
    {
        double first = materialValue(definitions.front(), it.key());
        bool mixed = false;
        for (auto d : definitions)
            if (std::abs(materialValue(d, it.key()) - first) > 1e-7)
                mixed = true;
        it.value()->showValue(first, mixed);
    }
    for (auto pair : QVector<QPair<QString, QPushButton *>>{
             {"baseColor", color}, {"emissive", emission}, {"mediumColor", mediumColor}})
    {
        auto c = sceneVector(definitions.front()[pair.first]);
        bool mixed = false;
        for (auto d : definitions)
            if (d[pair.first] != definitions.front()[pair.first])
                mixed = true;
        pair.second->setText(
            mixed ? tr("混合 · 点击设置")
                  : QString("%1, %2, %3").arg(c.x(), 0, 'f', 2).arg(c.y(), 0, 'f', 2).arg(c.z(), 0, 'f', 2));
    }
    for (auto it = textures.begin(); it != textures.end(); ++it)
    {
        auto refs = definitions.front()["textures"].toObject();
        QString id = refs[it.key()].toString();
        bool mixed = false;
        for (auto d : definitions)
            if (d["textures"].toObject()[it.key()].toString() != id)
                mixed = true;
        it.value()->setText(mixed ? tr("混合…") : id.isEmpty() ? tr("未设置…") : tr("查看 / 更换…"));
        it.value()->setToolTip(id);
    }
    restoring = false;
}
void ObjectInspector::setMaterialPage(bool enabled)
{
    transformSection->setVisible(!enabled);
    materialSection->setVisible(enabled);
    materialOverview->setVisible(!enabled);
    if (!enabled) browsedMaterial.clear();
    materialList->setVisible(browsedMaterial.isEmpty());
    materialSlotLabel->setVisible(browsedMaterial.isEmpty());
    refresh();
}
void ObjectInspector::browseMaterial(const QString &id)
{
    browsedMaterial = id;
    materialList->setVisible(id.isEmpty());
    materialSlotLabel->setVisible(id.isEmpty());
    refresh();
}
void ObjectInspector::editTransform(int index, double value)
{
    auto ids = editor->selectedModels(true);
    QMap<QString, QMatrix4x4> matrices;
    if (ids.isEmpty())
        return;
    if (editor->selectedModels().size() == 1)
    {
        auto m = sceneMatrix(editor->node(ids.front())["transform"]);
        auto position = m.column(3).toVector3D(), rotation = rotationOf(m), scale = scaleOf(m);
        if (index < 3)
        {
            m(index, 3) = value;
            matrices[ids.front()] = m;
        }
        else
        {
            if (index < 6)
                rotation[index - 3] = value;
            else
                scale[index - 6] = std::abs(value) < .0001 ? .0001 : value;
            QMatrix4x4 next;
            next.translate(position);
            next.rotate(QQuaternion::fromEulerAngles(rotation));
            next.scale(scale);
            matrices[ids.front()] = next;
        }
    }
    else
    {
        auto center = editor->bounds(editor->selectedModels()).center();
        QMatrix4x4 change;
        if (index < 3)
        {
            QVector3D delta;
            delta[index] = value - center[index];
            change.translate(delta);
        }
        else
        {
            change.translate(center);
            if (index < 6)
            {
                QVector3D axis;
                axis[index - 3] = 1;
                change.rotate(value, axis);
            }
            else
            {
                QVector3D scale(1, 1, 1);
                scale[index - 6] = std::max(.0001, value);
                change.scale(scale);
            }
            change.translate(-center);
        }
        for (auto id : ids)
            matrices[id] = change * sceneMatrix(editor->node(id)["transform"]);
    }
    editor->setTransforms(matrices, true, epoch * 100 + index);
}
void ObjectInspector::chooseColor(const QString &field)
{
    editor->materialScope = materialId();
    auto ids = editor->selectedModels(true);
    if (ids.isEmpty())
        return;
    auto m = findMaterial(editor->document, materialId().isEmpty() ? editor->node(ids.front())["material"].toString() : materialId());
    auto c = sceneVector(m[field]);
    auto chosen = QColorDialog::getColor(
        QColor::fromRgbF(qBound(0.f, c.x(), 1.f), qBound(0.f, c.y(), 1.f), qBound(0.f, c.z(), 1.f)), this,
        tr("设置线性颜色"));
    if (chosen.isValid())
        editor->setMaterialField(field,
                                 jsonVector(QVector3D(chosen.redF(), chosen.greenF(), chosen.blueF())));
}
void ObjectInspector::chooseTexture(const QString &slot)
{
    editor->materialScope = materialId();
    QMenu menu;
    auto view = menu.addAction(tr("查看纹理"));
    auto change = menu.addAction(tr("更换纹理…"));
    auto clear = menu.addAction(tr("清除纹理"));
    auto action = menu.exec(QCursor::pos());
    if (!action)
        return;
    auto ids = editor->selectedModels(true);
    if (!materialId().isEmpty())
        ids.erase(std::remove_if(ids.begin(), ids.end(),
                                 [this](const QString &id) {
                                     return editor->node(id)["material"].toString() != materialId();
                                 }),
                  ids.end());
    if (ids.isEmpty())
        return;
    auto mat = findMaterial(editor->document, editor->node(ids.front())["material"].toString());
    auto textureId = mat["textures"].toObject()[slot].toString();
    if (action == view)
    {
        QImage image;
        QString source;
        for (auto v : editor->document.root["textures"].toArray())
            if (v.toObject()["id"] == textureId)
            {
                source = v.toObject()["source"].toString();
                if (!source.isEmpty())
                    image.load(source);
                else
                {
                    auto model = v.toObject()["model"].toString();
                    auto embedded = v.toObject()["embedded"].toString();
                    QString modelSource;
                    for (auto resource : editor->document.root["models"].toArray())
                        if (resource.toObject()["id"] == model)
                            modelSource = resource.toObject()["source"].toString();
                    for (auto asset : editor->cache)
                        for (auto &tex : asset->textures)
                            if (QString::fromStdString(tex.sourcePath) == modelSource + "::" + embedded)
                                image = tex.image;
                }
            }
        if (!image.isNull())
        {
            auto dialog = new QWidget(nullptr, Qt::Window);
            dialog->setAttribute(Qt::WA_DeleteOnClose);
            dialog->setWindowTitle(tr("纹理预览 · %1 × %2").arg(image.width()).arg(image.height()));
            auto layout = new QVBoxLayout(dialog);
            auto preview = new ResultView;
            preview->setImage(image);
            layout->addWidget(preview);
            dialog->resize(640, 480);
            dialog->show();
        }
        return;
    }
    if (action == clear)
    {
        editor->setMaterialField("textures." + slot, QJsonValue());
        return;
    }
    auto path = QFileDialog::getOpenFileName(this, tr("选择纹理"), QString(),
                                             tr("图片 (*.png *.jpg *.jpeg *.bmp *.tga)"));
    if (path.isEmpty())
        return;
    QImageReader reader(path);
    if (reader.read().isNull())
    {
        emit editor->failed(tr("无法读取纹理"));
        return;
    }
    // Add the image and copy each affected material in the same prepared transaction.
    auto d = editor->document;
    auto tex = d.root["textures"].toArray();
    QString id = sceneId();
    tex.append(QJsonObject{{"id", id}, {"source", path}});
    d.root["textures"] = tex;
    auto definitions = d.root["materials"].toArray(), objects = d.root["objects"].toArray();
    QMap<QString, QString> copies;
    for (int i = 0; i < objects.size(); ++i)
    {
        auto o = objects[i].toObject();
        if (!ids.contains(o["id"].toString()))
            continue;
        QString source = o["material"].toString();
        if (!copies.contains(source))
        {
            auto material = findMaterial(d, source);
            QString copy = sceneId();
            material["id"] = copy;
            auto refs = material["textures"].toObject();
            refs[slot] = id;
            material["textures"] = refs;
            definitions.append(material);
            copies[source] = copy;
        }
        o["material"] = copies[source];
        objects[i] = o;
    }
    d.root["objects"] = objects;
    d.root["materials"] = definitions;
    editor->submit(d, tr("更换纹理"), EditorController::Environment);
}
void PerformancePanel::append(const RenderStats &s)
{
    if (!history.isEmpty() && s.seconds < history.last().seconds)
        history.clear();
    history.append(s);
    while (!history.isEmpty() && history.front().seconds < s.seconds - 60)
        history.removeFirst();
    chartDirty = true;
    if (isVisible())
        update();
}
void PerformancePanel::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    const auto pixels = size() * devicePixelRatioF();
    if (chart.size() != pixels || chart.devicePixelRatioF() != devicePixelRatioF())
    {
        chart = QPixmap(pixels);
        chart.setDevicePixelRatio(devicePixelRatioF());
        chartDirty = true;
    }
    if (chartDirty)
    {
        QPainter cached(&chart);
        cached.setFont(font());
        drawChart(cached);
        chartDirty = false;
    }
    p.drawPixmap(0, 0, chart);
}
void PerformancePanel::drawChart(QPainter &p)
{
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), QColor("#1c202a"));
    if (history.isEmpty())
        return;
    auto s = history.last();
    double maxFps = .1, maxTile = 1;
    for (const auto &v : history)
    {
        maxFps = std::max(maxFps, v.rasterActive ? v.rasterFps : v.fps);
        maxTile = std::max(maxTile, v.tileFps);
    }
    auto niceMaximum = [](double value) {
        double unit = std::pow(10., std::floor(std::log10(value)));
        double scaled = value / unit;
        return unit * (scaled <= 1 ? 1 : scaled <= 2 ? 2 : scaled <= 5 ? 5 : 10);
    };
    maxFps = niceMaximum(maxFps);
    maxTile = niceMaximum(maxTile);
    auto label = [](double value, double maximum) {
        int decimals = std::abs(value - std::round(value)) < .00001 ? 0 : maximum < 1 ? 2 : 1;
        return QString::number(value, 'f', decimals);
    };
    int left = std::max(40, p.fontMetrics().horizontalAdvance(label(maxFps, maxFps)) + 14);
    int right = s.tiled ? std::max(40, p.fontMetrics().horizontalAdvance(label(maxTile, maxTile)) + 14) : 10;
    QRectF graph(left, 68, std::max(30, width() - left - right), std::max(16, height() - 178));
    const int divisions = graph.height() < 100 ? 2 : 4;
    for (int i = 0; i <= divisions; ++i)
    {
        double fraction = double(i) / divisions;
        double y = graph.bottom() - fraction * graph.height();
        p.setPen(QColor("#303945"));
        p.drawLine(QPointF(graph.left(), y), QPointF(graph.right(), y));
        p.setPen(QColor("#67bbd4"));
        p.drawText(QRectF(0, y - 8, left - 7, 16), Qt::AlignRight | Qt::AlignVCenter,
                   label(maxFps * fraction, maxFps));
        if (s.tiled)
        {
            p.setPen(QColor("#efa65b"));
            p.drawText(QRectF(graph.right() + 7, y - 8, right - 7, 16), Qt::AlignLeft | Qt::AlignVCenter,
                       label(maxTile * fraction, maxTile));
        }
    }
    p.setPen(QColor("#8594a6"));
    for (int i = 0; i < 3; ++i)
        p.drawText(QRectF(graph.left() + i * graph.width() / 2 - 23, graph.bottom() + 3, 46, 18),
                   Qt::AlignCenter, tr("%1 秒").arg(-60 + i * 30));
    p.save();
    p.setClipRect(graph.adjusted(-1, -1, 1, 1));
    for (int curve = 0; curve < (s.tiled ? 2 : 1); ++curve)
    {
        QPainterPath path;
        for (int i = 0; i < history.size(); ++i)
        {
            const auto &v = history[i];
            QPointF point(graph.right() - (s.seconds - v.seconds) * graph.width() / 60,
                          graph.bottom() - (curve ? v.tileFps / maxTile :
                              (v.rasterActive ? v.rasterFps : v.fps) / maxFps) * graph.height());
            if (i == 0)
                path.moveTo(point);
            else
                path.lineTo(point);
        }
        p.setPen(QPen(curve ? QColor("#efa65b") : QColor("#67bbd4"), 1.5));
        p.drawPath(path);
    }
    p.restore();
    const auto bodyFont = p.font();
    auto metricFont = bodyFont;
    metricFont.setPointSize(19);
    metricFont.setWeight(QFont::DemiBold);
    p.setFont(metricFont);
    p.setPen(QColor("#70c9f0"));
    p.drawText(QRect(14, 8, width() / 2, 32), Qt::AlignVCenter,
               tr("%1 FPS").arg(s.rasterActive ? s.rasterFps : s.fps, 0, 'f', 1));
    p.setFont(bodyFont);
    p.setPen(QColor("#b3aafa"));
    p.drawText(QRect(width() / 2, 10, width() / 2 - 14, 28), Qt::AlignRight | Qt::AlignVCenter,
               tr("块 %1 / s").arg(s.tiled && !s.rasterActive ? QString::number(s.tileFps, 'f', 1) : tr("—")));
    p.setPen(QColor("#8c97ad"));
    p.drawText(QRect(14, 43, width() - 28, 18), s.rasterActive ?
               tr("光栅化完成帧率  ·  最近 60 秒") : tr("整图采样速率  ·  最近 60 秒"));
    p.setPen(QColor("#b4bed0"));
    const QStringList details = {
        s.rasterActive ? tr("光栅化 %1 ms · %2× MSAA").arg(s.rasterMs, 0, 'f', 1).arg(s.rasterSamples) :
            tr("光追 %1 ms · 降噪 %2 ms").arg(s.gpuMs, 0, 'f', 1).arg(s.denoiseMode=="realtime" ? s.realtimeDenoiseMs : s.oidnMs, 0, 'f', 1),
        tr("上传 %1  ·  BLAS %2  ·  TLAS %3 ms").arg(s.uploadMs, 0, 'f', 1).arg(s.blasMs, 0, 'f', 1).arg(s.tlasMs, 0, 'f', 2),
        tr("渲染资源  %1 MiB").arg(s.allocatedBytes / 1048576., 0, 'f', 1),
        s.denoiseMode=="realtime" ? tr("GPU 历史接受 %1% · %2 轮").arg(s.historyAcceptance*100,0,'f',1).arg(s.denoiseRounds) : tr("完成 %1 fps · 发布 %2 fps").arg(s.completedFps,0,'f',1).arg(s.publishedFps,0,'f',1)
    };
    for (int i = 0; i < details.size(); ++i)
        p.drawText(QRect(14, height() - 82 + i * 19, width() - 28, 19),
                   p.fontMetrics().elidedText(details[i], Qt::ElideRight, width() - 28));
}
PreviewSettingsPanel::PreviewSettingsPanel(QWidget *p) : QWidget(p)
{
    setObjectName("previewSettingsPanel");
    auto layout = new QFormLayout(this);
    layout->setContentsMargins(12, 10, 12, 8);
    layout->setSpacing(8);
    layout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    layout->setRowWrapPolicy(QFormLayout::WrapLongRows);
    auto spin = [&](const QString &label, int lo, int hi, int value) {
        auto s = new QSpinBox;
        s->setRange(lo, hi);
        s->setValue(value);
        s->setKeyboardTracking(false);
        layout->addRow(label, s);
        return s;
    };
    samples = spin(tr("预览 spp（0 无限）"), 0, 1000000, 0);
    bounces = spin(tr("预览反弹数"), 1, 64, 4);
    tile = spin(tr("预览块大小"), 16, 1024, 128);
    tiled = new QCheckBox(tr("分块预览"));
    lowResolution = new QCheckBox(tr("降低预览分辨率"));
    lowResolution->setObjectName("previewLowResolution");
    denoise = new QComboBox;
    denoise->addItems({tr("关闭"), tr("GPU 实时"), tr("OIDN")});
    denoise->setObjectName("previewDenoiseMode");
    antialiasing = new QCheckBox(tr("抗锯齿"));
    antialiasing->setObjectName("previewAntialiasing");
    layout->addRow(tiled);
    layout->addRow(lowResolution);
    layout->addRow(tr("预览降噪"), denoise);
    layout->addRow(antialiasing);
    interaction = new QComboBox;
    interaction->addItems({tr("保持路径追踪"), tr("光栅化"), tr("降低分辨率路径追踪")});
    interaction->setObjectName("interactionMode");
    interaction->setToolTip(tr("拖动相机或对象时的预览方式；停手后回到路径追踪继续累积。"));
    layout->addRow(tr("交互期间"), interaction);
    rasterLock = new QCheckBox(tr("锁定光栅化（始终）"));
    rasterLock->setObjectName("rasterLock");
    idle = spin(tr("回到路径追踪延迟 ms"), 50, 2000, 250);
    idle->setObjectName("interactionIdle");
    layout->addRow(rasterLock);
    // 只有用户操作才提交；setValues() 用 QSignalBlocker 同步文档值，不会触发这里。
    auto publish = [this] {
        if (syncing)
            return;
        emit changed(values());
    };
    for (auto s : {samples, bounces, tile, idle})
        connect(s, QOverload<int>::of(&QSpinBox::valueChanged), this, [publish](int) { publish(); });
    for (auto c : {tiled, lowResolution, antialiasing, rasterLock})
        connect(c, &QCheckBox::toggled, this, [publish](bool) { publish(); });
    connect(denoise, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [publish](int) { publish(); });
    connect(interaction, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [publish](int) { publish(); });
}
// setValues() 期间用 syncing 屏蔽提交，避免程序性同步被当成用户编辑再提交一次。
void PreviewSettingsPanel::setValues(const RenderParams::Snapshot &settings)
{
    syncing = true;
    originalSettings = settings;
    QSignalBlocker blockSamples(samples), blockBounces(bounces), blockTile(tile), blockIdle(idle);
    QSignalBlocker blockTiled(tiled), blockLow(lowResolution), blockDenoise(denoise);
    QSignalBlocker blockLock(rasterLock), blockInteraction(interaction), blockAA(antialiasing);
    samples->setValue(settings.maxRenderFrames);
    bounces->setValue(settings.maxBounces);
    tile->setValue(settings.tileSize);
    tiled->setChecked(settings.useTileRendering);
    lowResolution->setChecked(settings.renderLow);
    denoise->setCurrentIndex(int(settings.effectiveDenoiseMode()));
    antialiasing->setChecked(settings.antialiasing);
    interaction->setCurrentIndex(qBound(0, settings.interactionMode, 2));
    rasterLock->setChecked(settings.rasterLocked);
    idle->setValue(settings.interactionIdleMs);
    syncing = false;
}
RenderParams::Snapshot PreviewSettingsPanel::values() const
{
    RenderParams::Snapshot settings = originalSettings;
    settings.maxRenderFrames = samples->value();
    settings.maxBounces = bounces->value();
    settings.tileSize = tile->value();
    settings.useTileRendering = tiled->isChecked();
    settings.renderLow = lowResolution->isChecked();
    settings.denoiseMode = DenoiseMode(denoise->currentIndex());
    settings.denoise = settings.denoiseMode != DenoiseMode::None;
    settings.antialiasing = antialiasing->isChecked();
    settings.interactionMode = interaction->currentIndex();
    settings.rasterLocked = rasterLock->isChecked();
    settings.interactionIdleMs = idle->value();
    return settings;
}
ResultView::ResultView(QWidget *p) : QGraphicsView(p), canvas(this)
{
    setScene(&canvas);
    pixmap = canvas.addPixmap(QPixmap());
    setDragMode(ScrollHandDrag);
    setTransformationAnchor(AnchorUnderMouse);
    setBackgroundBrush(QColor("#12171d"));
    setFrameShape(QFrame::NoFrame);
}
void ResultView::setImage(const QImage &im)
{
    if (im.isNull())
    {
        image = {};
        pixmap->setPixmap({});
        canvas.setSceneRect({});
        viewport()->update();
        return;
    }
    bool resized = image.size() != im.size();
    image = im;
    pixmap->setPixmap(QPixmap::fromImage(im));
    canvas.setSceneRect(pixmap->boundingRect());
    if (fitting || resized)
        fit();
}
void ResultView::fit()
{
    fitting = true;
    fitInView(pixmap, Qt::KeepAspectRatio);
}
void ResultView::actualSize()
{
    fitting = false;
    resetTransform();
    scale(1.0 / devicePixelRatioF(), 1.0 / devicePixelRatioF());
}
void ResultView::wheelEvent(QWheelEvent *e)
{
    fitting = false;
    double factor = std::pow(1.0015, e->angleDelta().y());
    if (transform().m11() * factor > .01 && transform().m11() * factor < 64)
        scale(factor, factor);
}
void ResultView::resizeEvent(QResizeEvent *e)
{
    QGraphicsView::resizeEvent(e);
    if (fitting)
        fit();
}
void ResultView::drawForeground(QPainter *p, const QRectF &)
{
    if (!image.isNull()) return;
    p->save();
    p->resetTransform();
    p->setPen(QColor("#96acc6"));
    p->drawText(viewport()->rect().adjusted(24, 24, -24, -24), Qt::AlignCenter | Qt::TextWordWrap,
                tr("尚无渲染结果\n\n调整构图和输出后加入队列，再点击“运行队列”。\n结果会显示在这里。"));
    p->restore();
}
