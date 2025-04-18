﻿#include "Camera.h"
#include <QDebug>


Camera::Camera(QVector3D position, QVector3D up, float yaw, float pitch) :
    position(position),
    worldUp(up),
    front(-position),
    picth(pitch),
    yaw(yaw),
    movementSpeed(SPEED),
    mouseSensitivity(SENSITIVITY),
    zoom(ZOOM) {
    this->updateCameraVectors();
    r = position.z();
    for (uint i = 0; i != 1024; ++i)
        keys[i] = false;
}

Camera::~Camera()
{

}

// Returns the view matrix calculated using Euler Angles and the LookAt Matrix
QMatrix4x4 Camera::getViewMatrix()
{
    QMatrix4x4 view;
    //view.lookAt(this->position, this->position + this->front, this->up);
    view.lookAt(this->position, QVector3D(0.0f,0.0f,0.0f), this->up);
    return view;
}

// Processes input received from any keyboard-like input system. Accepts input parameter in the form of camera defined ENUM (to abstract it from windowing systems)
void Camera::processKeyboard(Camera_Movement direction, float deltaTime)
{
    float velocity = this->movementSpeed * deltaTime;
    if (direction == FORWARD) {
        this->r -= 0.005* velocity;
        position = QVector3D(-sin(radians(rotatAngle)) * cos(radians(upAngle)), sin(radians(upAngle)), cos(radians(rotatAngle)) * cos(radians(upAngle)));
        position *= r;
        this->updateCameraVectors();
    }
    if (direction == BACKWARD) {
        this->r += 0.005* velocity;
        position = QVector3D(-sin(radians(rotatAngle)) * cos(radians(upAngle)), sin(radians(upAngle)), cos(radians(rotatAngle)) * cos(radians(upAngle)));
        position *= r;
        this->updateCameraVectors();
    }
    if (direction == LEFT) {
        rotatAngle += 150 * velocity / 512;
        position = QVector3D(-sin(radians(rotatAngle)) * cos(radians(upAngle)), sin(radians(upAngle)), cos(radians(rotatAngle)) * cos(radians(upAngle)));
        position *= r;
        this->updateCameraVectors();
    }
    if (direction == RIGHT) {
        rotatAngle -= 150 * velocity / 512;
        position = QVector3D(-sin(radians(rotatAngle)) * cos(radians(upAngle)), sin(radians(upAngle)), cos(radians(rotatAngle)) * cos(radians(upAngle)));
        position *= r;
        this->updateCameraVectors();
    }
    if (direction == UP) {
        upAngle += 150 * (velocity) / 512;
        upAngle = std::min(upAngle, 89.0f);
        upAngle = std::max(upAngle, -89.0f);
        position = QVector3D(-sin(radians(rotatAngle)) * cos(radians(upAngle)), sin(radians(upAngle)), cos(radians(rotatAngle)) * cos(radians(upAngle)));
        position *= r;
        this->updateCameraVectors();
    }
    if (direction == DOWN) {
        upAngle -= 150 * (velocity) / 512;
        upAngle = std::min(upAngle, 89.0f);
        upAngle = std::max(upAngle, -89.0f);
        position = QVector3D(-sin(radians(rotatAngle)) * cos(radians(upAngle)), sin(radians(upAngle)), cos(radians(rotatAngle)) * cos(radians(upAngle)));
        position *= r;
        this->updateCameraVectors();
    }
}

// Processes input received from a mouse input system. Expects the offset value in both the x and y direction.
void Camera::processMouseMovement(float xoffset, float yoffset, bool constraintPitch)
{
    xoffset *= this->mouseSensitivity;
    yoffset *= this->mouseSensitivity;

    /*this->yaw += xoffset;
    this->picth += yoffset;

    if (constraintPitch) {
        if (this->picth > 89.0f)
            this->picth = 89.0f;
        if (this->picth < -89.0f)
            this->picth = -89.0f;
    }*/
    rotatAngle += 150 * (xoffset) / 512;
    upAngle += 150 * (-yoffset) / 512;
    upAngle = std::min(upAngle, 89.0f);
    upAngle = std::max(upAngle, -89.0f);

    position = QVector3D(-sin(radians(rotatAngle)) * cos(radians(upAngle)), sin(radians(upAngle)), cos(radians(rotatAngle)) * cos(radians(upAngle)));
    position *= r;
    this->updateCameraVectors();
}

// Processes input received from a mouse scroll-wheel event. Only requires input on the vertical wheel-axis
void Camera::processMouseScroll(float yoffset)
{
    /*if (this->zoom >= 1.0f && this->zoom <= 45.0f)
        this->zoom -= yoffset;
    if (this->zoom > 45.0f)
        this->zoom = 45.0f;
    if (this->zoom < 1.0f)
        this->zoom = 1.0f;*/
    r += -yoffset * 0.001;
    position = QVector3D(-sin(radians(rotatAngle)) * cos(radians(upAngle)), sin(radians(upAngle)), cos(radians(rotatAngle)) * cos(radians(upAngle)));
    position *= r;
    this->updateCameraVectors();
}

void Camera::processInput(float dt)
{

    if (keys[Qt::Key_Q])
        processKeyboard(FORWARD, dt);
    if (keys[Qt::Key_E])
        processKeyboard(BACKWARD, dt);
    if (keys[Qt::Key_A])
        processKeyboard(LEFT, dt);
    if (keys[Qt::Key_D])
        processKeyboard(RIGHT, dt);
    if (keys[Qt::Key_W])
        processKeyboard(UP, dt);
    if (keys[Qt::Key_S])
        processKeyboard(DOWN, dt);
}

void Camera::updateCameraVectors()
{
    // Calculate the new Front vector
    QVector3D front;
    front.setX(cos(radians(this->yaw)) * cos(radians(this->picth)));
    front.setY(sin(radians(this->picth)));
    front.setZ(sin(radians(this->yaw)) * cos(radians(this->picth)));
    this->front = front.normalized();
    this->right = QVector3D::crossProduct(this->front, this->worldUp).normalized();
    this->up = QVector3D::crossProduct(this->right, this->front).normalized();
}