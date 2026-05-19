#include "Camera.h"
#include <QDebug>
#include <qmath.h>


Camera::Camera(QVector3D position, QVector3D up, float yaw, float pitch) :
    position(position),
    worldUp(up),
    target(QVector3D(0.0f, 0.0f, 0.0f)),
    front(target - position),
    picth(pitch),
    yaw(yaw),
    movementSpeed(SPEED),
    mouseSensitivity(SENSITIVITY),
    zoom(ZOOM) {
    this->updateCameraVectors();
    r = position.length();

    QVector3D n_posi = position.normalized();
    upAngle = qRadiansToDegrees(asin(n_posi.y()));

    float cosUp = cos(qDegreesToRadians(upAngle));
    if (qAbs(cosUp) > 1e-6) {  // 避免除以接近零的值
        rotatAngle = qRadiansToDegrees(atan2(-n_posi.x(), n_posi.z()));
    }
    else {
        rotatAngle = 0.0f;  // 默认值
    }
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
    view.lookAt(this->position, this->target, this->up);
    return view;
}

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

// Processes input received from any keyboard-like input system. Accepts input parameter in the form of camera defined ENUM (to abstract it from windowing systems)
void Camera::processKeyboard(Camera_Movement direction, float deltaTime)
{
    float velocity = this->movementSpeed * deltaTime;
    if (direction == FORWARD) {
        this->r -= 0.005* velocity;
        this->updateOrbitPosition();
        this->updateCameraVectors();
    }
    if (direction == BACKWARD) {
        this->r += 0.005* velocity;
        this->updateOrbitPosition();
        this->updateCameraVectors();
    }
    if (direction == LEFT) {
        rotatAngle += 150 * velocity / 512;
        this->updateOrbitPosition();
        this->updateCameraVectors();
    }
    if (direction == RIGHT) {
        rotatAngle -= 150 * velocity / 512;
        this->updateOrbitPosition();
        this->updateCameraVectors();
    }
    if (direction == UP) {
        upAngle += 150 * (velocity) / 512;
        upAngle = std::min(upAngle, 89.0f);
        upAngle = std::max(upAngle, -89.0f);
        this->updateOrbitPosition();
        this->updateCameraVectors();
    }
    if (direction == DOWN) {
        upAngle -= 150 * (velocity) / 512;
        upAngle = std::min(upAngle, 89.0f);
        upAngle = std::max(upAngle, -89.0f);
        this->updateOrbitPosition();
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

    this->updateOrbitPosition();
    //qDebug() << position << " " << rotatAngle << " " << upAngle;
    this->updateCameraVectors();
}

void Camera::processMousePan(float xoffset, float yoffset)
{
    const float panScale = std::max(this->r, 0.001f) * 0.001f;
    const QVector3D pan = (-this->right * xoffset - this->up * yoffset) * panScale;
    this->target += pan;
    this->position += pan;
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
    this->updateOrbitPosition();
    this->updateCameraVectors();
}

void Camera::processInput(float dt)
{
    const float keyboardPanStep = 16.0f * dt;

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
    if (keys[Qt::Key_I])
        processMousePan(0.0f, -keyboardPanStep);
    if (keys[Qt::Key_J])
        processMousePan(keyboardPanStep, 0.0f);
    if (keys[Qt::Key_K])
        processMousePan(0.0f, keyboardPanStep);
    if (keys[Qt::Key_L])
        processMousePan(-keyboardPanStep, 0.0f);
}

void Camera::updateCameraVectors()
{
    QVector3D front = this->target - this->position;
    if (front.lengthSquared() < 1e-12f) {
        front = QVector3D(0.0f, 0.0f, -1.0f);
    }
    this->front = front.normalized();
    this->right = QVector3D::crossProduct(this->front, this->worldUp).normalized();
    this->up = QVector3D::crossProduct(this->right, this->front).normalized();
}
