// ------------------------------------------------------------------
// OUTPUT VARIABLES  ------------------------------------------------
// ------------------------------------------------------------------

out vec3 FS_OUT_Color;

// ------------------------------------------------------------------
// INPUT VARIABLES  -------------------------------------------------
// ------------------------------------------------------------------

in vec3 FS_IN_WorldPos;
in vec3 FS_IN_Normal;
in vec2 FS_IN_UV;
in vec4 FS_IN_NDCFragPos;

// ------------------------------------------------------------------
// UNIFORMS ---------------------------------------------------------
// ------------------------------------------------------------------

#define POISSON_DISK_SAMPLE_COUNT 64 
#define DIRECTIONAL_LIGHT_NEAR_PLANE 1.0
#define DIRECTIONAL_LIGHT_FAR_PLANE 650.0

uniform vec3      u_Color;
uniform vec3      u_LightColor;
uniform vec3      u_Direction;
uniform sampler2D s_ShadowMap;
uniform float     u_LightBias;
uniform float     u_LightSize;

layout(std140) uniform GlobalUniforms
{
    mat4 view_proj;
    mat4 light_view;
    mat4 light_view_proj;
    vec4 cam_pos;
};

const vec2 kPoissonSamples[64] = vec2[](
    vec2(-0.934812, 0.366741),
    vec2(-0.918943, -0.0941496),
    vec2(-0.873226, 0.62389),
    vec2(-0.8352, 0.937803),
    vec2(-0.822138, -0.281655),
    vec2(-0.812983, 0.10416),
    vec2(-0.786126, -0.767632),
    vec2(-0.739494, -0.535813),
    vec2(-0.681692, 0.284707),
    vec2(-0.61742, -0.234535),
    vec2(-0.601184, 0.562426),
    vec2(-0.607105, 0.847591),
    vec2(-0.581835, -0.00485244),
    vec2(-0.554247, -0.771111),
    vec2(-0.483383, -0.976928),
    vec2(-0.476669, -0.395672),
    vec2(-0.439802, 0.362407),
    vec2(-0.409772, -0.175695),
    vec2(-0.367534, 0.102451),
    vec2(-0.35313, 0.58153),
    vec2(-0.341594, -0.737541),
    vec2(-0.275979, 0.981567),
    vec2(-0.230811, 0.305094),
    vec2(-0.221656, 0.751152),
    vec2(-0.214393, -0.0592364),
    vec2(-0.204932, -0.483566),
    vec2(-0.183569, -0.266274),
    vec2(-0.123936, -0.754448),
    vec2(-0.0859096, 0.118625),
    vec2(-0.0610675, 0.460555),
    vec2(-0.0234687, -0.962523),
    vec2(-0.00485244, -0.373394),
    vec2(0.0213324, 0.760247),
    vec2(0.0359813, -0.0834071),
    vec2(0.0877407, -0.730766),
    vec2(0.14597, 0.281045),
    vec2(0.18186, -0.529649),
    vec2(0.188208, -0.289529),
    vec2(0.212928, 0.063509),
    vec2(0.23661, 0.566027),
    vec2(0.266579, 0.867061),
    vec2(0.320597, -0.883358),
    vec2(0.353557, 0.322733),
    vec2(0.404157, -0.651479),
    vec2(0.410443, -0.413068),
    vec2(0.413556, 0.123325),
    vec2(0.46556, -0.176183),
    vec2(0.49266, 0.55388),
    vec2(0.506333, 0.876888),
    vec2(0.535875, -0.885556),
    vec2(0.615894, 0.0703452),
    vec2(0.637135, -0.637623),
    vec2(0.677236, -0.174291),
    vec2(0.67626, 0.7116),
    vec2(0.686331, -0.389935),
    vec2(0.691031, 0.330729),
    vec2(0.715629, 0.999939),
    vec2(0.8493, -0.0485549),
    vec2(0.863582, -0.85229),
    vec2(0.890622, 0.850581),
    vec2(0.898068, 0.633778),
    vec2(0.92053, -0.355693),
    vec2(0.933348, -0.62981),
    vec2(0.95294, 0.156896)
);



// ------------------------------------------------------------------

float linear_to_eye_depth(float z, float near, float far)
{
    return near + (far - near) * z;
}

// ------------------------------------------------------------------

// Using similar triangles from the surface point to the area light
float search_region_radius_uv(float z_cs, float z_vs)
{
    return u_LightSize * (z_vs - DIRECTIONAL_LIGHT_NEAR_PLANE) / z_vs;
}

// ------------------------------------------------------------------

// Using similar triangles between the area light, the blocking plane and the surface point
float penumbra_radius_uv(float zReceiver, float zBlocker)
{
    return (zReceiver - zBlocker) / zBlocker;
}

// ------------------------------------------------------------------

// Project UV size to the near plane of the light
vec2 project_to_light_uv(float size_uv, float z_vs)
{
    return vec2(size_uv) * u_LightSize * DIRECTIONAL_LIGHT_NEAR_PLANE / z_vs;
}

// ------------------------------------------------------------------

// Derivatives of light-space depth with respect to texture2D coordinates
vec2 depth_gradient(vec2 uv, float z)
{
    vec2 dz_duv = vec2(0.0, 0.0);

    vec3 duvdist_dx = dFdx(vec3(uv,z));
    vec3 duvdist_dy = dFdy(vec3(uv,z));

    dz_duv.x = duvdist_dy.y * duvdist_dx.z;
    dz_duv.x -= duvdist_dx.y * duvdist_dy.z;

    dz_duv.y = duvdist_dx.x * duvdist_dy.z;
    dz_duv.y -= duvdist_dy.x * duvdist_dx.z;

    float det = (duvdist_dx.x * duvdist_dy.y) - (duvdist_dx.y * duvdist_dy.x);
    dz_duv /= det;

    return dz_duv;
}

// ------------------------------------------------------------------

// Returns average blocker depth in the search region, as well as the number of found blockers.
// Blockers are defined as shadow-map samples between the surface point and the light.
void find_blocker(out float accum_blocker_depth, 
    			  out float num_blockers,
    			  out float max_blockers,
    			  vec2 uv,
    			  float z0,
    			  float bias,
    			  float search_region_radius_uv)
{
    accum_blocker_depth = 0.0;
    num_blockers = 0.0;
	max_blockers = POISSON_DISK_SAMPLE_COUNT;
    
    for (int i = 0; i < POISSON_DISK_SAMPLE_COUNT; ++i)
    {
        vec2 offset = kPoissonSamples[i] * search_region_radius_uv;
        float shadow_map_depth = texture(s_ShadowMap, uv + offset).r;
		
        // float z = biasedZ(z0, dz_duv, offset);
        float biased_depth = z0 - bias;

        if (shadow_map_depth < biased_depth)
        {
            accum_blocker_depth += shadow_map_depth;
            num_blockers++;
        }
    }
}

// ------------------------------------------------------------------

float pcf_poisson_filter(vec2 uv, float z0, float bias, vec2 filter_radius_uv)
{
    float sum = 0.0;

    for (int i = 0; i < POISSON_DISK_SAMPLE_COUNT; ++i)
    {
        vec2 offset = kPoissonSamples[i] * filter_radius_uv;
        float shadow_map_depth = texture(s_ShadowMap, uv + offset).r;
        sum +=  shadow_map_depth < (z0 - bias) ? 0.0 : 1.0;
    }

    return sum / float(POISSON_DISK_SAMPLE_COUNT);
}

// ------------------------------------------------------------------

float pcss_filter(vec2 uv, float z, float bias, float z_vs, out float p_r)
{
    // ------------------------
    // STEP 1: blocker search
    // ------------------------
    float accum_blocker_depth, num_blockers, max_blockers;
    float search_region_radius_uv = search_region_radius_uv(z, z_vs);
    find_blocker(accum_blocker_depth, num_blockers, max_blockers, uv, z, bias, search_region_radius_uv);

    // Early out if not in the penumbra
    if (num_blockers == 0.0)
        return 1.0;

    // ------------------------
    // STEP 2: penumbra size
    // ------------------------
    float avg_blocker_depth = accum_blocker_depth / num_blockers;
    float avg_blocker_depth_vs = linear_to_eye_depth(avg_blocker_depth, DIRECTIONAL_LIGHT_NEAR_PLANE, DIRECTIONAL_LIGHT_FAR_PLANE);
    float penumbra_radius = penumbra_radius_uv(z_vs, avg_blocker_depth_vs);
    vec2 filter_radius = project_to_light_uv(penumbra_radius, z_vs);
    p_r = filter_radius.x;

    // ------------------------
    // STEP 3: filtering
    // ------------------------
    return pcf_poisson_filter(uv, z, bias, filter_radius);
}

float shadow_occlussion(vec3 p, out float search_radius, out float p_r)
{
    // Transform frag position into Light-space.
    vec4 light_space_pos = light_view_proj * vec4(p, 1.0);

    vec3 proj_coords = light_space_pos.xyz / light_space_pos.w;
    // transform to [0,1] range
    proj_coords = proj_coords * 0.5 + 0.5;
    // get depth of current fragment from light's perspective
    float current_depth = proj_coords.z;
    // check whether current frag pos is in shadow
    float bias   = u_LightBias;
 
    vec4 pos_vs = light_view * vec4(p, 1.0);

    search_radius = search_region_radius_uv(current_depth, abs(pos_vs.z));

    return pcss_filter(proj_coords.xy, current_depth, bias, abs(pos_vs.z), p_r);
}
// ------------------------------------------------------------------
// MAIN -------------------------------------------------------------
// ------------------------------------------------------------------

void main()
{
    float frag_depth = (FS_IN_NDCFragPos.z / FS_IN_NDCFragPos.w) * 0.5 + 0.5;
    float r;
    float p_r;
    float shadow     = shadow_occlussion(FS_IN_WorldPos, r, p_r);

    vec3 L = normalize(-u_Direction);
    vec3 N = normalize(FS_IN_Normal);

    FS_OUT_Color = u_Color * clamp(dot(N, L), 0.0, 1.0) * shadow + u_Color * 0.1;
}

// ------------------------------------------------------------------
