#version 330 core

layout(location = 0) in vec3 aPos;

uniform mat4 uMvp;
/// Device pixels, so a marker is the same size on screen however far away the
/// point is -- a hardpoint is a coordinate, not an object with a size.
uniform float uPointSize;

void main()
{
    gl_Position = uMvp * vec4(aPos, 1.0);
    gl_PointSize = uPointSize;
}
