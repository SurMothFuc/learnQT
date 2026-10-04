#include "WorkbenchPanels.h"
#include "UiDiagnostics.h"
#include "MaterialUi.h"
#include <QSettings>
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
#include <QDebug>
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
bool MixedSpin::hasUncommittedText() const
{
    return hasFocus() && lineEdit()->isModified();
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
    auto materialBox = new QWidget;
    materialSection = materialBox;
    materialBox->setObjectName("materialSection");
    auto materialLayout = new QVBoxLayout(materialBox);
    materialLayout->setContentsMargins(0, 0, 0, 0);
    materialLayout->setSpacing(6);
    materialList = new QComboBox;
    materialList->setObjectName("materialScope");
    materialSlotLabel = new QLabel(tr("编辑范围"));
    materialLayout->addWidget(materialSlotLabel);
    materialLayout->addWidget(materialList);
    connect(materialList, QOverload<int>::of(&QComboBox::activated), this, [this] { refresh(); });
    selectMaterialUsers = new QPushButton(tr("选择使用者"));
    selectMaterialUsers->setObjectName("selectMaterialUsers");
    layout->insertWidget(1, selectMaterialUsers);
    connect(selectMaterialUsers, &QPushButton::clicked, this, [this] {
        QSet<QString> ids;
        for (auto v : editor->document.root["objects"].toArray())
            if (v.toObject()["material"].toString() == previewMaterialId())
                ids.insert(v.toObject()["id"].toString());
        if (!ids.isEmpty()) editor->select(ids);
    });
    QMap<QString, QFormLayout *> forms;
    QSettings prefs(QSettings::defaultFormat(), QSettings::UserScope, "learnQT", "SceneWorkbench");
    const QStringList groupKeys{"surface", "alpha", "normal", "emission", "coat", "detail", "medium"};
    const QStringList groupTitles{tr("表面"), tr("透明与折射"), tr("法线"), tr("发光"), tr("清漆"), tr("织物与高级反射"), tr("内部介质")};
    for (int i = 0; i < groupKeys.size(); ++i) {
        const auto key = groupKeys[i];
        auto heading = new QToolButton;
        heading->setObjectName("materialGroup_" + key);
        heading->setCheckable(true);
        heading->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        heading->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        heading->setMinimumHeight(32);
        heading->setStyleSheet("QToolButton { text-align: left; background: #1d2938; border: 1px solid #334156; border-radius: 4px; padding: 4px 8px; } QToolButton:hover { background: #26364b; } QToolButton:checked { background: #1d2938; border-left: 2px solid #5596ec; }");
        heading->setText(groupTitles[i]);
        auto body = new QWidget;
        auto f = new QFormLayout(body);
        f->setContentsMargins(4, 6, 4, 8);
        f->setSpacing(8);
        f->setRowWrapPolicy(QFormLayout::WrapLongRows);
        f->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        bodies[key] = body; sections[key] = heading; forms[key] = f; titles[key] = groupTitles[i];
        const auto pref = "workspaceV5/materialEditor/groups/" + key;
        bool expanded = prefs.value(pref, key == "surface").toBool();
        if (prefs.contains(pref)) initializedSections.insert(key);
        heading->setChecked(expanded); body->setVisible(expanded);
        heading->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
        connect(heading, &QToolButton::toggled, this, [this, body, heading, key, pref](bool on) {
            body->setVisible(on); heading->setArrowType(on ? Qt::DownArrow : Qt::RightArrow);
            initializedSections.insert(key);
            QSettings p(QSettings::defaultFormat(), QSettings::UserScope, "learnQT", "SceneWorkbench");
            p.setValue(pref, on);
        });
        materialLayout->addWidget(heading); materialLayout->addWidget(body);
    }
    auto addRow = [&](QString group, QString key, QString label, QWidget *control) {
        auto row = new QWidget; row->setObjectName(key + "Row");
        auto box = new QVBoxLayout(row); box->setContentsMargins(0,0,0,0); box->setSpacing(4);
        box->addWidget(control);
        forms[group]->addRow(label, row);
        rows[key] = row; rowLabels[key] = forms[group]->labelForField(row);
        rowLabels[key]->setObjectName(key + "Label");
        rowLabels[key]->setProperty("materialLabel", label);
    };
    auto addTexture = [&](QString key, QString rowKey) {
        auto button = new QPushButton(tr("添加贴图…"));
        button->setObjectName("texture_" + key);
        button->setIconSize(QSize(28,28)); button->setMinimumHeight(34);
        button->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        textures[key] = button;
        rows[rowKey]->layout()->addWidget(button);
        connect(button, &QPushButton::clicked, this, [this,key] { chooseTexture(key); });
    };
    auto addColor = [&](QString group, QString key, QString label) {
        auto b = new QPushButton(tr("选择颜色…")); b->setObjectName(key);
        b->setIconSize(QSize(32,18));
        addRow(group,key,label,b);
        connect(b, &QPushButton::clicked, this, [this,key] { chooseColor(key); });
        return b;
    };
    color = addColor("surface", "baseColor", tr("基础色"));
    emission = addColor("emission", "emissive", tr("颜色"));
    mediumColor = addColor("medium", "mediumColor", tr("颜色"));
    const QStringList names{"metallic","roughness","IOR","alphaMode","opacity","alphaCutoff","transmission",
        "normalScale","normalMapFlipY","emissionStrength","clearcoat","clearcoatGloss","sheen","sheenTint",
        "specularTint","anisotropic","subsurface","mediumtype","mediumDensity","mediumAnisotropy"};
    const QStringList labels{tr("金属度"),tr("粗糙度"),tr("折射率"),tr("透明模式"),tr("不透明度"),tr("裁切阈值"),tr("透射"),
        tr("强度"),tr("绿色通道"),tr("强度"),tr("强度"),tr("光泽"),tr("绒光"),tr("绒光染色"),
        tr("高光染色"),tr("各向异性"),tr("次表面近似"),tr("类型"),tr("密度"),tr("散射方向性")};
    const QStringList groups{"surface","surface","surface","alpha","alpha","alpha","alpha",
        "normal","normal","emission","coat","coat","detail","detail","detail","detail","detail","medium","medium","medium"};
    for (int i = 0; i < names.size(); ++i) {
        const auto key = names[i];
        if (key == "alphaMode" || key == "mediumtype" || key == "normalMapFlipY") {
            auto b = new QComboBox; b->setObjectName(key); b->setPlaceholderText(tr("多个值"));
            const QStringList options = key == "alphaMode" ? QStringList{tr("不透明"),tr("透明边界（兼容）"),tr("裁切 Mask"),tr("混合 Blend")}
                : key == "mediumtype" ? QStringList{tr("无介质"),tr("吸收"),tr("散射"),tr("发光介质")}
                : QStringList{tr("原方向"),tr("翻转绿色通道")};
            for (int j = 0; j < options.size(); ++j) b->addItem(options[j], j);
            choices[key] = b; addRow(groups[i],key,labels[i],b);
            connect(b, QOverload<int>::of(&QComboBox::activated), this, [this,b,key](int index) {
                if (!restoring && index >= 0) commitMaterial(key, key == "normalMapFlipY" ? QJsonValue(index != 0) : QJsonValue::fromVariant(b->itemData(index)));
            });
            continue;
        }
        auto spin = new MixedSpin; fields[key] = spin; spin->setObjectName(key);
        spin->setRange(key == "IOR" ? 1 : key == "anisotropic" || key == "mediumAnisotropy" ? -.99 : 0,
            key == "IOR" ? 10 : key == "normalScale" || key == "mediumDensity" ? 1000 : key == "emissionStrength" ? 1000000 : 1);
        spin->setSingleStep(.01); spin->setMinimumWidth(88);
        spin->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        spin->setButtonSymbols(QAbstractSpinBox::NoButtons);
        auto line = new QWidget; auto h = new QHBoxLayout(line); h->setContentsMargins(0,0,0,0); h->setSpacing(6);
        if (spin->maximum() == 1) {
            auto slider = new QSlider(Qt::Horizontal); slider->setObjectName(key + "Slider");
            slider->setRange(qRound(spin->minimum()*1000),1000); slider->setMinimumWidth(50);
            sliders[key] = slider; h->addWidget(slider,1);
            connect(slider, &QSlider::valueChanged, this, [this,spin](int v) { if (!restoring) spin->setValue(v/1000.); });
            connect(slider, &QSlider::sliderReleased, this, [this] { ++epoch; });
        }
        h->addWidget(spin,1); addRow(groups[i],key,labels[i],line);
        connect(spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this,key,i](double v) {
            if (!restoring) commitMaterial(key,v,epoch*100+i+20);
        });
        connect(spin, &QDoubleSpinBox::editingFinished, this, [this] { ++epoch; });
    }
    for (const auto key : {"baseColor","metallic","roughness","emissive","opacity"}) addTexture(key,key);
    auto normal = new QLabel(tr("用贴图表现凹凸，不改变几何轮廓")); normal->setWordWrap(true);
    addRow("normal","normalTexture",tr("法线贴图"),normal); addTexture("normal","normalTexture");
    // Put the normal texture before the controls it enables.
    forms["normal"]->removeWidget(rows["normalTexture"]); forms["normal"]->removeWidget(rowLabels["normalTexture"]);
    forms["normal"]->insertRow(0,rowLabels["normalTexture"],rows["normalTexture"]);
    for (const auto pair : {qMakePair(QString("alpha"),tr("Alpha 控制表面覆盖；透射控制玻璃折射。透明边界用于直穿的介质边界。")),
                           qMakePair(QString("detail"),tr("次表面仅为表面近似，不计算真实内部散射。")),
                           qMakePair(QString("medium"),tr("体积效果需要闭合区域；密度与模型尺寸共同决定效果。"))}) {
        auto hint = new QLabel(pair.second); hint->setWordWrap(true); hint->setObjectName("muted");
        forms[pair.first]->addRow(hint);
    }
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
    connect(editor, &EditorController::changed, this, [this](int change) {
        UiSlotTimer timer(UiSlotObjectInspector);
        if (change == EditorController::Environment && !pendingTextureObject.isEmpty()) {
            if (browsedMaterial == pendingTextureScope && editor->selectedModels().contains(pendingTextureObject))
                browsedMaterial = editor->node(pendingTextureObject)["material"].toString();
            pendingTextureObject.clear(); pendingTextureScope.clear();
        }
        refresh();
    });
    connect(editor, &EditorController::failed, this, [this] { pendingTextureObject.clear(); pendingTextureScope.clear(); });
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
        updateMaterialUi({}, false);
        if (materialRefreshed) materialRefreshed();
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
    updateMaterialUi(definitions, enabled);
    if (materialRefreshed) materialRefreshed();
    restoring = false;
}
void ObjectInspector::setMaterialPage(bool enabled)
{
    transformSection->setVisible(!enabled);
    materialSection->setVisible(enabled);
    materialOverview->setVisible(!enabled);
    if (!enabled) browsedMaterial.clear();
    materialList->setVisible(browsedMaterial.isEmpty() && editor->selectedModels().size() > 1);
    materialSlotLabel->setVisible(browsedMaterial.isEmpty() && editor->selectedModels().size() > 1);
    refresh();
}
void ObjectInspector::browseMaterial(const QString &id)
{
    browsedMaterial = id;
    if (id.isEmpty()) {
        QSignalBlocker block(materialList);
        materialList->setCurrentIndex(0);
    }
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
QString ObjectInspector::previewMaterialId() const
{
    if (!browsedMaterial.isEmpty()) return findMaterial(editor->document, browsedMaterial).isEmpty() ? QString() : browsedMaterial;
    if (!materialList->currentData().toString().isEmpty()) return materialList->currentData().toString();
    QSet<QString> ids;
    for (auto id : editor->selectedModels()) ids.insert(editor->node(id)["material"].toString());
    return ids.size() == 1 ? *ids.begin() : QString();
}
QStringList ObjectInspector::materialTargets(const QString &field) const
{
    QStringList result;
    if (editor->busy || editor->renderLocked) return result;
    for (auto id : editor->selectedModels(true)) {
        auto material = editor->node(id)["material"].toString();
        if ((!materialId().isEmpty() && material != materialId()) ||
            !materialFieldApplicable(findMaterial(editor->document, material), field)) continue;
        result << id;
    }
    return result;
}
void ObjectInspector::commitMaterial(const QString &field, const QJsonValue &value, int key)
{
    const auto ids = materialTargets(field);
    const bool follow = !browsedMaterial.isEmpty() && !ids.isEmpty();
    editor->setMaterialFields(QJsonObject{{field, value}}, ids, key);
    if (follow) browseMaterial(editor->node(ids.front())["material"].toString());
    else refresh();
}
QImage ObjectInspector::textureImage(const QString &id, QString *name) const
{
    for (auto v : editor->document.root["textures"].toArray()) {
        auto def = v.toObject(); if (def["id"].toString() != id) continue;
        const auto source = def["source"].toString();
        if (name) *name = source.isEmpty() ? tr("内嵌贴图") : QFileInfo(source).fileName();
        if (!source.isEmpty()) {
            const QFileInfo info(source);
            const auto key = source + QString::number(info.lastModified().toMSecsSinceEpoch()) + QString::number(info.size());
            if (!textureThumbnails.contains(key)) {
                QImageReader reader(source); reader.setScaledSize(QSize(48,48));
                if (textureThumbnails.size() > 64) textureThumbnails.clear();
                textureThumbnails[key] = reader.read();
            }
            return textureThumbnails.value(key);
        }
        QString model;
        for (auto m : editor->document.root["models"].toArray())
            if (m.toObject()["id"] == def["model"]) model = m.toObject()["source"].toString();
        for (auto asset : editor->cache) for (const auto &t : asset->textures)
            if (QString::fromStdString(t.sourcePath) == model + "::" + def["embedded"].toString()) {
                const auto key = QString::fromStdString(t.sourcePath) + QString::number(t.image.cacheKey());
                if (!textureThumbnails.contains(key)) {
                    if (textureThumbnails.size() > 64) textureThumbnails.clear();
                    textureThumbnails[key] = t.image.scaled(48,48,Qt::KeepAspectRatio,Qt::SmoothTransformation);
                }
                return textureThumbnails.value(key);
            }
    }
    return {};
}
void ObjectInspector::updateMaterialUi(const QList<QJsonObject> &defs, bool enabled)
{
    if (materialSection->isHidden()) { selectMaterialUsers->hide(); return; }
    const auto preview = previewMaterialId();
    auto selected = findMaterial(editor->document, preview);
    if (!defs.isEmpty()) {
        const auto reason = enabled ? tr("影响 %1 个所选对象 · 共享材质按需隔离").arg(materialTargets(QString()).size()) :
            editor->renderLocked ? tr("只读 · 正式任务锁定编辑") :
            !editor->selectedModels().isEmpty() && editor->selectedModels(true).isEmpty() ? tr("只读 · 对象已锁定") : tr("只读 · 选择使用者后可编辑");
        summary->setText((preview.isEmpty() ? tr("批量编辑 · %1 个材质").arg(defs.size()) : materialDisplayName(editor->document,preview)) + "\n" + reason);
    }
    selectMaterialUsers->setVisible(!enabled && !preview.isEmpty());
    selectMaterialUsers->setEnabled(!preview.isEmpty() && !editor->busy && !editor->renderLocked);
    materialList->setVisible(browsedMaterial.isEmpty() && defs.size() > 1);
    materialSlotLabel->setVisible(materialList->isVisibleTo(materialSection));
    auto relevant = [&](const QString &key) {
        QList<QJsonObject> subset;
        for (auto d : defs) if (materialFieldApplicable(d, key)) subset << d;
        if (rows.contains(key)) {
            rows[key]->setVisible(!subset.isEmpty()); rowLabels[key]->setVisible(!subset.isEmpty());
            if (auto label = qobject_cast<QLabel *>(rowLabels[key]))
                label->setText(label->property("materialLabel").toString() +
                    (!subset.isEmpty() && subset.size() != defs.size() ? tr("（部分）") : QString()));
            rows[key]->setToolTip(subset.size() != defs.size() ? tr("仅应用于启用此功能的 %1 个材质").arg(subset.size()) : QString());
        }
        return subset;
    };
    for (auto it = choices.begin(); it != choices.end(); ++it) {
        auto values = relevant(it.key()); int value = -1;
        if (!values.isEmpty()) {
            value = int(materialValue(values.front(), it.key()));
            for (auto d : values) if (int(materialValue(d,it.key())) != value) { value = -1; break; }
        }
        const QSignalBlocker block(it.value()); it.value()->setCurrentIndex(it.value()->findData(value));
    }
    for (auto it = fields.begin(); it != fields.end(); ++it) {
        auto values = relevant(it.key()); if (values.isEmpty()) continue;
        double first = materialValue(values.front(), it.key()); bool mixed = false;
        for (auto d : values) if (std::abs(materialValue(d,it.key())-first) > 1e-7) mixed = true;
        if (!it.value()->hasUncommittedText()) it.value()->showValue(first,mixed);
        if (sliders.contains(it.key())) {
            auto slider = sliders[it.key()]; QSignalBlocker block(slider);
            slider->setValue(qRound(first*1000)); slider->setEnabled(enabled);
            slider->setToolTip(mixed ? tr("多个值 · 拖动以统一设置") : QString());
        }
    }
    for (auto pair : QVector<QPair<QString,QPushButton *>>{{"baseColor",color},{"emissive",emission},{"mediumColor",mediumColor}}) {
        auto values = relevant(pair.first); if (values.isEmpty()) continue;
        auto c = sceneVector(values.front()[pair.first]); if (pair.first == "emissive") c = editor->materialEmissionHue(values.front()["id"].toString());
        bool mixed = false;
        for (auto d : values) {
            auto other = sceneVector(d[pair.first]); if (pair.first == "emissive") other = editor->materialEmissionHue(d["id"].toString());
            if (other != c) mixed = true;
        }
        QPixmap swatch(32,18); swatch.fill(mixed ? QColor(110,120,135) : materialDisplayColor(c));
        pair.second->setIcon(QIcon(swatch)); pair.second->setText(mixed ? tr("多个值…") : tr("选择颜色…"));
        pair.second->setToolTip(tr("线性 RGB：%1, %2, %3").arg(c.x()).arg(c.y()).arg(c.z()));
    }
    for (auto it = textures.begin(); it != textures.end(); ++it) {
        QList<QJsonObject> values;
        for (auto d : defs) if (materialFieldApplicable(d,"textures."+it.key())) values << d;
        it.value()->setVisible(!values.isEmpty()); if (values.isEmpty()) continue;
        it.value()->setEnabled(true);
        QString id = values.front()["textures"].toObject()[it.key()].toString(); bool mixed = false;
        for (auto d : values) if (d["textures"].toObject()[it.key()].toString() != id) mixed = true;
        QString name; auto image = mixed ? QImage() : textureImage(id,&name);
        it.value()->setIcon(image.isNull() ? QIcon() : QIcon(QPixmap::fromImage(image.scaled(28,28,Qt::KeepAspectRatio,Qt::SmoothTransformation))));
        it.value()->setText(mixed ? tr("多个贴图…") : id.isEmpty() ? tr("＋ 添加贴图…") : name.left(20) + " ▾");
        it.value()->setToolTip(id.isEmpty() ? tr("常量 × 贴图；点击添加图片") : name + "\n" + tr("点击查看、更换或清除；常量与贴图相乘"));
    }
    relevant("normalTexture");
    for (auto it = sections.begin(); it != sections.end(); ++it) {
        bool active = it.key() == "surface";
        for (auto d : defs) {
            auto tex = d["textures"].toObject();
            if (it.key() == "alpha") active |= d["alphaMode"].toInt()!=Opaque || d["transmission"].toDouble()>0;
            if (it.key() == "normal") active |= !tex["normal"].toString().isEmpty();
            if (it.key() == "emission") active |= materialValue(d,"emissionStrength")>0;
            if (it.key() == "coat") active |= d["clearcoat"].toDouble()>0;
            if (it.key() == "detail") active |= d["sheen"].toDouble()>0 || d["subsurface"].toDouble()>0 || d["anisotropic"].toDouble()!=0 || d["specularTint"].toDouble()>0;
            if (it.key() == "medium") active |= d["mediumtype"].toInt()!=None;
        }
        it.value()->setText(titles[it.key()] + (active ? QString() : tr(" · 未启用")));
        if (!defs.isEmpty() && !initializedSections.contains(it.key())) {
            initializedSections.insert(it.key()); QSignalBlocker block(it.value());
            it.value()->setChecked(active); it.value()->setArrowType(active ? Qt::DownArrow : Qt::RightArrow); bodies[it.key()]->setVisible(active);
        }
    }
}
void ObjectInspector::chooseColor(const QString &field)
{
    auto ids = materialTargets(field);
    if (ids.isEmpty()) return;
    auto m = findMaterial(editor->document, editor->node(ids.front())["material"].toString());
    auto c = sceneVector(m[field]);
    if (field == "emissive") c = editor->materialEmissionHue(m["id"].toString());
    auto chosen = QColorDialog::getColor(materialDisplayColor(c), this, tr("选择颜色"), QColorDialog::DontUseNativeDialog);
    if (chosen.isValid()) commitMaterial(field == "emissive" ? "emissionColor" : field, jsonVector(materialLinearColor(chosen)));
}
void ObjectInspector::chooseTexture(const QString &slot)
{
    editor->materialScope = materialId();
    auto ids = materialTargets("textures." + slot);
    auto mat = findMaterial(editor->document, ids.isEmpty() ? previewMaterialId() : editor->node(ids.front())["material"].toString());
    auto textureId = mat["textures"].toObject()[slot].toString();
    QMenu menu;
    auto view = menu.addAction(tr("查看纹理"));
    auto change = menu.addAction(tr("更换纹理…"));
    auto clear = menu.addAction(tr("清除纹理"));
    view->setEnabled(!textureId.isEmpty());
    change->setEnabled(!ids.isEmpty()); clear->setEnabled(!ids.isEmpty() && !textureId.isEmpty());
    auto action = menu.exec(QCursor::pos());
    if (!action)
        return;
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
    if (ids.isEmpty()) return;
    if (action == clear)
    {
        commitMaterial("textures." + slot, QJsonValue());
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
    if (!browsedMaterial.isEmpty()) { pendingTextureObject = ids.front(); pendingTextureScope = browsedMaterial; }
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
        s.rasterActive ? tr("光栅化 %1 ms").arg(s.rasterMs, 0, 'f', 1) :
            tr("光追 %1 ms  ·  OIDN %2 ms").arg(s.gpuMs, 0, 'f', 1).arg(s.oidnMs, 0, 'f', 1),
        tr("上传 %1  ·  BLAS %2  ·  TLAS %3 ms").arg(s.uploadMs, 0, 'f', 1).arg(s.blasMs, 0, 'f', 1).arg(s.tlasMs, 0, 'f', 2),
        tr("渲染资源  %1 MiB").arg(s.allocatedBytes / 1048576., 0, 'f', 1),
        tr("历史 %1 ms  ·  合成 %2 ms").arg(s.gpuHistoryMs, 0, 'f', 1).arg(s.gpuCompositeMs, 0, 'f', 1)
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
    denoise = new QCheckBox(tr("预览降噪"));
    layout->addRow(tiled);
    layout->addRow(lowResolution);
    layout->addRow(denoise);
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
    for (auto c : {tiled, lowResolution, denoise, rasterLock})
        connect(c, &QCheckBox::toggled, this, [publish](bool) { publish(); });
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
    QSignalBlocker blockLock(rasterLock), blockInteraction(interaction);
    samples->setValue(settings.maxRenderFrames);
    bounces->setValue(settings.maxBounces);
    tile->setValue(settings.tileSize);
    tiled->setChecked(settings.useTileRendering);
    lowResolution->setChecked(settings.renderLow);
    denoise->setChecked(settings.denoise);
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
    settings.denoise = denoise->isChecked();
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
