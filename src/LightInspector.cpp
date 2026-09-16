#include "WorkbenchPanels.h"
#include "UiDiagnostics.h"
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QVBoxLayout>
LightInspector::LightInspector(EditorController *e, QWidget *p) : QWidget(p), editor(e)
{
    auto layout = new QVBoxLayout(this);
    auto form = new QFormLayout;
    layout->addLayout(form);
    list = new QComboBox;
    form->addRow(tr("灯光"), list);
    connect(list, QOverload<int>::of(&QComboBox::activated), this, [this] { refresh(); });
    name = new QLineEdit;
    form->addRow(tr("名称"), name);
    connect(name, &QLineEdit::editingFinished, this, [this] {
        if (restoring)
            return;
        auto d = editor->document;
        auto lights = d.root["lights"].toArray();
        for (int i = 0; i < lights.size(); ++i)
        {
            auto l = lights[i].toObject();
            if (l["id"].toString() == list->currentData().toString())
            {
                l["name"] = name->text();
                lights[i] = l;
            }
        }
        d.root["lights"] = lights;
        editor->submit(d, tr("灯光名称"), EditorController::Organization);
    });
    for (int i = 0; i < 7; ++i)
    {
        values[i] = new MixedSpin;
        values[i]->setRange(i < 3 ? -1e8 : 0, 1e8);
        form->addRow(QStringList{tr("位置 / 方向 X"), tr("位置 / 方向 Y"), tr("位置 / 方向 Z"),
                                 tr("辐亮度 R"), tr("辐亮度 G"), tr("辐亮度 B"),
                                 tr("半径 / 太阳半角 rad")}[i],
                     values[i]);
        connect(values[i], QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this, i](double v) {
            if (!restoring)
                edit(i, v);
        });
    }
    auto buttons = new QHBoxLayout;
    layout->addLayout(buttons);
    for (bool sun : {false, true})
    {
        auto b = new QPushButton(sun ? tr("添加太阳") : tr("添加球光"));
        b->setObjectName(sun ? "addSunLight" : "addSphereLight");
        buttons->addWidget(b);
        connect(b, &QPushButton::clicked, this, [this, sun] {
            auto d = editor->document;
            auto lights = d.root["lights"].toArray();
            lights.append(QJsonObject{
                {"id", sceneId()},
                {"name", sun ? tr("太阳") : tr("球形光")},
                {"type", sun ? "sun" : "sphere"},
                {"radius", sun ? .01 : .1},
                {sun ? "direction" : "position", sun ? QJsonArray{-1, -1, -1} : QJsonArray{0, 3, 0}},
                {"radiance", sun ? QJsonArray{1000, 1000, 1000} : QJsonArray{50, 50, 50}}});
            d.root["lights"] = lights;
            editor->submit(d, tr("添加灯光"), EditorController::Lighting);
            list->setCurrentIndex(list->count() - 1);
            refresh();
        });
    }
    auto remove = new QPushButton(tr("删除灯光"));
    layout->addWidget(remove);
    connect(remove, &QPushButton::clicked, this, [this] {
        auto d = editor->document;
        QJsonArray kept;
        for (auto v : d.root["lights"].toArray())
            if (v.toObject()["id"].toString() != list->currentData().toString())
                kept.append(v);
        d.root["lights"] = kept;
        editor->submit(d, tr("删除灯光"), EditorController::Lighting);
    });
    connect(editor, &EditorController::changed, this, [this](int) {
        UiSlotTimer timer(UiSlotLightInspector);
        refresh();
    });
    refresh();
}
void LightInspector::refresh()
{
    restoring = true;
    auto id = list->currentData().toString();
    list->clear();
    for (auto v : editor->document.root["lights"].toArray())
    {
        auto l = v.toObject();
        list->addItem(l["name"].toString(l["id"].toString()), l["id"].toString());
    }
    int index = list->findData(id);
    if (index >= 0)
        list->setCurrentIndex(index);
    QJsonObject light;
    for (auto v : editor->document.root["lights"].toArray())
        if (v.toObject()["id"].toString() == list->currentData().toString())
            light = v.toObject();
    bool enabled = !light.isEmpty();
    name->setEnabled(enabled);
    name->setText(light["name"].toString(light["id"].toString()));
    auto p = sceneVector(light[light["type"].toString() == "sun" ? "direction" : "position"]),
         c = sceneVector(light["radiance"]);
    for (int i = 0; i < 7; ++i)
    {
        values[i]->setEnabled(enabled);
        values[i]->showValue(i < 3 ? p[i] : i < 6 ? c[i - 3] : light["radius"].toDouble());
    }
    restoring = false;
}
void LightInspector::edit(int field, double value)
{
    auto d = editor->document;
    auto lights = d.root["lights"].toArray();
    for (int i = 0; i < lights.size(); ++i)
    {
        auto l = lights[i].toObject();
        if (l["id"].toString() != list->currentData().toString())
            continue;
        if (field == 6)
            l["radius"] = value;
        else
        {
            QString key = field < 3 ? (l["type"].toString() == "sun" ? "direction" : "position") : "radiance";
            auto v = sceneVector(l[key]);
            v[field % 3] = value;
            l[key] = jsonVector(v);
        }
        lights[i] = l;
    }
    d.root["lights"] = lights;
    editor->submit(d, tr("调整灯光"), EditorController::Lighting);
}
