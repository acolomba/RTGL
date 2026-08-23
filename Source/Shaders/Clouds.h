// Doom64-RT: VOLUMETRIC CLOUDS -- the shared half.
//
// Included by CmCloudMap.comp (which marches the slab into the map) and by the
// two sky fragment shaders (which composite the map over sky geometry). Needs
// DESC_SET_GLOBAL_UNIFORM and DESC_SET_VOLUMETRIC defined by the includer.
//
// THE MAP IS A WORLD-DIRECTION IMAGE, NOT A SCREEN ONE. u is azimuth around
// the world up axis over 360 degrees, v is altitude over the upper hemisphere.
// Both the render-resolution sky pass and the 256^2 GI cubemap pass sample the
// same texel for the same direction, so the march is paid once per frame and
// costs the same at every screen resolution. It also means the temporal
// history in CmCloudMap.comp is keyed by direction alone -- no motion vectors,
// no depth, nothing a sprite in front of the sky can invalidate, which is the
// failure the froxel volume's screen-space history had.

#ifndef CLOUDS_H_
#define CLOUDS_H_

#define CLOUD_MODE_NONE   0
#define CLOUD_MODE_BACK   1 // backdrop: back * T + radiance
#define CLOUD_MODE_BEHIND 2 // drawn after the backdrop but behind the cloud: * T only

bool cloudsEnabled()
{
    return globalUniform.cloudParams0.x > 0.5;
}

// One fixed orthonormal basis around the world up axis. Nothing here depends on
// the camera -- if it did, turning the head would turn the clouds.
void cloudBasis( out vec3 up, out vec3 t1, out vec3 t2 )
{
    up = normalize( globalUniform.worldUpVector.xyz );
    vec3 ref = abs( up.y ) < 0.9 ? vec3( 0, 1, 0 ) : vec3( 1, 0, 0 );
    t1 = normalize( cross( up, ref ) );
    t2 = cross( up, t1 );
}

vec2 cloudDirToUv( vec3 dir )
{
    vec3 up, t1, t2;
    cloudBasis( up, t1, t2 );

    float y  = dot( dir, up );
    float x  = dot( dir, t1 );
    float z  = dot( dir, t2 );
    float az = atan( z, x ); // -pi..pi
    float u  = az / ( 2.0 * M_PI ) + 0.5;
    // v is sqrt(altitude fraction): the rows are packed toward the horizon,
    // where the slab is seen at a grazing angle and its features are
    // smallest on the sky. Negative below the horizon (not in the map).
    float af = asin( clamp( y, -1.0, 1.0 ) ) / ( 0.5 * M_PI ); // -1..1
    float v  = af >= 0.0 ? sqrt( af ) : af;
    return vec2( u, v );
}

vec3 cloudUvToDir( vec2 uv )
{
    vec3 up, t1, t2;
    cloudBasis( up, t1, t2 );

    float az  = ( uv.x - 0.5 ) * 2.0 * M_PI;
    float alt = uv.y * uv.y * 0.5 * M_PI;
    float c   = cos( alt );
    return normalize( t1 * ( c * cos( az ) ) + t2 * ( c * sin( az ) ) + up * sin( alt ) );
}

#ifdef DESC_SET_VOLUMETRIC
// rgb = premultiplied radiance (sky-texture units), a = transmittance.
vec4 cloudSample( vec3 dir )
{
    vec2 uv = cloudDirToUv( dir );
    if( uv.y <= 0.0 )
    {
        return vec4( 0, 0, 0, 1 );
    }
    // The sampler wraps in u and clamps in v, so the seam at azimuth +-180 is
    // continuous and the horizon row is what "just below the horizon" reads.
    return textureLod( g_cloudMap_Sampler, uv, 0.0 );
}

// Doom64-RT: how much of the DIRECTIONAL LIGHT reaches `surfPos` (world,
// metres) along `toLight` (unit, toward the light), per channel. Used by
// sampleDirectionalLight (Light.h) for every shadow ray, so shafts get gaps
// between clouds and the ground gets cloud shadows.
//
// The map was marched from the sky viewer (the origin); a surface elsewhere
// sees the slab through a different column. Corrected by intersecting the
// ray with the slab's middle plane and sampling the map in the direction of
// THAT point from the origin -- exact at mid-height, and the level is small
// against the slab's altitude so the error above and below is tiny.
vec3 cloudSunAttenuation( vec3 surfPos, vec3 toLight )
{
    if( !cloudsEnabled() || globalUniform.cloudUnderColor.w < 0.5 )
    {
        return vec3( 1.0 );
    }

    vec3  up  = normalize( globalUniform.worldUpVector.xyz );
    float upd = dot( toLight, up );
    if( upd <= 0.02 )
    {
        // Light from below the horizon: not through the clouds.
        return vec3( 1.0 );
    }

    float mid = globalUniform.cloudParams0.y + 0.5 * globalUniform.cloudParams0.z;
    float t   = ( mid - dot( surfPos, up ) ) / upd;
    if( t <= 0.0 )
    {
        return vec3( 1.0 );
    }
    vec3 hit = surfPos + toLight * t;

    float T = cloudSample( normalize( hit ) ).a;

    // The deck's contract, kept: the clear fraction passes everything, the
    // covered fraction passes lightTransmit, tinted by the cloud colour.
    vec3 tint = globalUniform.cloudTint.rgb;
    vec3 full = vec3( T ) + ( 1.0 - T ) * globalUniform.cloudLightColor.w * tint;

    float k = globalUniform.cloudAmbient.w;
    return vec3( 1.0 ) - k * ( vec3( 1.0 ) - full );
}

vec4 cloudComposite( vec4 color, vec3 dir, uint mode )
{
    if( !cloudsEnabled() || mode == CLOUD_MODE_NONE )
    {
        return color;
    }

    uint debugMode = uint( globalUniform.cloudParams3.z + 0.5 );
    if( debugMode == 3 )
    {
        return color;
    }

    vec4 c = cloudSample( dir );

    if( debugMode == 1 )
    {
        return mode == CLOUD_MODE_BACK ? vec4( vec3( c.a ), color.a ) : color;
    }

    if( mode == CLOUD_MODE_BEHIND )
    {
        return vec4( color.rgb * c.a, color.a );
    }

    // CLOUD_MODE_BACK. The glow behind the slab rides on the backdrop's
    // transmittance: clear sky is the glow, thin cloud burns with it, dense
    // cloud is a silhouette against it. Faded at the horizon with the rest.
    // The glow fades at the horizon like the cloud does (the map's
    // transmittance is 1 there by construction, so without this the glow
    // is a flat bright band along the skyline).
    float altDeg  = degrees( asin( clamp( dot( dir, normalize( globalUniform.worldUpVector.xyz ) ), -1.0, 1.0 ) ) );
    float hz      = max( globalUniform.cloudParams2.z, 0.01 );
    float backFade = smoothstep( 0.0, hz * 3.0, altDeg );
    vec3 back = globalUniform.cloudBackColor.rgb * globalUniform.cloudBackColor.w * backFade;
    return vec4( ( color.rgb + back ) * c.a + c.rgb, color.a );
}
#endif

#endif // CLOUDS_H_
