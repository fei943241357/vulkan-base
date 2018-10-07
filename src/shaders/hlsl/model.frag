[[vk::binding(1, 0)]]
Texture2D texture;

[[vk::binding(2, 0)]]
SamplerState texture_sampler;

struct VS2PS {
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD;
};

float srgb_encode(float c) {
    if (c <= 0.0031308f)
        return 12.92f * c;
    else
        return 1.055f * pow(c, 1.f/2.4f) - 0.055f;
}

float4 main(VS2PS data) : SV_TARGET
{
    float4 color = texture.Sample(texture_sampler, data.uv);
    return float4(srgb_encode(color.r), srgb_encode(color.g), srgb_encode(color.b), color.a);
}