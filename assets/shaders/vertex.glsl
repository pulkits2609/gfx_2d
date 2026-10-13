#version 460 core

layout(location = 0) in vec2 aPos;

uniform mat4 u_Model;
uniform mat4 u_Projection;

void main()
{
    gl_Position = u_Projection * u_Model * vec4(aPos, 0.0, 1.0);
}