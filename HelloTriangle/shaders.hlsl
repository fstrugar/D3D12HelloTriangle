//*********************************************************
//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
//*********************************************************

struct PSInput
{
    float4 position : SV_POSITION;
    float4 color : COLOR;
};

PSInput VSMain(float4 position : POSITION, float4 color : COLOR)
{
    PSInput result;

    result.position = position;
    result.color = color;

    return result;
}

/*
float4 PSMain(PSInput input) : SV_TARGET
{
    return input.color;
}
*/
 
float4 PSMain(PSInput input) : SV_TARGET
{
    float4 color = input.color;
    [loop] for( int i = 0; i < uint(input.position.x) % 10; i++ )
        color += (uint(input.position.y) % 10) * 0.01;
    float somethingA = ddx_fine( color.x );
    float somethingB = QuadReadLaneAt( input.position.x, 0 );
    color.x += somethingA * somethingB;
    return color;
}
