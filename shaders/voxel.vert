#version 450

#if defined(PATCH_VULKAN)
#define VERTEX_INDEX gl_VertexIndex
#else
#define VERTEX_INDEX gl_VertexID
#endif

layout(location = 0) out vec2 out_uv;

void main() {
    out_uv = vec2((VERTEX_INDEX << 1) & 2, VERTEX_INDEX & 2);
    gl_Position = vec4(out_uv * 2.0 - 1.0, 0.0, 1.0);
}
