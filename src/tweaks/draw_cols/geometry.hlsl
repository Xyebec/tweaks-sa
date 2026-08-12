float4x4 worldViewProj : register(c0);

struct VsInput {
    float3 position : POSITION0;
    float4 color    : COLOR0;
};

struct VsOutput {
    float4 position : POSITION0;
    float4 color    : COLOR0;
};

VsOutput vs_main(VsInput input) {
    VsOutput output;
    
    output.position = mul(worldViewProj, float4(input.position, 1.0f));
    output.color = input.color;
    
    return output;
}

float4 ps_main(VsOutput input) : COLOR0 {
    return input.color;
}