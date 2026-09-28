#include "learnQT.h"
#include "WorkbenchStyle.h"
#include "WorkspaceUi.h"
#include <QDir>
#include <QFileInfo>
#include <QMessageBox>
#include <QSettings>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QUuid>

void learnQT::refreshRenderCameras()
{
    if (!m_renderCameraChoice)
        return;
    const QSignalBlocker block(m_renderCameraChoice);
    const auto cameras = editor->document.root["cameras"].toArray();
    const auto selected = m_draftSourceId.isEmpty()
                              ? editor->document.root["activeCameraId"].toString()
                              : m_draftSourceId;
    m_renderCameraChoice->clear();
    for (auto value : cameras)
    {
        auto camera = value.toObject();
        m_renderCameraChoice->addItem(camera["name"].toString(), camera["id"].toString());
    }
    int index = m_renderCameraChoice->findData(selected);
    if (index < 0)
        index = m_renderCameraChoice->findData(editor->document.root["activeCameraId"].toString());
    m_renderCameraChoice->setCurrentIndex(index);
    if (index >= 0 && (m_draftSourceId != m_renderCameraChoice->currentData().toString() ||
                       m_draftCamera.isEmpty()))
    {
        m_draftSourceId = m_renderCameraChoice->currentData().toString();
        m_draftCamera = cameras[index].toObject();
    }
}

void learnQT::saveDraftCamera()
{
    if (m_draftCamera.isEmpty() || m_loading || editor->renderLocked)
        return;
    auto next = editor->document;
    auto cameras = next.root["cameras"].toArray();
    auto camera = m_draftCamera;
    camera["id"] = QUuid::createUuid().toString(QUuid::WithoutBraces);
    camera["name"] = tr("相机 %1").arg(cameras.size() + 1);
    cameras.append(camera);
    next.root["cameras"] = cameras;
    next.root["activeCameraId"] = camera["id"];
    QJsonObject legacy;
    for (auto key : {"position", "target", "up", "fov"})
        legacy[key] = camera[key];
    next.root["camera"] = legacy;
    editor->submit(next, tr("保存构图为新相机"), EditorController::CameraChange);
    m_draftSourceId = camera["id"].toString();
    m_draftCamera = camera;
    refreshRenderCameras();
}

void learnQT::addRenderTask()
{
    if (m_loading || editor->busy || m_draftCamera.isEmpty())
        return;
    RenderJobSettings settings;
    settings.size = QSize(outputWidth->value(), outputHeight->value());
    settings.samples = outputSamples->value();
    settings.tileSize = outputTile->value();
    settings.bounces = outputBounces->value();
    settings.denoise = outputDenoise->isChecked();
    if (!settings.valid())
    {
        QMessageBox::warning(this, tr("输出设置"), tr("请使用有效设置；单张图最多 6710 万像素。"));
        return;
    }
    QueueItem item;
    item.request.id = m_nextQueueId++;
    item.request.document = editor->document;
    QJsonObject legacy;
    for (auto key : {"position", "target", "up", "fov"})
        legacy[key] = m_draftCamera[key];
    item.request.document.root["camera"] = legacy;
    item.request.assets = editor->cache;
    item.request.settings = settings;
    item.request.resourceSignatures = renderResourceSignatures(item.request.document);
    for (auto signature : item.request.resourceSignatures)
        if (signature == QStringLiteral("missing") || signature == QStringLiteral("unreadable"))
        {
            QMessageBox::warning(this, tr("任务资源"),
                                 tr("模型、贴图或环境资源无法读取，不能加入队列。"));
            return;
        }
    item.cameraName = m_renderCameraChoice ? m_renderCameraChoice->currentText() : tr("相机");
    item.name = tr("任务 %1 · %2").arg(item.request.id).arg(item.cameraName);
    item.format = m_renderFormat && m_renderFormat->currentIndex() == 1 ? "jpg" : "png";
    m_renderQueue.append(std::move(item));
    refreshRenderQueue();
    if (workspace && workspace->task)
    {
        const QSignalBlocker block(workspace->task);
        workspace->task->selectRow(m_renderQueue.size() - 1);
    }
    if (m_queueRunning && !m_activeQueueId)
        dispatchRenderTask();
}

void learnQT::runRenderQueue()
{
    if (m_queueRunning)
        return;
    QSettings settings(QSettings::defaultFormat(), QSettings::UserScope, "learnQT", "SceneWorkbench");
    const QString directory = settings.value("workspaceV4/autoExportPath").toString();
    if (directory.isEmpty() || !QFileInfo(directory).isDir() || !QFileInfo(directory).isWritable())
    {
        QMessageBox::warning(this, tr("自动导出路径"),
                             tr("请先在设置页选择可写的自动导出目录。"));
        navigateWorkspace(WorkspacePage::Settings);
        return;
    }
    m_queueOutputDirectory = directory;
    m_queueRunning = true;
    if (!m_queueWorker)
    {
        setRenderPreviewMode(true);
        if (!viewport->renderThread())
        {
            taskLabel->setText(tr("正在准备渲染上下文"));
            return;
        }
    }
    dispatchRenderTask();
}

void learnQT::dispatchRenderTask()
{
    if (!m_queueRunning || m_activeQueueId || !m_queueWorker)
        return;
    const QDir directory(m_queueOutputDirectory);
    for (int row = 0; row < m_renderQueue.size(); ++row)
    {
        auto &item = m_renderQueue[row];
        if (item.status != QStringLiteral("等待中"))
            continue;
        QString base = item.name;
        for (const QChar forbidden : QStringLiteral("<>:\"/\\|?*"))
            base.replace(forbidden, QChar('_'));
        for (int i = 0; i < base.size(); ++i)
            if (base[i].unicode() < 32)
                base[i] = QChar('_');
        base = base.trimmed();
        while (base.endsWith('.'))
            base.chop(1);
        if (base.isEmpty())
            base = tr("任务 %1").arg(item.request.id);
        if (QStringList{"CON", "PRN", "AUX", "NUL", "COM1", "COM2", "COM3", "COM4",
                        "COM5", "COM6", "COM7", "COM8", "COM9", "LPT1", "LPT2", "LPT3",
                        "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"}.contains(base.toUpper()))
            base.prepend('_');
        base = base.left(100);
        QString path = directory.filePath(base + '.' + item.format);
        auto taken = [this, &item](const QString &candidate) {
            if (QFileInfo::exists(candidate))
                return true;
            for (const auto &existing : m_renderQueue)
                if (existing.request.id != item.request.id &&
                    existing.request.outputPath.compare(candidate, Qt::CaseInsensitive) == 0)
                    return true;
            return false;
        };
        for (int number = 2; taken(path); ++number)
            path = directory.filePath(base + '-' + QString::number(number) + '.' + item.format);
        item.request.outputPath = path;
        if (!m_queueWorker->submit(item.request))
        {
            item.status = tr("失败");
            item.error = tr("渲染队列工作线程不可用");
            m_queueRunning = false;
            refreshRenderQueue();
            return;
        }
        m_activeQueueId = item.request.id;
        if (!m_resultBrowsingPinned)
        {
            m_viewedTaskId = m_activeQueueId;
            const QSignalBlocker block(workspace->task);
            workspace->task->selectRow(row);
        }
        item.status = tr("准备中");
        if (viewport->renderThread())
            viewport->renderThread()->setForceRaster(true);
        refreshRenderQueue();
        setRenderPreviewMode(false);
        return;
    }
    m_queueRunning = false;
    if (viewport->renderThread())
        viewport->renderThread()->setForceRaster(false);
    refreshRenderQueue();
}

void learnQT::refreshRenderQueue()
{
    if (!workspace || !workspace->task)
        return;
    auto table = workspace->task;
    const auto selected = table->currentRow() >= 0 && table->item(table->currentRow(), 0)
                              ? table->item(table->currentRow(), 0)->data(Qt::UserRole).toULongLong()
                              : 0;
    QSignalBlocker block(table);
    table->setRowCount(qMax(1, m_renderQueue.size()));
    if (m_renderQueue.isEmpty())
    {
        for (int column = 0; column < 5; ++column)
            table->setItem(0, column, new QTableWidgetItem(column == 0 ? tr("尚无任务") : QStringLiteral("—")));
        return;
    }
    for (int row = 0; row < m_renderQueue.size(); ++row)
    {
        const auto &item = m_renderQueue[row];
        const QStringList cells{item.name,
                                QString("%1 × %2").arg(item.request.settings.size.width())
                                    .arg(item.request.settings.size.height()),
                                QString("%1 / %2 spp").arg(item.samples).arg(item.request.settings.samples),
                                item.status,
                                QString::number(item.seconds, 'f', 1) + tr(" 秒")};
        for (int column = 0; column < cells.size(); ++column)
        {
            auto cell = new QTableWidgetItem(cells[column]);
            if (column == 0)
                cell->setData(Qt::UserRole, QVariant::fromValue<qulonglong>(item.request.id));
            table->setItem(row, column, cell);
        }
        if (item.request.id == selected)
            table->selectRow(row);
    }
}

void learnQT::showRenderTaskResult()
{
    if (!workspace || !workspace->task)
        return;
    const int row = workspace->task->currentRow();
    if (row < 0 || row >= m_renderQueue.size())
        return;
    m_viewedTaskId = m_renderQueue[row].request.id;
    m_resultBrowsingPinned = true;
    resultView->setImage(m_renderQueue[row].result);
    setRenderPreviewMode(false);
}

void learnQT::setRenderPreviewMode(bool preview)
{
    m_renderPreviewMode = preview;
    if (!preview && m_viewedTaskId)
        for (const auto &item : m_renderQueue)
            if (item.request.id == m_viewedTaskId)
            {
                resultView->setImage(item.result);
                break;
            }
    if (workspace && workspace->page == int(WorkspacePage::Render))
    {
        if (!m_draftCamera.isEmpty())
        {
            Camera draft;
            draft.restoreState(sceneVector(m_draftCamera["position"]), sceneVector(m_draftCamera["target"]),
                               sceneVector(m_draftCamera["up"]), m_draftCamera["fov"].toDouble());
            viewport->setCompositionMode(true, draft, QSize(outputWidth->value(), outputHeight->value()));
        }
        const QSignalBlocker block(views);
        views->setCurrentIndex(preview ? 0 : 1);
        if (viewport->renderThread())
            viewport->renderThread()->setPreviewVisible(preview);
    }
}
