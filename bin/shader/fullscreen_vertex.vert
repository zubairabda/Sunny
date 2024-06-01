#version 450

layout (location = 0) out vec2 out_uv;

void main()
{
    out_uv = vec2((gl_VertexIndex << 1) & 0x2, gl_VertexIndex & 0x2);

    gl_Position = vec4((out_uv * 2.0f) - 1.0f, 0.0f, 1.0f);
}
