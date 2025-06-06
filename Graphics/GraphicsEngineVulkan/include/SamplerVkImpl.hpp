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
/// Declaration of Diligent::SamplerVkImpl class

#include "EngineVkImplTraits.hpp"
#include "SamplerBase.hpp"
#include "VulkanUtilities/ObjectWrappers.hpp"

namespace Diligent
{

/// Sampler object object implementation in Vulkan backend.
class SamplerVkImpl final : public SamplerBase<EngineVkImplTraits>
{
public:
    using TSamplerBase = SamplerBase<EngineVkImplTraits>;

    SamplerVkImpl(IReferenceCounters* pRefCounters, RenderDeviceVkImpl* pRenderDeviceVk, const SamplerDesc& SamplerDesc);
    // Special constructor for serialization
    SamplerVkImpl(IReferenceCounters* pRefCounters, const SamplerDesc& SamplerDesc) noexcept;
    ~SamplerVkImpl();

    IMPLEMENT_QUERY_INTERFACE_IN_PLACE(IID_SamplerVk, TSamplerBase)

    /// Implementation of ISamplerVk::GetVkSampler().
    virtual VkSampler DILIGENT_CALL_TYPE GetVkSampler() const override final { return m_VkSampler; }

private:
    friend class ShaderVkImpl;
    /// Vk sampler handle
    VulkanUtilities::SamplerWrapper m_VkSampler;
    static constexpr Uint64         m_ImmediateContextMask = ~Uint64{0};
};

} // namespace Diligent
