#version 450
/*
layout (location = 0) in struct
{
    uint position;
    uint color;
} vtx_in;

layout (location = 0) out struct 
{
    uint color;
} vtx_out;
*/

layout (location = 0) in vec2 in_pos;
layout (location = 1) in vec2 in_uv;
layout (location = 2) in ivec2 in_texpage;
layout (location = 3) in ivec2 in_clut;
layout (location = 4) in vec4 in_color;

layout (location = 0) out vec2 out_uv;
layout (location = 1) out ivec2 out_texpage;
layout (location = 2) out ivec2 out_clut;
layout (location = 3) out vec4 out_color;

void main()
{
    gl_Position = vec4(in_pos, 0.0f, 1.0f);
    out_uv = in_uv;
    out_texpage = in_texpage;
    out_clut = in_clut;
    out_color = in_color;
}