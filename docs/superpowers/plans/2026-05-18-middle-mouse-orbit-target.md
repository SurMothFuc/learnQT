# Middle Mouse Orbit Target Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add middle-mouse drag panning that moves the camera orbit center so future left-button rotations orbit around the new center.

**Architecture:** Make the orbit target explicit in `Camera`, and route middle-button drag from `GLWidget` into a new `Camera::processMousePan()` method. Keep the existing orbit math, but add `target` when recomputing `position`.

**Tech Stack:** C++14, Qt5 Widgets/OpenGL, `QVector3D`, `QMatrix4x4`, existing CMake project.

---

## File Structure

- Modify `include/Camera.h`: expose `target`, declare `processMousePan()`, and add a private orbit-position helper.
- Modify `src/Camera.cpp`: make view, orbit, zoom, keyboard, and pan operate relative to `target`.
- Modify `include/glwidget.h`: track middle-button drag state independently.
- Modify `src/glwidget.cpp`: start/stop middle-drag interaction and call `processMousePan()`.
- Create `tests/test_middle_mouse_orbit_target.py`: static regression checks for the new camera API and interaction wiring, matching the existing Python test style.

### Task 1: Camera API Regression Test

**Files:**
- Create: `tests/test_middle_mouse_orbit_target.py`

- [ ] **Step 1: Write the failing test**

```python
#!/usr/bin/env python3
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(path):
    return (ROOT / path).read_text(encoding="utf-8-sig")


def main():
    camera_h = read("include/Camera.h")
    camera_cpp = read("src/Camera.cpp")
    glwidget_h = read("include/glwidget.h")
    glwidget_cpp = read("src/glwidget.cpp")

    assert "QVector3D target;" in camera_h
    assert "void processMousePan(float xoffset, float yoffset);" in camera_h
    assert "QVector3D orbitOffset() const;" in camera_h
    assert "void updateOrbitPosition();" in camera_h

    assert "view.lookAt(this->position, this->target, this->up);" in camera_cpp
    assert "position = target + orbitOffset();" in camera_cpp
    assert "void Camera::processMousePan(float xoffset, float yoffset)" in camera_cpp
    assert "const QVector3D pan = (-this->right * xoffset - this->up * yoffset) * panScale;" in camera_cpp
    assert "this->target += pan;" in camera_cpp
    assert "this->position += pan;" in camera_cpp

    assert "bool m_bMiddlePressed = false;" in glwidget_h
    assert "event->button() == Qt::MiddleButton" in glwidget_cpp
    assert "Scene::getInstance().camera.processMousePan(xoffset, yoffset);" in glwidget_cpp


if __name__ == "__main__":
    main()
```

- [ ] **Step 2: Run test to verify it fails**

Run: `python3 tests/test_middle_mouse_orbit_target.py`
Expected: FAIL on missing `Camera::target` or `processMousePan`.

- [ ] **Step 3: Commit test if git identity is configured**

Run: `git add tests/test_middle_mouse_orbit_target.py && git commit -m "test: cover middle mouse orbit target"`
Expected: commit succeeds if repository author identity is configured. If identity is missing, keep the file staged or unstaged and continue.

### Task 2: Camera Target and Pan Implementation

**Files:**
- Modify: `include/Camera.h`
- Modify: `src/Camera.cpp`
- Test: `tests/test_middle_mouse_orbit_target.py`

- [ ] **Step 1: Add Camera declarations**

Add to public API in `include/Camera.h`:

```cpp
void processMousePan(float xoffset, float yoffset);
QVector3D target;
```

Add to private API in `include/Camera.h`:

```cpp
QVector3D orbitOffset() const;
void updateOrbitPosition();
```

- [ ] **Step 2: Implement target-relative orbit math**

In `src/Camera.cpp`, initialize `target` to origin in the constructor initializer list:

```cpp
target(QVector3D(0.0f, 0.0f, 0.0f)),
```

Replace hard-coded view target:

```cpp
view.lookAt(this->position, this->target, this->up);
```

Add helpers:

```cpp
QVector3D Camera::orbitOffset() const
{
    QVector3D offset(-sin(radians(rotatAngle)) * cos(radians(upAngle)),
        sin(radians(upAngle)),
        cos(radians(rotatAngle)) * cos(radians(upAngle)));
    return offset * r;
}

void Camera::updateOrbitPosition()
{
    position = target + orbitOffset();
}
```

Replace repeated spherical `position = ...; position *= r;` blocks with:

```cpp
this->updateOrbitPosition();
```

- [ ] **Step 3: Implement middle-drag pan**

Add to `src/Camera.cpp`:

```cpp
void Camera::processMousePan(float xoffset, float yoffset)
{
    const float panScale = std::max(this->r, 0.001f) * 0.001f;
    const QVector3D pan = (-this->right * xoffset - this->up * yoffset) * panScale;
    this->target += pan;
    this->position += pan;
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `python3 tests/test_middle_mouse_orbit_target.py`
Expected: PASS with exit code 0.

### Task 3: GLWidget Middle Button Wiring

**Files:**
- Modify: `include/glwidget.h`
- Modify: `src/glwidget.cpp`
- Test: `tests/test_middle_mouse_orbit_target.py`

- [ ] **Step 1: Track middle-button state**

Add to `include/glwidget.h`:

```cpp
bool m_bMiddlePressed = false;
```

- [ ] **Step 2: Update mouse press/release handling**

In `mousePressEvent()`, add a middle-button branch that sets `m_bMiddlePressed`, records `m_lastPos`, and enables low-resolution rendering.

In `mouseReleaseEvent()`, clear only the button that was released. Restore high-resolution rendering only when neither left nor middle dragging remains active.

- [ ] **Step 3: Route middle movement to Camera**

In `mouseMoveEvent()`, compute offsets once. If left is pressed, call `processMouseMovement(xoffset, yoffset)`. If middle is pressed, call `processMousePan(xoffset, yoffset)`. In both cases, lock `param_mutex` and call `markSceneDirty(SceneDirtyFlag::Camera)`.

- [ ] **Step 4: Run test to verify it passes**

Run: `python3 tests/test_middle_mouse_orbit_target.py`
Expected: PASS with exit code 0.

### Task 4: Build and Final Verification

**Files:**
- Verify all touched files.

- [ ] **Step 1: Run Python regression tests**

Run: `python3 tests/test_finite_analytic_lights.py && python3 tests/test_middle_mouse_orbit_target.py`
Expected: both commands exit 0.

- [ ] **Step 2: Run CMake build if configured**

Run: `cmake --build build`
Expected: build exits 0 if a local `build` directory exists and is configured.

- [ ] **Step 3: Inspect diff**

Run: `git diff -- include/Camera.h src/Camera.cpp include/glwidget.h src/glwidget.cpp tests/test_middle_mouse_orbit_target.py`
Expected: diff contains only the explicit orbit target, middle-button pan wiring, and regression test.
