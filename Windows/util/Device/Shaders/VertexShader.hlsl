struct VS_OUT
{
    float3 color : COLOR;
    float4 pos : SV_POSITION;
};

cbuffer CBuf
{
    matrix mat;
};

VS_OUT main(float2 pos : Position, float3 color: Color)
{
    VS_OUT output;
    output.pos = mul((pos.x, pos.y, 0.0f, 1.0f), mat);
    output.color = color;
    return output;
}