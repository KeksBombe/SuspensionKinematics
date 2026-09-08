#version 330 core

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;

uniform mat4 uMvp;
uniform mat4 uModelView;
uniform mat3 uNormalMatrix;

out vec3 vNormalVS;
out vec3 vPosVS;

void main()
{
    vPosVS = vec3(uModelView * vec4(aPos, 1.0));
    vNormalVS = uNormalMatrix * aNormal;
    gl_Position = uMvp * vec4(aPos, 1.0);
}
