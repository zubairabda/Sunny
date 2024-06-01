#version 450

layout (set = 0, binding = 0) uniform sampler2D tex;
layout (location = 0) in vec2 in_uv;

layout (location = 0) out vec4 out_color;

void main()
{
    out_color = texture(tex, in_uv);
    //out_color = vec4(1.0f, 0.0f, 0.0f, 1.0f);
}