#version 450

layout (set = 0, binding = 0) uniform sampler2D vram;

layout (push_constant) uniform constant
{
    uint texture;
} constants;

layout (location = 0) in vec2 in_uv;
layout (location = 1) flat in ivec2 in_texpage;
layout (location = 2) flat in ivec2 in_clut;
layout (location = 3) in vec4 in_color;

layout (location = 0) out vec4 out_color;

void main()
{
    switch (constants.texture)
    {
    case 0:
        out_color = in_color;
        break;
    case 1:
        ivec2 direct_uv = ivec2(int(in_uv.x), int(in_uv.y)) + in_texpage;
        out_color = (texelFetch(vram, direct_uv, 0) * in_color) * 2.0f; // for now we treat all raw textures as half brightness and multiply it here
        if (out_color == vec4(0)) // temporary fix to get around fills ignoring transparency
            discard;
        break;
    case 2: // 4bpp
        ivec2 uv = ivec2(int(in_uv.x), int(in_uv.y));
        ivec2 texcoord = ivec2(uv.x >> 2, uv.y) + in_texpage; // divide uv x by 4 to get pixel where clut is
        vec4 texel = (texelFetch(vram, texcoord, 0) * 31.0f) + 0.5f; // 0.5 offset needed to fix clut sampling

        int clut = int(texel.r) | int(texel.g) << 5 | int(texel.b) << 10 | int(texel.a) << 15;
        int shift = (uv.x & 0x3) << 2; // texcoord gets us the pixel we should look at, shift gets us the index we should look at within that pixel
        int clut_index = (clut >> shift) & 0xf;

        ivec2 clut_coord = ivec2(in_clut.x + clut_index, in_clut.y);
        out_color = (texelFetch(vram, clut_coord, 0) * in_color) * 2.0f;
        if (out_color == vec4(0))
            discard;
        break;
    }
}