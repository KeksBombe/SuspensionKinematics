#version 330 core

in vec3 vNormalVS;
in vec3 vPosVS;

uniform vec3 uBaseColor;

out vec4 fragColor;

void main()
{
    vec3 N = normalize(vNormalVS);
    vec3 V = normalize(-vPosVS);

    // Two-sided: plenty of real STL files have inconsistent winding, and an
    // import viewer should show the geometry rather than punish the exporter.
    if (dot(N, V) < 0.0) N = -N;

    // View-space headlight, nudged off-axis so curvature stays readable.
    vec3 L = normalize(vec3(0.3, 0.4, 1.0));
    float diffuse = max(dot(N, L), 0.0);
    float specular = pow(max(dot(N, normalize(L + V)), 0.0), 32.0) * 0.25;

    fragColor = vec4(uBaseColor * (0.28 + 0.72 * diffuse) + vec3(specular), 1.0);
}
