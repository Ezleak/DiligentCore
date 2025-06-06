/*
 *  Copyright 2019-2025 Diligent Graphics LLC
 *  Copyright 2015-2019 Egor Yusov
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 *  In no event and under no legal theory, whether in tort (including negligence),
 *  contract, or otherwise, unless required by applicable law (such as deliberate
 *  and grossly negligent acts) or agreed to in writing, shall any Contributor be
 *  liable for any damages, including any direct, indirect, special, incidental,
 *  or consequential damages of any character arising as a result of this License or
 *  out of the use or inability to use the software (including but not limited to damages
 *  for loss of goodwill, work stoppage, computer failure or malfunction, or any and
 *  all other commercial damages or losses), even if such Contributor has been advised
 *  of the possibility of such damages.
 */

#include "EngineMemory.h"
#include "StringTools.hpp"
#include "ShaderResources.hpp"
#include "HashUtils.hpp"
#include "ShaderResourceVariableBase.hpp"
#include "Align.hpp"

namespace Diligent
{

SHADER_RESOURCE_TYPE D3DShaderResourceAttribs::GetShaderResourceType() const
{
    switch (InputType) // Not using GetInputType() to avoid warnings for D3D_SIT_RTACCELERATIONSTRUCTURE in old SDKs
    {
        case D3D_SIT_CBUFFER:
            return SHADER_RESOURCE_TYPE_CONSTANT_BUFFER;
            break;

        case D3D_SIT_TBUFFER:
            UNSUPPORTED("TBuffers are not supported");
            return SHADER_RESOURCE_TYPE_UNKNOWN;
            break;

        case D3D_SIT_TEXTURE:
            return (GetSRVDimension() == D3D_SRV_DIMENSION_BUFFER ? SHADER_RESOURCE_TYPE_BUFFER_SRV : SHADER_RESOURCE_TYPE_TEXTURE_SRV);
            break;

        case D3D_SIT_SAMPLER:
            return SHADER_RESOURCE_TYPE_SAMPLER;
            break;

        case D3D_SIT_UAV_RWTYPED:
            return (GetSRVDimension() == D3D_SRV_DIMENSION_BUFFER ? SHADER_RESOURCE_TYPE_BUFFER_UAV : SHADER_RESOURCE_TYPE_TEXTURE_UAV);
            break;

        case D3D_SIT_STRUCTURED:
        case D3D_SIT_BYTEADDRESS:
            return SHADER_RESOURCE_TYPE_BUFFER_SRV;
            break;

        case D3D_SIT_UAV_RWSTRUCTURED:
        case D3D_SIT_UAV_RWBYTEADDRESS:
        case D3D_SIT_UAV_APPEND_STRUCTURED:
        case D3D_SIT_UAV_CONSUME_STRUCTURED:
        case D3D_SIT_UAV_RWSTRUCTURED_WITH_COUNTER:
            return SHADER_RESOURCE_TYPE_BUFFER_UAV;
            break;

        case D3D_SIT_RTACCELERATIONSTRUCTURE:
            return SHADER_RESOURCE_TYPE_ACCEL_STRUCT;
            break;

        default:
            UNEXPECTED("Unknown input type");
            return SHADER_RESOURCE_TYPE_UNKNOWN;
    }
}

PIPELINE_RESOURCE_FLAGS D3DShaderResourceAttribs::GetPipelineResourceFlags() const
{
    switch (GetInputType())
    {
        case D3D_SIT_UAV_RWTYPED:
        case D3D_SIT_TEXTURE:
            return (GetSRVDimension() == D3D_SRV_DIMENSION_BUFFER) ? PIPELINE_RESOURCE_FLAG_FORMATTED_BUFFER : PIPELINE_RESOURCE_FLAG_NONE;

        default:
            return PIPELINE_RESOURCE_FLAG_NONE;
    }
}


ShaderResources::~ShaderResources()
{
    for (Uint32 n = 0; n < GetNumCBs(); ++n)
        GetCB(n).~D3DShaderResourceAttribs();

    for (Uint32 n = 0; n < GetNumTexSRV(); ++n)
        GetTexSRV(n).~D3DShaderResourceAttribs();

    for (Uint32 n = 0; n < GetNumTexUAV(); ++n)
        GetTexUAV(n).~D3DShaderResourceAttribs();

    for (Uint32 n = 0; n < GetNumBufSRV(); ++n)
        GetBufSRV(n).~D3DShaderResourceAttribs();

    for (Uint32 n = 0; n < GetNumBufUAV(); ++n)
        GetBufUAV(n).~D3DShaderResourceAttribs();

    for (Uint32 n = 0; n < GetNumSamplers(); ++n)
        GetSampler(n).~D3DShaderResourceAttribs();

    for (Uint32 n = 0; n < GetNumAccelStructs(); ++n)
        GetAccelStruct(n).~D3DShaderResourceAttribs();
}

void ShaderResources::AllocateMemory(IMemoryAllocator&                Allocator,
                                     const D3DShaderResourceCounters& ResCounters,
                                     size_t                           ResourceNamesPoolSize,
                                     StringPool&                      ResourceNamesPool)
{
    Uint32 CurrentOffset = 0;

    auto AdvanceOffset = [&CurrentOffset](Uint32 NumResources) //
    {
        constexpr Uint32 MaxOffset = std::numeric_limits<OffsetType>::max();
        VERIFY(CurrentOffset <= MaxOffset, "Current offset (", CurrentOffset, ") exceeds max allowed value (", MaxOffset, ")");
        OffsetType Offset = static_cast<OffsetType>(CurrentOffset);
        CurrentOffset += NumResources;
        return Offset;
    };

    // clang-format off
    OffsetType CBOffset  = AdvanceOffset(ResCounters.NumCBs);       (void)CBOffset; // To suppress warning
    m_TexSRVOffset       = AdvanceOffset(ResCounters.NumTexSRVs);
    m_TexUAVOffset       = AdvanceOffset(ResCounters.NumTexUAVs);
    m_BufSRVOffset       = AdvanceOffset(ResCounters.NumBufSRVs);
    m_BufUAVOffset       = AdvanceOffset(ResCounters.NumBufUAVs);
    m_SamplersOffset     = AdvanceOffset(ResCounters.NumSamplers);
    m_AccelStructsOffset = AdvanceOffset(ResCounters.NumAccelStructs);
    m_TotalResources     = AdvanceOffset(0);

    size_t AlignedResourceNamesPoolSize = AlignUp(ResourceNamesPoolSize, sizeof(void*));
    size_t MemorySize = m_TotalResources * sizeof(D3DShaderResourceAttribs) + AlignedResourceNamesPoolSize * sizeof(char);

    VERIFY_EXPR(GetNumCBs()         == ResCounters.NumCBs);
    VERIFY_EXPR(GetNumTexSRV()      == ResCounters.NumTexSRVs);
    VERIFY_EXPR(GetNumTexUAV()      == ResCounters.NumTexUAVs);
    VERIFY_EXPR(GetNumBufSRV()      == ResCounters.NumBufSRVs);
    VERIFY_EXPR(GetNumBufUAV()      == ResCounters.NumBufUAVs);
    VERIFY_EXPR(GetNumSamplers()    == ResCounters.NumSamplers);
    VERIFY_EXPR(GetNumAccelStructs()== ResCounters.NumAccelStructs);
    // clang-format on

    if (MemorySize)
    {
        void* pRawMem   = ALLOCATE_RAW(Allocator, "Allocator for shader resources", MemorySize);
        m_MemoryBuffer  = std::unique_ptr<void, STDDeleterRawMem<void>>(pRawMem, Allocator);
        char* NamesPool = reinterpret_cast<char*>(reinterpret_cast<D3DShaderResourceAttribs*>(pRawMem) + m_TotalResources);
        ResourceNamesPool.AssignMemory(NamesPool, ResourceNamesPoolSize);
    }
}

#ifdef DILIGENT_DEVELOPMENT
void ShaderResources::DvpVerifyResourceLayout(const PipelineResourceLayoutDesc& ResourceLayout,
                                              const ShaderResources* const      pShaderResources[],
                                              Uint32                            NumShaders,
                                              bool                              VerifyVariables,
                                              bool                              VerifyImmutableSamplers) noexcept
{
    auto GetAllowedShadersString = [&](SHADER_TYPE ShaderStages) //
    {
        std::string ShadersStr;
        while (ShaderStages != SHADER_TYPE_UNKNOWN)
        {
            const SHADER_TYPE ShaderType = ShaderStages & static_cast<SHADER_TYPE>(~(static_cast<Uint32>(ShaderStages) - 1));
            String            ShaderName;
            for (Uint32 s = 0; s < NumShaders; ++s)
            {
                const ShaderResources& Resources = *pShaderResources[s];
                if ((ShaderStages & Resources.GetShaderType()) != 0)
                {
                    if (ShaderName.size())
                        ShaderName += ", ";
                    ShaderName += Resources.GetShaderName();
                }
            }

            if (!ShadersStr.empty())
                ShadersStr.append(", ");
            ShadersStr.append(GetShaderTypeLiteralName(ShaderType));
            ShadersStr.append(" (");
            if (ShaderName.size())
            {
                ShadersStr.push_back('\'');
                ShadersStr.append(ShaderName);
                ShadersStr.push_back('\'');
            }
            else
            {
                ShadersStr.append("Not enabled in PSO");
            }
            ShadersStr.append(")");

            ShaderStages &= ~ShaderType;
        }
        return ShadersStr;
    };

    if (VerifyVariables)
    {
        for (Uint32 v = 0; v < ResourceLayout.NumVariables; ++v)
        {
            const ShaderResourceVariableDesc& VarDesc = ResourceLayout.Variables[v];
            if (VarDesc.ShaderStages == SHADER_TYPE_UNKNOWN)
            {
                LOG_WARNING_MESSAGE("No allowed shader stages are specified for ", GetShaderVariableTypeLiteralName(VarDesc.Type), " variable '", VarDesc.Name, "'.");
                continue;
            }

            bool VariableFound = false;
            for (Uint32 s = 0; s < NumShaders && !VariableFound; ++s)
            {
                const ShaderResources& Resources = *pShaderResources[s];
                if ((VarDesc.ShaderStages & Resources.GetShaderType()) == 0)
                    continue;

                const bool UseCombinedTextureSamplers = Resources.IsUsingCombinedTextureSamplers();
                for (Uint32 n = 0; n < Resources.m_TotalResources && !VariableFound; ++n)
                {
                    const D3DShaderResourceAttribs& Res = Resources.GetResAttribs(n, Resources.m_TotalResources, 0);

                    // Skip samplers if combined texture samplers are used as
                    // in this case they are not treated as independent variables
                    if (UseCombinedTextureSamplers && Res.GetInputType() == D3D_SIT_SAMPLER)
                        continue;

                    VariableFound = (strcmp(Res.Name, VarDesc.Name) == 0);
                }
            }

            if (!VariableFound)
            {
                LOG_WARNING_MESSAGE(GetShaderVariableTypeLiteralName(VarDesc.Type), " variable '", VarDesc.Name,
                                    "' is not found in any of the designated shader stages: ",
                                    GetAllowedShadersString(VarDesc.ShaderStages));
            }
        }
    }

    if (VerifyImmutableSamplers)
    {
        for (Uint32 sam = 0; sam < ResourceLayout.NumImmutableSamplers; ++sam)
        {
            const ImmutableSamplerDesc& StSamDesc = ResourceLayout.ImmutableSamplers[sam];
            if (StSamDesc.ShaderStages == SHADER_TYPE_UNKNOWN)
            {
                LOG_WARNING_MESSAGE("No allowed shader stages are specified for immutable sampler '", StSamDesc.SamplerOrTextureName, "'.");
                continue;
            }

            const char* TexOrSamName = StSamDesc.SamplerOrTextureName;

            bool ImtblSamplerFound = false;
            for (Uint32 s = 0; s < NumShaders && !ImtblSamplerFound; ++s)
            {
                const ShaderResources& Resources = *pShaderResources[s];
                if ((StSamDesc.ShaderStages & Resources.GetShaderType()) == 0)
                    continue;

                // Look for immutable sampler.
                // In case HLSL-style combined image samplers are used, the condition is  Sampler.Name == "g_Texture" + "_sampler".
                // Otherwise the condition is  Sampler.Name == "g_Texture_sampler" + "".
                const char* CombinedSamplerSuffix = Resources.GetCombinedSamplerSuffix();
                for (Uint32 n = 0; n < Resources.GetNumSamplers() && !ImtblSamplerFound; ++n)
                {
                    const D3DShaderResourceAttribs& Sampler = Resources.GetSampler(n);
                    ImtblSamplerFound                       = StreqSuff(Sampler.Name, TexOrSamName, CombinedSamplerSuffix);
                }
            }

            if (!ImtblSamplerFound)
            {
                LOG_WARNING_MESSAGE("Immutable sampler '", TexOrSamName, "' is not found in any of the designated shader stages: ",
                                    GetAllowedShadersString(StSamDesc.ShaderStages));
            }
        }
    }
}
#endif


Uint32 ShaderResources::FindAssignedSamplerId(const D3DShaderResourceAttribs& TexSRV, const char* SamplerSuffix) const
{
    VERIFY_EXPR(SamplerSuffix != nullptr && *SamplerSuffix != 0);
    VERIFY_EXPR(TexSRV.GetInputType() == D3D_SIT_TEXTURE && TexSRV.GetSRVDimension() != D3D_SRV_DIMENSION_BUFFER);
    Uint32 NumSamplers = GetNumSamplers();
    for (Uint32 s = 0; s < NumSamplers; ++s)
    {
        const D3DShaderResourceAttribs& Sampler = GetSampler(s);
        if (StreqSuff(Sampler.Name, TexSRV.Name, SamplerSuffix))
        {
            DEV_CHECK_ERR(Sampler.BindCount == TexSRV.BindCount || Sampler.BindCount == 1, "Sampler '", Sampler.Name, "' assigned to texture '", TexSRV.Name, "' must be scalar or have the same array dimension (", TexSRV.BindCount, "). Actual sampler array dimension : ", Sampler.BindCount);
            return s;
        }
    }
    return D3DShaderResourceAttribs::InvalidSamplerId;
}

bool ShaderResources::IsCompatibleWith(const ShaderResources& Res) const
{
    if (GetNumCBs() != Res.GetNumCBs() ||
        GetNumTexSRV() != Res.GetNumTexSRV() ||
        GetNumTexUAV() != Res.GetNumTexUAV() ||
        GetNumBufSRV() != Res.GetNumBufSRV() ||
        GetNumBufUAV() != Res.GetNumBufUAV() ||
        GetNumSamplers() != Res.GetNumSamplers())
        return false;

    bool IsCompatible = true;
    ProcessResources(
        [&](const D3DShaderResourceAttribs& CB, Uint32 n) //
        {
            if (!CB.IsCompatibleWith(Res.GetCB(n)))
                IsCompatible = false;
        },
        [&](const D3DShaderResourceAttribs& Sam, Uint32 n) //
        {
            if (!Sam.IsCompatibleWith(Res.GetSampler(n)))
                IsCompatible = false;
        },
        [&](const D3DShaderResourceAttribs& TexSRV, Uint32 n) //
        {
            if (!TexSRV.IsCompatibleWith(Res.GetTexSRV(n)))
                IsCompatible = false;
        },
        [&](const D3DShaderResourceAttribs& TexUAV, Uint32 n) //
        {
            if (!TexUAV.IsCompatibleWith(Res.GetTexUAV(n)))
                IsCompatible = false;
        },
        [&](const D3DShaderResourceAttribs& BufSRV, Uint32 n) //
        {
            if (!BufSRV.IsCompatibleWith(Res.GetBufSRV(n)))
                IsCompatible = false;
        },
        [&](const D3DShaderResourceAttribs& BufUAV, Uint32 n) //
        {
            if (!BufUAV.IsCompatibleWith(Res.GetBufUAV(n)))
                IsCompatible = false;
        },
        [&](const D3DShaderResourceAttribs& AccelStruct, Uint32 n) //
        {
            if (!AccelStruct.IsCompatibleWith(Res.GetAccelStruct(n)))
                IsCompatible = false;
        } //
    );
    return IsCompatible;
}

bool D3DShaderResourceAttribs::IsMultisample() const
{
    switch (GetSRVDimension())
    {
        case D3D_SRV_DIMENSION_TEXTURE2DMS:
        case D3D_SRV_DIMENSION_TEXTURE2DMSARRAY:
            return true;
        default:
            return false;
    }
}

HLSLShaderResourceDesc ShaderResources::GetHLSLShaderResourceDesc(Uint32 Index) const
{
    DEV_CHECK_ERR(Index < m_TotalResources, "Resource index (", Index, ") is out of range");
    HLSLShaderResourceDesc HLSLResourceDesc = {};
    if (Index < m_TotalResources)
    {
        const D3DShaderResourceAttribs& Res = GetResAttribs(Index, m_TotalResources, 0);
        return Res.GetHLSLResourceDesc();
    }
    return HLSLResourceDesc;
}

size_t ShaderResources::GetHash() const
{
    size_t hash = ComputeHash(GetNumCBs(), GetNumTexSRV(), GetNumTexUAV(), GetNumBufSRV(), GetNumBufUAV(), GetNumSamplers());
    for (Uint32 n = 0; n < m_TotalResources; ++n)
    {
        const D3DShaderResourceAttribs& Res = GetResAttribs(n, m_TotalResources, 0);
        HashCombine(hash, Res);
    }

    return hash;
}

} // namespace Diligent
