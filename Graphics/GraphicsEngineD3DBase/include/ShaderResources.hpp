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

#pragma once

/// \file
/// Declaration of Diligent::ShaderResources class
/// See http://diligentgraphics.com/diligent-engine/architecture/d3d12/shader-resources/

// ShaderResources class uses continuous chunk of memory to store all resources, as follows:
//
//
//       m_MemoryBuffer            m_TexSRVOffset                      m_TexUAVOffset                      m_BufSRVOffset                      m_BufUAVOffset                      m_SamplersOffset                            m_MemorySize
//        |                         |                                   |                                   |                                   |                                   |                           |                  |
//        |  CB[0]  ...  CB[Ncb-1]  |  TexSRV[0]  ...  TexSRV[Ntsrv-1]  |  TexUAV[0]  ...  TexUAV[Ntuav-1]  |  BufSRV[0]  ...  BufSRV[Nbsrv-1]  |  BufUAV[0]  ...  BufUAV[Nbuav-1]  |  Sam[0]  ...  Sam[Nsam-1] |  Resource Names  |
//
//  Ncb   - number of constant buffers
//  Ntsrv - number of texture SRVs
//  Ntuav - number of texture UAVs
//  Nbsrv - number of buffer SRVs
//  Nbuav - number of buffer UAVs
//  Nsam  - number of samplers
//
//
//  If texture SRV is assigned a sampler, it is cross-referenced through SamplerOrTexSRVId:
//
//                           _____________________SamplerOrTexSRVId___________________
//                          |                                                         |
//                          |                                                         V
//   |  CBs   |   ...   TexSRV[n] ...   | TexUAVs | BufSRVs | BufUAVs |  Sam[0] ...  Sam[SamplerId] ... |
//                          A                                                         |
//                          '---------------------SamplerOrTexSRVId-------------------'
//

#include <memory>

#include "WinHPreface.h"
#include <d3dcommon.h>
#include "WinHPostface.h"

#ifndef NTDDI_WIN10_VB // First defined in Win SDK 10.0.19041.0
#    define D3D_SIT_RTACCELERATIONSTRUCTURE (D3D_SIT_UAV_RWSTRUCTURED_WITH_COUNTER + 1)
#endif

#include "ShaderD3D.h"
#include "STDAllocator.hpp"
#include "HashUtils.hpp"
#include "StringPool.hpp"
#include "D3DShaderResourceLoader.hpp"
#include "PipelineState.h"
#include "D3DCommonTypeConversions.hpp"

namespace Diligent
{

// sizeof(D3DShaderResourceAttribs) == 16 (x64)
struct D3DShaderResourceAttribs
{
    // clang-format off

/* 0 */ const char* const Name;

/* 8 */ const Uint32 BindPoint;
/*12 */ const Uint32 BindCount;
/*16 */ const Uint32 Space;

    //            4               4                 24
    // bit | 0  1  2  3   |  4  5  6  7  |  8   9  10   ...   31  |
    //     |              |              |                        |
    //     |  InputType   |   SRV Dim    | SamplerOrTexSRVIdBits  |
    static constexpr const Uint32 ShaderInputTypeBits    =  4;
    static constexpr const Uint32 SRVDimBits             =  4;
    static constexpr const Uint32 SamplerOrTexSRVIdBits  = 24;
    static_assert(ShaderInputTypeBits + SRVDimBits + SamplerOrTexSRVIdBits == 32, "Attributes are better be packed into 32 bits");

    static_assert(D3D_SIT_UAV_RWSTRUCTURED_WITH_COUNTER < (1 << ShaderInputTypeBits), "Not enough bits to represent D3D_SHADER_INPUT_TYPE");
    static_assert(D3D_SRV_DIMENSION_BUFFEREX            < (1 << SRVDimBits),          "Not enough bits to represent D3D_SRV_DIMENSION");

private:
         // We need to use Uint32 instead of the actual type for reliability and correctness.
         // There originally was a problem when the type of InputType was D3D_SHADER_INPUT_TYPE:
         // the value of D3D_SIT_UAV_RWBYTEADDRESS (8) was interpreted as -8 (as the underlying enum type
         // is signed) causing errors
/*20.0*/ const Uint32  InputType          : ShaderInputTypeBits;     // Max value: D3D_SIT_UAV_RWSTRUCTURED_WITH_COUNTER == 11
/*20.4*/ const Uint32  SRVDimension       : SRVDimBits;              // Max value: D3D_SRV_DIMENSION_BUFFEREX == 11
/*21.0*/       Uint32  SamplerOrTexSRVId  : SamplerOrTexSRVIdBits;   // Max value: 2^24-1
/*24  */ // End of structure

    // clang-format on

public:
    static constexpr const Uint32 InvalidSamplerId = (1U << SamplerOrTexSRVIdBits) - 1U;
    static constexpr const Uint32 MaxSamplerId     = InvalidSamplerId - 1;
    static constexpr const Uint32 InvalidTexSRVId  = (1U << SamplerOrTexSRVIdBits) - 1U;
    static constexpr const auto   InvalidBindPoint = std::numeric_limits<decltype(BindPoint)>::max();


    D3DShaderResourceAttribs(const char*           _Name,
                             UINT                  _BindPoint,
                             UINT                  _BindCount,
                             UINT                  _Space,
                             D3D_SHADER_INPUT_TYPE _InputType,
                             D3D_SRV_DIMENSION     _SRVDimension,
                             Uint32                _SamplerId) noexcept :
        // clang-format off
        Name               {_Name},
        BindPoint          {_BindPoint},
        BindCount          {_BindCount},
        Space              {_Space},
        InputType          {static_cast<decltype(InputType)>   (_InputType)   },
        SRVDimension       {static_cast<decltype(SRVDimension)>(_SRVDimension)},
        SamplerOrTexSRVId  {_SamplerId}
    // clang-format on
    {
#ifdef DILIGENT_DEBUG
        // clang-format off
        VERIFY(_InputType    < (1 << ShaderInputTypeBits),   "Shader input type is out of expected range");
        VERIFY(_SRVDimension < (1 << SRVDimBits),            "SRV dimensions is out of expected range");
        VERIFY(_SamplerId    < (1 << SamplerOrTexSRVIdBits), "SamplerOrTexSRVId is out of representable range");
        // clang-format on

        if (_InputType == D3D_SIT_TEXTURE && _SRVDimension != D3D_SRV_DIMENSION_BUFFER)
            VERIFY_EXPR(GetCombinedSamplerId() == _SamplerId);
        else
            VERIFY(_SamplerId == InvalidSamplerId, "Only texture SRV can be assigned a valid texture sampler");
#endif
    }

    D3DShaderResourceAttribs(StringPool& NamesPool, const D3DShaderResourceAttribs& rhs, Uint32 _SamplerId) noexcept :
        // clang-format off
        D3DShaderResourceAttribs
        {
            NamesPool.CopyString(rhs.Name),
            rhs.BindPoint,
            rhs.BindCount,
            rhs.Space,
            rhs.GetInputType(),
            rhs.GetSRVDimension(),
            _SamplerId
        }
    // clang-format on
    {
        VERIFY(_SamplerId == InvalidSamplerId || (GetInputType() == D3D_SIT_TEXTURE && GetSRVDimension() != D3D_SRV_DIMENSION_BUFFER),
               "Only texture SRV can be assigned a valid texture sampler");
    }

    D3DShaderResourceAttribs(StringPool& NamesPool, const D3DShaderResourceAttribs& rhs) noexcept :
        // clang-format off
        D3DShaderResourceAttribs
        {
            NamesPool.CopyString(rhs.Name),
            rhs.BindPoint,
            rhs.BindCount,
            rhs.Space,
            rhs.GetInputType(),
            rhs.GetSRVDimension(),
            rhs.SamplerOrTexSRVId
        }
    // clang-format on
    {
    }

    // clang-format off
    D3DShaderResourceAttribs             (const D3DShaderResourceAttribs&  rhs) = delete;
    D3DShaderResourceAttribs             (      D3DShaderResourceAttribs&& rhs) = default; // Required for vector<D3DShaderResourceAttribs>
    D3DShaderResourceAttribs& operator = (const D3DShaderResourceAttribs&  rhs) = delete;
    D3DShaderResourceAttribs& operator = (      D3DShaderResourceAttribs&& rhs) = delete;
    // clang-format on

    D3D_SHADER_INPUT_TYPE GetInputType() const
    {
        return static_cast<D3D_SHADER_INPUT_TYPE>(InputType);
    }

    D3D_SRV_DIMENSION GetSRVDimension() const
    {
        return static_cast<D3D_SRV_DIMENSION>(SRVDimension);
    }

    RESOURCE_DIMENSION GetResourceDimension() const
    {
        return D3DSrvDimensionToResourceDimension(GetSRVDimension());
    }

    bool IsMultisample() const;

    bool IsCombinedWithSampler() const
    {
        return GetInputType() == D3D_SIT_TEXTURE && SamplerOrTexSRVId != InvalidSamplerId;
    }

    bool IsCombinedWithTexSRV() const
    {
        return GetCombinedTexSRVId() != InvalidTexSRVId;
    }

    bool IsValidBindPoint() const
    {
        return BindPoint != InvalidBindPoint;
    }

    bool IsCompatibleWith(const D3DShaderResourceAttribs& Attribs) const
    {
        return BindPoint == Attribs.BindPoint &&
            BindCount == Attribs.BindCount &&
            Space == Attribs.Space &&
            InputType == Attribs.InputType &&
            SRVDimension == Attribs.SRVDimension &&
            SamplerOrTexSRVId == Attribs.SamplerOrTexSRVId;
    }

    size_t GetHash() const
    {
        return ComputeHash(BindPoint, BindCount, Space, InputType, SRVDimension, SamplerOrTexSRVId);
    }

    HLSLShaderResourceDesc GetHLSLResourceDesc() const
    {
        HLSLShaderResourceDesc ResourceDesc;
        ResourceDesc.Name           = Name;
        ResourceDesc.ArraySize      = BindCount;
        ResourceDesc.ShaderRegister = BindPoint;
        ResourceDesc.RegisterSpace  = Space;
        ResourceDesc.Type           = GetShaderResourceType();

        return ResourceDesc;
    }

    Uint32 GetCombinedSamplerId() const
    {
        VERIFY(GetInputType() == D3D_SIT_TEXTURE && GetSRVDimension() != D3D_SRV_DIMENSION_BUFFER, "Invalid input type: D3D_SIT_TEXTURE is expected");
        return SamplerOrTexSRVId;
    }

    SHADER_RESOURCE_TYPE    GetShaderResourceType() const;
    PIPELINE_RESOURCE_FLAGS GetPipelineResourceFlags() const;

private:
    friend class ShaderResources;

    void SetTexSRVId(Uint32 TexSRVId)
    {
        VERIFY(GetInputType() == D3D_SIT_SAMPLER, "Invalid input type: D3D_SIT_SAMPLER is expected");
        VERIFY(TexSRVId < (1 << SamplerOrTexSRVIdBits), "TexSRVId (", TexSRVId, ") is out of representable range");
        SamplerOrTexSRVId = TexSRVId;
    }

    Uint32 GetCombinedTexSRVId() const
    {
        VERIFY(GetInputType() == D3D_SIT_SAMPLER, "Invalid input type: D3D_SIT_SAMPLER is expected");
        return SamplerOrTexSRVId;
    }
};
static_assert(sizeof(D3DShaderResourceAttribs) == sizeof(void*) + sizeof(Uint32) * 4, "Unexpected sizeof(D3DShaderResourceAttribs)");


/// Diligent::ShaderResources class
class ShaderResources
{
public:
    ShaderResources(SHADER_TYPE ShaderType) noexcept :
        m_ShaderType{ShaderType}
    {
    }

    // clang-format off
    ShaderResources             (const ShaderResources&)  = delete;
    ShaderResources             (      ShaderResources&&) = delete;
    ShaderResources& operator = (const ShaderResources&)  = delete;
    ShaderResources& operator = (      ShaderResources&&) = delete;
    // clang-format on

    ~ShaderResources();

    // clang-format off
    Uint32 GetNumCBs()         const noexcept { return (m_TexSRVOffset       - 0);                    }
    Uint32 GetNumTexSRV()      const noexcept { return (m_TexUAVOffset       - m_TexSRVOffset);       }
    Uint32 GetNumTexUAV()      const noexcept { return (m_BufSRVOffset       - m_TexUAVOffset);       }
    Uint32 GetNumBufSRV()      const noexcept { return (m_BufUAVOffset       - m_BufSRVOffset);       }
    Uint32 GetNumBufUAV()      const noexcept { return (m_SamplersOffset     - m_BufUAVOffset);       }
    Uint32 GetNumSamplers()    const noexcept { return (m_AccelStructsOffset - m_SamplersOffset);     }
    Uint32 GetNumAccelStructs()const noexcept { return (m_TotalResources     - m_AccelStructsOffset); }
    Uint32 GetTotalResources() const noexcept { return  m_TotalResources;                             }

    const D3DShaderResourceAttribs& GetCB         (Uint32 n)const noexcept { return GetResAttribs(n, GetNumCBs(),                       0);       }
    const D3DShaderResourceAttribs& GetTexSRV     (Uint32 n)const noexcept { return GetResAttribs(n, GetNumTexSRV(),       m_TexSRVOffset);       }
    const D3DShaderResourceAttribs& GetTexUAV     (Uint32 n)const noexcept { return GetResAttribs(n, GetNumTexUAV(),       m_TexUAVOffset);       }
    const D3DShaderResourceAttribs& GetBufSRV     (Uint32 n)const noexcept { return GetResAttribs(n, GetNumBufSRV(),       m_BufSRVOffset);       }
    const D3DShaderResourceAttribs& GetBufUAV     (Uint32 n)const noexcept { return GetResAttribs(n, GetNumBufUAV(),       m_BufUAVOffset);       }
    const D3DShaderResourceAttribs& GetSampler    (Uint32 n)const noexcept { return GetResAttribs(n, GetNumSamplers(),     m_SamplersOffset);     }
    const D3DShaderResourceAttribs& GetAccelStruct(Uint32 n)const noexcept { return GetResAttribs(n, GetNumAccelStructs(), m_AccelStructsOffset); }
    // clang-format on

    const D3DShaderResourceAttribs& GetCombinedSampler(const D3DShaderResourceAttribs& TexSRV) const noexcept
    {
        VERIFY(TexSRV.IsCombinedWithSampler(), "This texture SRV is not combined with any sampler");
        return GetSampler(TexSRV.GetCombinedSamplerId());
    }

    const D3DShaderResourceAttribs& GetCombinedTextureSRV(const D3DShaderResourceAttribs& Sampler) const noexcept
    {
        VERIFY(Sampler.IsCombinedWithTexSRV(), "This sampler is not combined with any texture SRV");
        return GetTexSRV(Sampler.GetCombinedTexSRVId());
    }

    SHADER_TYPE GetShaderType() const noexcept { return m_ShaderType; }

    HLSLShaderResourceDesc GetHLSLShaderResourceDesc(Uint32 Index) const;

    const ShaderCodeBufferDesc* GetConstantBufferDesc(Uint32 Index) const
    {
        if (Index >= GetNumCBs())
        {
            UNEXPECTED("Constant buffer index (", Index, ") is out of range.");
            return nullptr;
        }

        if (!m_CBReflectionBuffer)
        {
            UNEXPECTED("Constant buffer reflection information is not loaded. Please set the LoadConstantBufferReflection flag when creating the shader.");
            return nullptr;
        }

        return reinterpret_cast<const ShaderCodeBufferDesc*>(m_CBReflectionBuffer.get()) + Index;
    }

    template <typename THandleCB,
              typename THandleSampler,
              typename THandleTexSRV,
              typename THandleTexUAV,
              typename THandleBufSRV,
              typename THandleBufUAV,
              typename THandleAccelStruct>
    void ProcessResources(THandleCB          HandleCB,
                          THandleSampler     HandleSampler,
                          THandleTexSRV      HandleTexSRV,
                          THandleTexUAV      HandleTexUAV,
                          THandleBufSRV      HandleBufSRV,
                          THandleBufUAV      HandleBufUAV,
                          THandleAccelStruct HandleAccelStruct) const
    {
        for (Uint32 n = 0; n < GetNumCBs(); ++n)
        {
            const D3DShaderResourceAttribs& CB = GetCB(n);
            HandleCB(CB, n);
        }

        for (Uint32 n = 0; n < GetNumSamplers(); ++n)
        {
            const D3DShaderResourceAttribs& Sampler = GetSampler(n);
            HandleSampler(Sampler, n);
        }

        for (Uint32 n = 0; n < GetNumTexSRV(); ++n)
        {
            const D3DShaderResourceAttribs& TexSRV = GetTexSRV(n);
            HandleTexSRV(TexSRV, n);
        }

        for (Uint32 n = 0; n < GetNumTexUAV(); ++n)
        {
            const D3DShaderResourceAttribs& TexUAV = GetTexUAV(n);
            HandleTexUAV(TexUAV, n);
        }

        for (Uint32 n = 0; n < GetNumBufSRV(); ++n)
        {
            const D3DShaderResourceAttribs& BufSRV = GetBufSRV(n);
            HandleBufSRV(BufSRV, n);
        }

        for (Uint32 n = 0; n < GetNumBufUAV(); ++n)
        {
            const D3DShaderResourceAttribs& BufUAV = GetBufUAV(n);
            HandleBufUAV(BufUAV, n);
        }

        for (Uint32 n = 0; n < GetNumAccelStructs(); ++n)
        {
            const D3DShaderResourceAttribs& AS = GetAccelStruct(n);
            HandleAccelStruct(AS, n);
        }
    }
    template <typename THandler>
    void ProcessResources(THandler&& Handler) const
    {
        ProcessResources(Handler, Handler, Handler, Handler, Handler, Handler, Handler);
    }

    bool        IsCompatibleWith(const ShaderResources& Resources) const;
    bool        IsUsingCombinedTextureSamplers() const { return m_SamplerSuffix != nullptr; }
    const char* GetCombinedSamplerSuffix() const { return m_SamplerSuffix; }
    const Char* GetShaderName() const { return m_ShaderName; }

    size_t GetHash() const;

#ifdef DILIGENT_DEVELOPMENT
    static void DvpVerifyResourceLayout(const PipelineResourceLayoutDesc& ResourceLayout,
                                        const ShaderResources* const      pShaderResources[],
                                        Uint32                            NumShaders,
                                        bool                              VerifyVariables,
                                        bool                              VerifyImmutableSamplers) noexcept;
#endif

    void GetShaderModel(Uint32& Major, Uint32& Minor) const
    {
        Major = (m_ShaderVersion & 0x000000F0) >> 4;
        Minor = (m_ShaderVersion & 0x0000000F);
    }

protected:
    template <typename TD3DReflectionTraits,
              typename TShaderReflection,
              typename TNewResourceHandler>
    void Initialize(TShaderReflection*  pShaderReflection,
                    TNewResourceHandler NewResHandler,
                    const Char*         ShaderName,
                    const Char*         SamplerSuffix,
                    bool                LoadConstantBufferReflection);


    __forceinline D3DShaderResourceAttribs& GetResAttribs(Uint32 n, Uint32 NumResources, Uint32 Offset) noexcept
    {
        VERIFY(n < NumResources, "Resource index (", n, ") is out of range. Resource array size: ", NumResources);
        VERIFY_EXPR(Offset + n < m_TotalResources);
        return reinterpret_cast<D3DShaderResourceAttribs*>(m_MemoryBuffer.get())[Offset + n];
    }

    __forceinline const D3DShaderResourceAttribs& GetResAttribs(Uint32 n, Uint32 NumResources, Uint32 Offset) const noexcept
    {
        VERIFY(n < NumResources, "Resource index (", n, ") is out of range. Resource array size: ", NumResources);
        VERIFY_EXPR(Offset + n < m_TotalResources);
        return reinterpret_cast<const D3DShaderResourceAttribs*>(m_MemoryBuffer.get())[Offset + n];
    }

    // clang-format off
    D3DShaderResourceAttribs& GetCB(Uint32 n)         noexcept { return GetResAttribs(n, GetNumCBs(), 0); }
    D3DShaderResourceAttribs& GetTexSRV(Uint32 n)     noexcept { return GetResAttribs(n, GetNumTexSRV(), m_TexSRVOffset); }
    D3DShaderResourceAttribs& GetTexUAV(Uint32 n)     noexcept { return GetResAttribs(n, GetNumTexUAV(), m_TexUAVOffset); }
    D3DShaderResourceAttribs& GetBufSRV(Uint32 n)     noexcept { return GetResAttribs(n, GetNumBufSRV(), m_BufSRVOffset); }
    D3DShaderResourceAttribs& GetBufUAV(Uint32 n)     noexcept { return GetResAttribs(n, GetNumBufUAV(), m_BufUAVOffset); }
    D3DShaderResourceAttribs& GetSampler(Uint32 n)    noexcept { return GetResAttribs(n, GetNumSamplers(), m_SamplersOffset); }
    D3DShaderResourceAttribs& GetAccelStruct(Uint32 n)noexcept { return GetResAttribs(n, GetNumAccelStructs(), m_AccelStructsOffset); }
    // clang-format on

private:
    void AllocateMemory(IMemoryAllocator&                Allocator,
                        const D3DShaderResourceCounters& ResCounters,
                        size_t                           ResourceNamesPoolSize,
                        StringPool&                      ResourceNamesPool);

    Uint32 FindAssignedSamplerId(const D3DShaderResourceAttribs& TexSRV, const char* SamplerSuffix) const;

    // Memory buffer that holds all resources as continuous chunk of memory:
    // | CBs | TexSRVs | TexUAVs | BufSRVs | BufUAVs | Samplers |  Resource Names  |
    //

    std::unique_ptr<void, STDDeleterRawMem<void>> m_MemoryBuffer;
    std::unique_ptr<void, STDDeleterRawMem<void>> m_CBReflectionBuffer;

    const char* m_SamplerSuffix = nullptr; // The suffix and the shader name
    const char* m_ShaderName    = nullptr; // are put into the Resource Names section

    // Offsets in elements of D3DShaderResourceAttribs
    typedef Uint16 OffsetType;
    OffsetType     m_TexSRVOffset       = 0;
    OffsetType     m_TexUAVOffset       = 0;
    OffsetType     m_BufSRVOffset       = 0;
    OffsetType     m_BufUAVOffset       = 0;
    OffsetType     m_SamplersOffset     = 0;
    OffsetType     m_AccelStructsOffset = 0;
    OffsetType     m_TotalResources     = 0;

    const SHADER_TYPE m_ShaderType;

    Uint32 m_ShaderVersion = 0;
};


template <typename TD3DReflectionTraits,
          typename TShaderReflection,
          typename TNewResourceHandler>
void ShaderResources::Initialize(TShaderReflection*  pShaderReflection,
                                 TNewResourceHandler NewResHandler,
                                 const Char*         ShaderName,
                                 const Char*         CombinedSamplerSuffix,
                                 bool                LoadConstantBufferReflection)
{
    using D3D_SHADER_DESC = typename TD3DReflectionTraits::D3D_SHADER_DESC;

    Uint32 CurrCB = 0, CurrTexSRV = 0, CurrTexUAV = 0, CurrBufSRV = 0, CurrBufUAV = 0, CurrSampler = 0, CurrAS = 0;

    // Resource names pool is only needed to facilitate string allocation.
    StringPool ResourceNamesPool;

    // Constant buffer reflections
    std::vector<ShaderCodeBufferDescX> CBReflections;

    LoadD3DShaderResources<D3DShaderResourceAttribs, TD3DReflectionTraits>(
        pShaderReflection, LoadConstantBufferReflection,

        [&](const D3D_SHADER_DESC& d3dShaderDesc) //
        {
            m_ShaderVersion = d3dShaderDesc.Version;
        },

        [&](const D3DShaderResourceCounters& ResCounters, size_t ResourceNamesPoolSize) //
        {
            VERIFY_EXPR(ShaderName != nullptr);
            ResourceNamesPoolSize += strlen(ShaderName) + 1;

            if (CombinedSamplerSuffix != nullptr)
                ResourceNamesPoolSize += strlen(CombinedSamplerSuffix) + 1;

            AllocateMemory(GetRawAllocator(), ResCounters, ResourceNamesPoolSize, ResourceNamesPool);
        },

        [&](const D3DShaderResourceAttribs& CBAttribs, ShaderCodeBufferDescX&& CBReflection) //
        {
            VERIFY_EXPR(CBAttribs.GetInputType() == D3D_SIT_CBUFFER);
            D3DShaderResourceAttribs* pNewCB = new (&GetCB(CurrCB++)) D3DShaderResourceAttribs{ResourceNamesPool, CBAttribs};
            NewResHandler.OnNewCB(*pNewCB);
            if (LoadConstantBufferReflection)
                CBReflections.emplace_back(std::move(CBReflection));
        },

        [&](const D3DShaderResourceAttribs& TexUAV) //
        {
            VERIFY_EXPR(TexUAV.GetInputType() == D3D_SIT_UAV_RWTYPED && TexUAV.GetSRVDimension() != D3D_SRV_DIMENSION_BUFFER);
            D3DShaderResourceAttribs* pNewTexUAV = new (&GetTexUAV(CurrTexUAV++)) D3DShaderResourceAttribs{ResourceNamesPool, TexUAV};
            NewResHandler.OnNewTexUAV(*pNewTexUAV);
        },

        [&](const D3DShaderResourceAttribs& BuffUAV) //
        {
            VERIFY_EXPR(BuffUAV.GetInputType() == D3D_SIT_UAV_RWTYPED && BuffUAV.GetSRVDimension() == D3D_SRV_DIMENSION_BUFFER || BuffUAV.GetInputType() == D3D_SIT_UAV_RWSTRUCTURED || BuffUAV.GetInputType() == D3D_SIT_UAV_RWBYTEADDRESS);
            D3DShaderResourceAttribs* pNewBufUAV = new (&GetBufUAV(CurrBufUAV++)) D3DShaderResourceAttribs{ResourceNamesPool, BuffUAV};
            NewResHandler.OnNewBuffUAV(*pNewBufUAV);
        },

        [&](const D3DShaderResourceAttribs& BuffSRV) //
        {
            VERIFY_EXPR(BuffSRV.GetInputType() == D3D_SIT_TEXTURE && BuffSRV.GetSRVDimension() == D3D_SRV_DIMENSION_BUFFER || BuffSRV.GetInputType() == D3D_SIT_STRUCTURED || BuffSRV.GetInputType() == D3D_SIT_BYTEADDRESS);
            D3DShaderResourceAttribs* pNewBuffSRV = new (&GetBufSRV(CurrBufSRV++)) D3DShaderResourceAttribs{ResourceNamesPool, BuffSRV};
            NewResHandler.OnNewBuffSRV(*pNewBuffSRV);
        },

        [&](const D3DShaderResourceAttribs& SamplerAttribs) //
        {
            VERIFY_EXPR(SamplerAttribs.GetInputType() == D3D_SIT_SAMPLER);
            D3DShaderResourceAttribs* pNewSampler = new (&GetSampler(CurrSampler++)) D3DShaderResourceAttribs{ResourceNamesPool, SamplerAttribs};
            NewResHandler.OnNewSampler(*pNewSampler);
        },

        [&](const D3DShaderResourceAttribs& TexAttribs) //
        {
            VERIFY_EXPR(TexAttribs.GetInputType() == D3D_SIT_TEXTURE && TexAttribs.GetSRVDimension() != D3D_SRV_DIMENSION_BUFFER);
            VERIFY(CurrSampler == GetNumSamplers(), "All samplers must be initialized before texture SRVs");

            Uint32                    SamplerId  = CombinedSamplerSuffix != nullptr ? FindAssignedSamplerId(TexAttribs, CombinedSamplerSuffix) : D3DShaderResourceAttribs::InvalidSamplerId;
            D3DShaderResourceAttribs* pNewTexSRV = new (&GetTexSRV(CurrTexSRV)) D3DShaderResourceAttribs{ResourceNamesPool, TexAttribs, SamplerId};
            if (SamplerId != D3DShaderResourceAttribs::InvalidSamplerId)
            {
                GetSampler(SamplerId).SetTexSRVId(CurrTexSRV);
            }
            ++CurrTexSRV;
            NewResHandler.OnNewTexSRV(*pNewTexSRV);
        },

        [&](const D3DShaderResourceAttribs& AccelStructAttribs) //
        {
            VERIFY_EXPR(AccelStructAttribs.GetInputType() == D3D_SIT_RTACCELERATIONSTRUCTURE);
            D3DShaderResourceAttribs* pNewAccelStruct = new (&GetAccelStruct(CurrAS++)) D3DShaderResourceAttribs{ResourceNamesPool, AccelStructAttribs};
            NewResHandler.OnNewAccelStruct(*pNewAccelStruct);
        } //
    );

    m_ShaderName = ResourceNamesPool.CopyString(ShaderName);

    if (CombinedSamplerSuffix != nullptr)
    {
        m_SamplerSuffix = ResourceNamesPool.CopyString(CombinedSamplerSuffix);

#ifdef DILIGENT_DEVELOPMENT
        for (Uint32 n = 0; n < GetNumSamplers(); ++n)
        {
            const D3DShaderResourceAttribs& Sampler = GetSampler(n);
            if (!Sampler.IsCombinedWithTexSRV())
                LOG_ERROR_MESSAGE("Shader '", ShaderName, "' uses combined texture samplers, but sampler '", Sampler.Name, "' is not assigned to any texture");
        }
#endif
    }

    VERIFY_EXPR(ResourceNamesPool.GetRemainingSize() == 0);
    // clang-format off
    VERIFY(CurrCB      == GetNumCBs(),          "Not all CBs are initialized which will cause a crash when ~D3DShaderResourceAttribs() is called");
    VERIFY(CurrTexSRV  == GetNumTexSRV(),       "Not all Tex SRVs are initialized which will cause a crash when ~D3DShaderResourceAttribs() is called" );
    VERIFY(CurrTexUAV  == GetNumTexUAV(),       "Not all Tex UAVs are initialized which will cause a crash when ~D3DShaderResourceAttribs() is called" );
    VERIFY(CurrBufSRV  == GetNumBufSRV(),       "Not all Buf SRVs are initialized which will cause a crash when ~D3DShaderResourceAttribs() is called" );
    VERIFY(CurrBufUAV  == GetNumBufUAV(),       "Not all Buf UAVs are initialized which will cause a crash when ~D3DShaderResourceAttribs() is called" );
    VERIFY(CurrSampler == GetNumSamplers(),     "Not all Samplers are initialized which will cause a crash when ~D3DShaderResourceAttribs() is called" );
    VERIFY(CurrAS      == GetNumAccelStructs(), "Not all Accel Structs are initialized which will cause a crash when ~D3DShaderResourceAttribs() is called" );
    // clang-format on

    if (!CBReflections.empty())
    {
        VERIFY_EXPR(LoadConstantBufferReflection);
        VERIFY_EXPR(CBReflections.size() == GetNumCBs());
        m_CBReflectionBuffer = ShaderCodeBufferDescX::PackArray(CBReflections.cbegin(), CBReflections.cend(), GetRawAllocator());
    }
}

} // namespace Diligent

namespace std
{

template <>
struct hash<Diligent::D3DShaderResourceAttribs>
{
    size_t operator()(const Diligent::D3DShaderResourceAttribs& Attribs) const
    {
        return Attribs.GetHash();
    }
};

template <>
struct hash<Diligent::ShaderResources>
{
    size_t operator()(const Diligent::ShaderResources& Res) const
    {
        return Res.GetHash();
    }
};

} // namespace std
