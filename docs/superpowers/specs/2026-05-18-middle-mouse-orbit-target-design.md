# Middle Mouse Orbit Target Design

## Context

`GLWidget` currently sends left-button drags to `Camera::processMouseMovement()`.
That method updates spherical orbit angles and recomputes `Camera::position` around
the implicit world origin. `Camera::getViewMatrix()` also hard-codes the look-at
center to `(0, 0, 0)`, so the rotation center cannot be changed.

The requested interaction is middle-mouse dragging to pan the view. This should
move the orbit center and camera together along the current view plane, so later
left-button orbiting rotates around the new center.

## Behavior

- Add an explicit `Camera::target` orbit center initialized to `(0, 0, 0)`.
- `Camera::getViewMatrix()` uses `lookAt(position, target, up)`.
- Existing left-button orbit behavior remains visually unchanged when `target`
  is the origin.
- Left-button orbit keeps `target` fixed and recomputes `position` as
  `target + sphericalOffset`.
- Wheel zoom keeps `target` fixed and moves `position` along the current orbit
  offset.
- Middle-button drag pans the camera by translating both `target` and
  `position` by the same vector in the camera view plane.
- During middle-button drag, rendering switches to low resolution just like
  left-button drag, then restores on release.

## Interaction Mapping

- Left mouse drag: orbit around `Camera::target`.
- Middle mouse drag: pan the view plane and update `Camera::target`.
- Wheel: zoom toward or away from `Camera::target`.
- Keyboard controls retain their existing orbit/zoom semantics, but operate
  relative to `Camera::target`.

## Implementation Notes

- Add a helper for recomputing the orbit position from `rotatAngle`, `upAngle`,
  `r`, and `target` to avoid duplicating the spherical-coordinate expression.
- Add `Camera::processMousePan(float xoffset, float yoffset)`.
- Pan direction should follow viewport convention: dragging right moves the
  scene right on screen by moving camera and target opposite the camera's
  `right` vector; dragging up moves the scene up by moving camera and target
  opposite the camera's `up` vector.
- Scale pan speed by orbit radius so panning remains usable when zoomed in or
  out.
- Track left and middle button pressed states independently in `GLWidget`.

## Testing

- Add or update a focused camera behavior test if the project test setup can
  compile/link `Camera` without the full Qt application.
- Minimum expected coverage:
  - pan changes both `target` and `position` by the same delta;
  - pan preserves `position - target`;
  - orbit after pan keeps the new `target`;
  - view matrix uses the explicit target.
- If the current test harness cannot build C++ Qt camera tests, verify with a
  project build and document the remaining manual check.
