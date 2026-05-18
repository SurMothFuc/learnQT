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
    assert "const float keyboardPanStep = 16.0f * dt;" in camera_cpp
    assert "if (keys[Qt::Key_I])\n        processMousePan(0.0f, -keyboardPanStep);" in camera_cpp
    assert "if (keys[Qt::Key_J])\n        processMousePan(keyboardPanStep, 0.0f);" in camera_cpp
    assert "if (keys[Qt::Key_K])\n        processMousePan(0.0f, keyboardPanStep);" in camera_cpp
    assert "if (keys[Qt::Key_L])\n        processMousePan(-keyboardPanStep, 0.0f);" in camera_cpp

    assert "bool m_bMiddlePressed = false;" in glwidget_h
    assert "event->button() == Qt::MiddleButton" in glwidget_cpp
    assert "Scene::getInstance().camera.processMousePan(xoffset, yoffset);" in glwidget_cpp


if __name__ == "__main__":
    main()
