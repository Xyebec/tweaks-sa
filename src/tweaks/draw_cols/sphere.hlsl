float4x4 worldViewProj : register(c0);

struct VsInput {
    // Stream 0 (geometry)
    float3 position : POSITION0;
    
    // Stream 1 (instance)
    float4 instPosScale : TEXCOORD0; // xyz = position, w = scale
    float4 instColor    : COLOR0;
};

struct VsOutput {
    float4 position : POSITION0;
    float4 color    : COLOR0;
};

VsOutput vs_main(VsInput input) {
    VsOutput output;

    float3 worldPosition = input.position * input.instPosScale.w + input.instPosScale.xyz;

    output.position = mul(worldViewProj, float4(worldPosition, 1.0f));
    output.color = input.instColor;

    return output;
}

float4 ps_main(VsOutput input) : COLOR {
    return input.color;
}
