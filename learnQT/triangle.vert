#version 330 core
layout (location = 0) in vec3 aPos;
 
out vec3 ourColor;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
 
void main(){
  gl_Position =vec4(aPos, 1.0f);
  ourColor =vec3(aPos);
}