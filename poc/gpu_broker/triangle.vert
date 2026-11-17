#version 450
// A triangle around the centre of the screen, turned by angle: its centroid
// stays at the centre, so the centre pixel is always a third of each colour.
layout(push_constant) uniform Push { float angle; float aspect; } push;
layout(location = 0) out vec3 colour;

const vec2 corners[3] = vec2[](vec2(0.0, -0.8), vec2(0.6928, 0.4), vec2(-0.6928, 0.4));
const vec3 tints[3] = vec3[](vec3(1.0, 0.0, 0.0), vec3(0.0, 1.0, 0.0), vec3(0.0, 0.0, 1.0));

void main()
{
    float c = cos(push.angle), s = sin(push.angle);
    vec2 p = corners[gl_VertexIndex];
    p = vec2(c * p.x - s * p.y, s * p.x + c * p.y);
    gl_Position = vec4(p.x / push.aspect, p.y, 0.0, 1.0);
    colour = tints[gl_VertexIndex];
}
