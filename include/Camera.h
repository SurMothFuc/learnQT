#pragma once
#ifndef CAMERA_H
#define CAMERA_H


#include <QVector3D>
#include <QMatrix4x4>
#include <QKeyEvent>
#include <algorithm>
#include "common.h"

// Defines several possible options for camera movement. Used as abstraction to stay away from window-system specific input methods
enum Camera_Movement {
    FORWARD,
    BACKWARD,
    LEFT,
    RIGHT,
    UP,
    DOWN
};

// Default camera values
const float YAW = -90.0f;
const float PITCH = 0.0f;
const float SPEED = 2.0f;
const float SENSITIVITY = 0.5f;
const float ZOOM = 53.130102f; // Matches the original ray plane at z = -2.

class Camera {
public:
    Camera(QVector3D position = QVector3D(0.0f, 0.0f, 0.0f), QVector3D up = QVector3D(0.0f, 1.0f, 0.0f),
        float yaw = YAW, float pitch = PITCH);
    ~Camera();

    QMatrix4x4 getViewMatrix();
    void restoreState(const QVector3D& eye, const QVector3D& lookAt, const QVector3D& worldUp, float fov);
    void processMouseMovement(float xoffset, float yoffset, bool constraintPitch = true);
    void processMousePan(float xoffset, float yoffset);
    void processMouseScroll(float yoffset);
    void processInput(float dt);

    QVector3D position;
    QVector3D worldUp;
    QVector3D target;
    QVector3D front;

    QVector3D up;
    QVector3D right;

    //Eular Angles
    float picth;
    float yaw;

    float upAngle = 0.0;
    float rotatAngle = 0.0;
    float r = 4;

    //Camera options
    float movementSpeed;
    float mouseSensitivity;
    float zoom;

    //Keyboard multi-touch
    bool keys[1024];
private:
    QVector3D orbitOffset() const;
    void updateOrbitPosition();
    void updateCameraVectors();
    void processKeyboard(Camera_Movement direction, float deltaTime);
};

#endif // CAMERA_H
