/*******************************************************************************
* Copyright 2019 Intel Corporation
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*     http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*******************************************************************************/

#ifndef GPU_OCL_RNN_RNN_REORDERS_HPP
#define GPU_OCL_RNN_RNN_REORDERS_HPP

#include "common/c_types_map.hpp"
#include "common/memory.hpp"
#include "common/utils.hpp"
#include "gpu/compute/compute.hpp"
#include "gpu/ocl/jit_primitive_conf.hpp"
#include "gpu/ocl/ocl_reorder_pd.hpp"
#include "gpu/ocl/ocl_utils.hpp"

namespace dnnl {
namespace impl {
namespace gpu {
namespace ocl {

status_t rnn_weights_reorder_init_conf(
        jit_rnn_reorder_conf_t &jrp, const reorder_pd_t *pd);
status_t rnn_weights_reorder_init_const_def(compute::kernel_ctx_t &kernel_ctx,
        const jit_rnn_reorder_conf_t &jrp, const memory_desc_wrapper &input_md,
        const memory_desc_wrapper &output_md);

struct rnn_weights_reorder_t : public primitive_impl_t {
    struct pd_t : public reorder_pd_t {
        using reorder_pd_t::reorder_pd_t;

        DECLARE_COMMON_PD_T("cross_engine::rnn", rnn_weights_reorder_t);

        DECLARE_OCL_REORDER_CREATE();

        status_t init() {
            if (!(dst_md()->extra.flags
                        & memory_extra_flags::gpu_rnn_u8s8_compensation))
                return status::unimplemented;

            bool args_ok = true
                    && utils::one_of(src_engine_->kind(), engine_kind::gpu,
                            engine_kind::cpu)
                    && dst_engine_->kind() == engine_kind::gpu;
            if (!args_ok) return status::unimplemented;

            auto *compute_engine
                    = utils::downcast<compute::compute_engine_t *>(dst_engine_);

            args_ok = args_ok
                    && compute_engine->mayiuse(
                            compute::device_ext_t::intel_subgroups)
                    && IMPLICATION(
                            utils::one_of(data_type::f16, src_md()->data_type,
                                    dst_md()->data_type),
                            true
                                    && compute_engine->mayiuse(
                                            compute::device_ext_t::khr_fp16)
                                    && compute_engine->mayiuse(
                                            compute::device_ext_t::
                                                    intel_subgroups_short));

            return rnn_weights_reorder_init_conf(jrp_, this);
        }

        jit_rnn_reorder_conf_t jrp_;
    };

    virtual status_t init() override {
        auto *compute_engine
                = utils::downcast<compute::compute_engine_t *>(engine());
        compute::kernel_ctx_t kernel_ctx;

        auto status = rnn_weights_reorder_init_const_def(
                kernel_ctx, pd()->jrp_, pd()->src_md(), pd()->dst_md());
        if (status != status::success) return status;

        compute_engine->create_kernel(&kernel_, "wei_reorder", kernel_ctx);
        if (!kernel_) return status::runtime_error;

        if (pd()->jrp_.do_reorder) {
            size_t size = pd()->jrp_.nelems * sizeof(float);
            memory_storage_t *temp_buf_ptr;
            engine()->create_memory_storage(&temp_buf_ptr, size);
            temp_buf.reset(temp_buf_ptr);
            if (!temp_buf) return status::runtime_error;

            size = pd()->jrp_.scales_count * sizeof(float);
            engine()->create_memory_storage(&temp_buf_ptr, size);
            scales_buf.reset(temp_buf_ptr);
            if (!scales_buf) return status::runtime_error;
        }

        return status::success;
    }

    rnn_weights_reorder_t(const pd_t *apd) : primitive_impl_t(apd) {}

    virtual status_t execute(const exec_ctx_t &ctx) const override;

private:
    const pd_t *pd() const { return (const pd_t *)primitive_impl_t::pd(); }
    compute::kernel_t kernel_;
    std::unique_ptr<memory_storage_t> temp_buf;
    std::unique_ptr<memory_storage_t> scales_buf;
};

} // namespace ocl
} // namespace gpu
} // namespace impl
} // namespace dnnl

#endif
