#version 330 core

uniform vec4 uColor;
uniform vec3 uRimColor;

out vec4 fragColor;

void main()
{
    // gl_PointCoord runs 0..1 across the sprite; recentre it so the marker can
    // be drawn as a disc rather than the square the rasteriser hands over.
    vec2 offset = gl_PointCoord * 2.0 - 1.0;
    float radius = length(offset);
    if (radius > 1.0) discard;

    // A dark rim, so the marker stays legible against both the light surface
    // colour of a shaded part and the dark background.
    float rim = smoothstep(0.55, 0.85, radius);
    vec3 rgb = mix(uColor.rgb, uRimColor, rim);

    // MSAA does not antialias a discarded fragment, so the edge is softened
    // here instead.
    float alpha = uColor.a * (1.0 - smoothstep(0.9, 1.0, radius));
    fragColor = vec4(rgb, alpha);
}
