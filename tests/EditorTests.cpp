#include "EditorController.h"
#include "SceneTreeModel.h"
#include "RenderRateTracker.h"
#include <QApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryDir>
#include <iostream>
#include <random>
#include <stdexcept>
namespace
{
void require(bool condition, const char *message)
{
    if (!condition)
    {
        std::cout << "FAILED: " << message << std::endl;
        throw std::runtime_error(message);
    }
}
void wait(EditorController &editor)
{
    QElapsedTimer time;
    time.start();
    while (editor.busy && time.elapsed() < 10000)
    {
        QCoreApplication::processEvents();
        QThread::msleep(1);
    }
    require(!editor.busy, "Background scene preparation timed out");
}
} // namespace
void testEditor()
{
    std::cout << "Camera: repeated wheel dolly and rate measurement" << std::endl;
    for (auto target : {QVector3D(), QVector3D(12, -4, 9), QVector3D(1e6f, -2e6f, 3e6f)})
    {
        Camera camera;
        camera.restoreState(target + QVector3D(3, 2, 5), target, QVector3D(0, 1, 0), 45);
        const auto front = camera.front;
        for (int i = 0; i < 2000; ++i)
        {
            camera.processMouseScroll(120);
            require(camera.r > 0 && QVector3D::dotProduct(front, camera.front) > .99f,
                    "Wheel zoom crossed or collapsed the orbit target");
            require(camera.target == target && std::isfinite(camera.position.x()), "Wheel moved target or overflowed");
        }
        float nearDistance = camera.r;
        camera.processMouseScroll(-120);
        require(camera.r > nearDistance, "Zoom cannot leave its near limit");
        camera.processMouseScroll(1e30f);
        camera.processMouseScroll(-1e30f);
        require(std::isfinite(camera.r) && camera.r > 0, "Extreme wheel input overflowed");
    }
    RenderRateTracker rates;
    rates.observe(0, 0, 0, 1, true);
    for (int i = 1; i <= 2000; ++i)
    {
        double time = i * .01;
        rates.observe(time, i / 80, i, 1, true);
        if (i >= 400 && i % 20 == 0)
            require(std::abs(rates.fps(time) - 1.25) < .001 && std::abs(rates.tileFps(time) - 100) < .001,
                    "Steady complete-round FPS oscillates at UI refresh intervals");
    }
    require(rates.fps(25) < .5, "FPS does not decay during a real stall");
    rates.observe(25, 25, 2000, 1, false);
    require(rates.fps(25) == 0 && rates.tileFps(25) == 0, "Paused work reports throughput");
    rates.observe(26, 0, 0, 2, true);
    rates.observe(27, 1, 100, 2, true);
    require(std::abs(rates.fps(27) - 1) < .001, "New accumulation reused old throughput");
    std::cout << "Editor: empty scene, root and groups" << std::endl;
    QTemporaryDir temporary;
    require(temporary.isValid(), "Temporary editor fixture failed");
    QString path = temporary.path() + "/mesh.obj";
    QFile file(path);
    require(file.open(QIODevice::WriteOnly), "Cannot write editor fixture");
    file.write("v -1 -1 0\nv 1 -1 0\nv 0 1 0\nv 0 0 1\nf 1 2 3\nf 1 2 4\n");
    file.close();
    QString error;
    auto empty = Scene::prepareScene(QString(), false, error);
    require(bool(empty), "Empty scene preparation failed");
    require(empty->instances.empty() && empty->tlas.size() == 1, "Empty scene has geometry");
    EditorController editor;
    editor.install(*empty);
    SceneTreeModel tree(&editor);
    QString failure;
    QObject::connect(&editor, &EditorController::failed, [&](QString e) { failure = e; });
    editor.rename("root", "工作场景");
    require(editor.node("root")["name"] == "工作场景", "Root rename failed");
    editor.remove({"root"}, true);
    require(!editor.node("root").isEmpty(), "Root was deleted");
    require(!editor.move({"root"}, "root"), "Root moved");
    editor.createGroup("root");
    QString group = editor.active;
    editor.createGroup(group);
    QString nested = editor.active;
    require(!editor.move({group}, nested), "Cycle reparent accepted");
    editor.select({group}, group);
    editor.importFiles({path, path});
    wait(editor);
    require(failure.isEmpty(), "Batch import failed");
    require(editor.document.root["objects"].toArray().size() == 2, "Batch import replaced scene");
    std::cout << "Editor: cached instances and materials" << std::endl;
    auto prepared = Scene::prepareDocument(editor.document, error, editor.cache);
    require(bool(prepared), "Cached preparation failed");
    require(prepared->blasBuildCount == 0, "Cached import rebuilt BLAS");
    require(prepared->meshes.size() == 1 && prepared->instances.size() == 2,
            "Repeated meshes do not share geometry");
    QString a = prepared->instances[0].id, b = prepared->instances[1].id;
    require(editor.node(a)["parent"] == group, "Import ignored selected group");
    editor.select({group, a}, a);
    require(editor.selectedModels().size() == 2, "Group/model selection repeated descendant");
    editor.select({a}, a);
    QJsonValue materialB = editor.node(b)["material"];
    editor.setMaterialField("roughness", .23);
    require(editor.node(b)["material"] == materialB, "Material edit changed unselected binding");
    require(editor.node(a)["material"] != materialB, "Shared material was not isolated");
    auto initial = sceneMatrix(editor.node(a)["transform"]);
    QMatrix4x4 mirrored;
    mirrored.translate(2, .5, -1);
    mirrored.rotate(34, QVector3D(0, 1, 0));
    mirrored.scale(-2, .6, 1.7);
    editor.setTransforms({{a, mirrored}});
    auto bytes = prepared->geometryData.size();
    auto nodes = prepared->meshes[0]->nodes.size();
    prepared->applyEditorDocument(editor.document);
    require(prepared->geometryData.size() == bytes && prepared->meshes[0]->nodes.size() == nodes &&
                prepared->blasBuildCount == 0,
            "Transform rebuilt immutable geometry");
    std::cout << "Editor: randomized TLAS / brute force" << std::endl;
    std::mt19937 random(793);
    std::uniform_real_distribution<float> coordinate(-5, 5);
    for (int i = 0; i < 2000; ++i)
    {
        QVector3D origin(coordinate(random), coordinate(random), coordinate(random)),
            direction(coordinate(random), coordinate(random), coordinate(random));
        direction.normalize();
        auto accelerated =
            intersectScene(prepared->meshes, prepared->instances, prepared->tlas, origin, direction);
        auto brute =
            intersectScene(prepared->meshes, prepared->instances, prepared->tlas, origin, direction, true);
        require(accelerated.instance == brute.instance, "TLAS/BLAS differs from brute force");
        if (brute.instance >= 0)
            require(std::abs(accelerated.distance - brute.distance) < 1e-4,
                    "Nonuniform scale changed ray distance");
    }
    editor.setMaterialField("emissive", jsonVector(QVector3D(5, 3, 1)));
    const int beforeMaterialTlas = prepared->tlasBuildCount;
    prepared->applyEditorDocument(editor.document, true, true);
    require(prepared->tlasBuildCount == beforeMaterialTlas, "Material edit rebuilt TLAS");
    auto texturePrepared =
        Scene::prepareDocument(editor.document, error, editor.cache, {}, editor.acceleration);
    require(texturePrepared && texturePrepared->tlasBuildCount == 0 && texturePrepared->blasBuildCount == 0,
            "Resource refresh rebuilt reusable acceleration structures");
    for (auto light : prepared->lights_encoded)
        if (int(light.param0.x()) == EncodedLightTriangle)
        {
            int surface = int(light.param0.y());
            require(surface >= 0 && surface < int(prepared->surfaces.size()), "Emissive surface ID invalid");
            require(prepared->surfacePdfs[surface] == light.param0.z(),
                    "Emissive PDF differs across sampling paths");
            const auto ref = prepared->surfaces[surface];
            const auto &instance = prepared->instances[ref.instance];
            const auto &triangle = prepared->triangles[ref.geometry];
            const float worldArea =
                .5f * QVector3D::crossProduct(instance.transform.mapVector(triangle.p2 - triangle.p1),
                                              instance.transform.mapVector(triangle.p3 - triangle.p1))
                          .length();
            require(std::abs(light.param1.w() - worldArea) < 1e-5,
                    "Emissive area did not follow mirrored nonuniform transform");
        }
    editor.setFlag(a, "locked", true);
    auto locked = editor.node(a);
    editor.setTransforms({{a, initial}});
    editor.remove({a});
    editor.setMaterialField("roughness", .9);
    require(editor.node(a) == locked, "Locked object was modified");
    editor.setFlag(a, "locked", false);
    std::cout << "Editor: duplicate / undo" << std::endl;
    editor.select({a}, a);
    editor.duplicate();
    wait(editor);
    require(editor.document.root["objects"].toArray().size() == 3, "Duplicate failed");
    editor.undo.undo();
    wait(editor);
    require(editor.document.root["objects"].toArray().size() == 2, "Undo duplicate failed");
    editor.undo.redo();
    wait(editor);
    require(editor.document.root["objects"].toArray().size() == 3, "Redo duplicate failed");
    editor.move({a}, nested);
    QJsonValue before = editor.node(a)["transform"];
    editor.remove({nested});
    require(editor.node(a)["parent"] == group && editor.node(a)["transform"] == before,
            "Dissolving group changed contents/world transform");
    editor.undo.undo();
    require(editor.node(a)["parent"] == nested, "Undo dissolve failed");
    auto docBeforeFailure = editor.document.root;
    auto undoBefore = editor.undo.index();
    editor.importFiles({path, temporary.path() + "/missing.obj"});
    wait(editor);
    require(!failure.isEmpty() && editor.document.root == docBeforeFailure &&
                editor.undo.index() == undoBefore,
            "Failed batch was not atomic");
    failure.clear();
    editor.remove({group}, true);
    wait(editor);
    require(editor.document.root["objects"].toArray().isEmpty(),
            "Delete group contents did not remove descendants");
    editor.undo.undo();
    wait(editor);
    require(editor.document.root["objects"].toArray().size() == 3, "Undo delete lost cached assets");
    QString save = temporary.path() + "/v2.scene.json";
    require(editor.document.saveScene(save, error), "v2 save failed");
    SceneDocument restored;
    require(SceneDocument::loadScene(save, restored, error), "v2 load failed");
    auto expected = editor.document.root;
    expected["portable"] = false;
    require(restored.root == expected, "v2 roundtrip lost organization/instances");
    require(tree.rowCount() == 1 && tree.id(tree.index(0, 0)) == "root", "Tree has multiple roots");
    editor.remove({group, nested});
    require(editor.node(a)["parent"] == "root", "Nested group dissolve left an orphan");
    editor.undo.undo();
    Camera original, moved;
    editor.document.restoreCamera(original);
    moved = original;
    moved.processMousePan(7, 3);
    ++editor.cameraCommand;
    const int commands = editor.undo.count();
    editor.setCamera(moved);
    moved.processMousePan(4, 1);
    editor.setCamera(moved);
    require(editor.undo.count() <= commands + 1, "Camera gesture was not coalesced");
    editor.document.filePath = save;
    editor.undo.undo();
    Camera restoredCamera;
    editor.document.restoreCamera(restoredCamera);
    require((restoredCamera.position - original.position).length() < 1e-6,
            "Undo did not restore the camera gesture");
    require(editor.document.filePath == save, "Undo reverted the Save As destination");
}
