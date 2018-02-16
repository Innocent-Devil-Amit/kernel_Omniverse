/* Copyright (c) 2014-2017, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#define pr_fmt(fmt)	"%s: " fmt, __func__

#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/sync.h>
#include <linux/uaccess.h>
#include <linux/of.h>
#include <linux/clk.h>
#include <linux/msm-bus.h>
#include <linux/msm-bus-board.h>
#include <linux/regulator/consumer.h>

#include "mdss_rotator_internal.h"
#include "mdss_mdp.h"
#include "mdss_debug.h"

/* waiting for hw time out, 3 vsync for 30fps*/
#define ROT_HW_ACQUIRE_TIMEOUT_IN_MS 100

/* acquire fence time out, following other driver fence time out practice */
#define ROT_FENCE_WAIT_TIMEOUT MSEC_PER_SEC
/*
 * Max rotator hw blocks possible. Used for upper array limits instead of
 * alloc and freeing small array
 */
#define ROT_MAX_HW_BLOCKS 2

#define ROT_CHECK_BOUNDS(offset, size, max_size) \
	(((size) > (max_size)) || ((offset) > ((max_size) - (size))))

#define CLASS_NAME "rotator"
#define DRIVER_NAME "mdss_rotator"

#define MDP_REG_BUS_VECTOR_ENTRY(ab_val, ib_val)	\
	{						\
		.src = MSM_BUS_MASTER_AMPSS_M0,		\
		.dst = MSM_BUS_SLAVE_DISPLAY_CFG,	\
		.ab = (ab_val),				\
		.ib = (ib_val),				\
	}

#define BUS_VOTE_19_MHZ 153600000

static struct msm_bus_vectors rot_reg_bus_vectors[] = {
	MDP_REG_BUS_VECTOR_ENTRY(0, 0),
	MDP_REG_BUS_VECTOR_ENTRY(0, BUS_VOTE_19_MHZ),
};
static struct msm_bus_paths rot_reg_bus_usecases[ARRAY_SIZE(
		rot_reg_bus_vectors)];
static struct msm_bus_scale_pdata rot_reg_bus_scale_table = {
	.usecase = rot_reg_bus_usecases,
	.num_usecases = ARRAY_SIZE(rot_reg_bus_usecases),
	.name = "mdss_rot_reg",
	.active_only = 1,
};

static struct mdss_rot_mgr *rot_mgr;
static void mdss_rotator_wq_handler(struct work_struct *work);

static int mdss_rotator_bus_scale_set_quota(struct mdss_rot_bus_data_type *bus,
		u64 quota)
{
	int new_uc_idx;
	int ret;

	if (bus->bus_hdl < 1) {
		pr_err("invalid bus handle %d\n", bus->bus_hdl);
		return -EINVAL;
	}

	if (bus->curr_quota_val == quota) {
		pr_debug("bw request already requested\n");
		return 0;
	}

	if (!quota) {
		new_uc_idx = 0;
	} else {
		struct msm_bus_vectors *vect = NULL;
		struct msm_bus_scale_pdata *bw_table =
			bus->bus_scale_pdata;
		u64 port_quota = quota;
		u32 total_axi_port_cnt;
		int i;

		new_uc_idx = (bus->curr_bw_uc_idx %
			(bw_table->num_usecases - 1)) + 1;

		total_axi_port_cnt = bw_table->usecase[new_uc_idx].num_paths;
		if (total_axi_port_cnt == 0) {
			pr_err("Number of bw paths is 0\n");
			return -ENODEV;
		}
		do_div(port_quota, total_axi_port_cnt);

		for (i = 0; i < total_axi_port_cnt; i++) {
			vect = &bw_table->usecase[new_uc_idx].vectors[i];
			vect->ab = port_quota;
			vect->ib = 0;
		}
	}
	bus->curr_bw_uc_idx = new_uc_idx;
	bus->curr_quota_val = quota;

	pr_debug("uc_idx=%d quota=%llu\n", new_uc_idx, quota);
	MDSS_XLOG(new_uc_idx, ((quota >> 32) & 0xFFFFFFFF),
		(quota & 0xFFFFFFFF));
	ATRACE_BEGIN("msm_bus_scale_req_rot");
	ret = msm_bus_scale_client_update_request(bus->bus_hdl,
		new_uc_idx);
	ATRACE_END("msm_bus_scale_req_rot");
	return ret;
}

static int mdss_rotator_enable_reg_bus(struct mdss_rot_mgr *mgr, u64 quota)
{
	int ret = 0, changed = 0;
	u32 usecase_ndx = 0;

	if (!mgr || !mgr->reg_bus.bus_hdl)
		return 0;

	if (quota)
		usecase_ndx = 1;

	if (usecase_ndx != mgr->reg_bus.curr_bw_uc_idx) {
		mgr->reg_bus.curr_bw_uc_idx = usecase_ndx;
		changed++;
	}

	pr_debug("%s, changed=%d register bus %s\n", __func__, changed,
		quota ? "Enable":"Disable");

	if (changed) {
		ATRACE_BEGIN("msm_bus_scale_req_rot_reg");
		ret = msm_bus_scale_client_update_request(mgr->reg_bus.bus_hdl,
			usecase_ndx);
		ATRACE_END("msm_bus_scale_req_rot_reg");
	}

	return ret;
}

/*
 * Clock rate of all open sessions working a particular hw block
 * are added together to get the required rate for that hw block.
 * The max of each hw block becomes the final clock rate voted for
 */
static unsigned long mdss_rotator_clk_rate_calc(
	struct mdss_rot_mgr *mgr,
	struct mdss_rot_file_private *private)
{
	struct mdss_rot_perf *perf;
	unsigned long clk_rate[ROT_MAX_HW_BLOCKS] = {0};
	unsigned long total_clk_rate = 0;
	int i, wb_idx;

	mutex_lock(&private->perf_lock);
	list_for_each_entry(perf, &private->perf_list, list) {
		bool rate_accounted_for = false;
		mutex_lock(&perf->work_dis_lock);
		/*
		 * If there is one session that has two work items across
		 * different hw blocks rate is accounted for in both blocks.
		 */
		for (i = 0; i < mgr->queue_count; i++) {
			if (perf->work_distribution[i]) {
				clk_rate[i] += perf->clk_rate;
				rate_accounted_for = true;
			}
		}

		/*
		 * Sessions that are open but not distributed on any hw block
		 * Still need to be accounted for. Rate is added to last known
		 * wb idx.
		 */
		wb_idx = perf->last_wb_idx;
		if ((!rate_accounted_for) && (wb_idx >= 0) &&
				(wb_idx < mgr->queue_count))
			clk_rate[wb_idx] += perf->clk_rate;
		mutex_unlock(&perf->work_dis_lock);
	}
	mutex_unlock(&private->perf_lock);

	for (i = 0; i < mgr->queue_count; i++)
		total_clk_rate = max(clk_rate[i], total_clk_rate);

	pr_debug("Total clk rate calc=%lu\n", total_clk_rate);
	return total_clk_rate;
}

static struct clk *mdss_rotator_get_clk(struct mdss_rot_mgr *mgr, u32 clk_idx)
{
	if (clk_idx >= MDSS_CLK_ROTATOR_END_IDX) {
		pr_err("Invalid clk index:%u", clk_idx);
		return NULL;
	}

	return mgr->rot_clk[clk_idx];
}

static void mdss_rotator_set_clk_rate(struct mdss_rot_mgr *mgr,
		unsigned long rate, u32 clk_idx)
{
	unsigned long clk_rate;
	struct clk *clk = mdss_rotator_get_clk(mgr, clk_idx);
	int ret;

	if (clk) {
		mutex_lock(&mgr->clk_lock);
		clk_rate = clk_round_rate(clk, rate);
		if (IS_ERR_VALUE(clk_rate)) {
			pr_err("unable to round rate err=%ld\n", clk_rate);
		} else if (clk_rate != clk_get_rate(clk)) {
			ret = clk_set_rate(clk, clk_rate);
			if (IS_ERR_VALUE(ret)) {
				pr_err("clk_set_rate failed, err:%d\n", ret);
			} else {
				pr_debug("rotator clk rate=%lu\n", clk_rate);
				MDSS_XLOG(clk_rate);
			}
		}
		mutex_unlock(&mgr->clk_lock);
	} else {
		pr_err("rotator clk not setup properly\n");
	}
}

static void mdss_rotator_footswitch_ctrl(struct mdss_rot_mgr *mgr, bool on)
{
	int ret;

	if (mgr->regulator_enable == on) {
		pr_err("Regulators already in selected mode on=%d\n", on);
		return;
	}

	pr_debug("%s: rotator regulators", on ? "Enable" : "Disable");
	ret = msm_dss_enable_vreg(mgr->module_power.vreg_config,
		mgr->module_power.num_vreg, on);
	if (ret) {
		pr_warn("Rotator regulator failed to %s\n",
			on ? "enable" : "disable");
		return;
	}

	mgr->regulator_enable = on;
}

static int mdss_rotator_clk_ctrl(struct mdss_rot_mgr *mgr, int enable)
{
	struct clk *clk;
	int ret = 0;
	int i, changed = 0;

	mutex_lock(&mgr->clk_lock);
	if (enable) {
		if (mgr->rot_enable_clk_cnt == 0)
			changed++;
		mgr->rot_enable_clk_cnt++;
	} else {
		if (mgr->rot_enable_clk_cnt) {
			mgr->rot_enable_clk_cnt--;
			if (mgr->rot_enable_clk_cnt == 0)
				changed++;
		} else {
			pr_err("Can not be turned off\n");
		}
	}

	if (changed) {
		pr_debug("Rotator clk %s\n", enable ? "enable" : "disable");
		for (i = 0; i < MDSS_CLK_ROTATOR_END_IDX; i++) {
			clk = mgr->rot_clk[i];
			if (enable) {
				ret = clk_prepare_enable(clk);
				if (ret) {
					pr_err("enable failed clk_idx %d\n", i);
					goto error;
				}
			} else {
				clk_disable_unprepare(clk);
			}
		}
		mutex_lock(&mgr->bus_lock);
		if (enable) {
			/* Active+Sleep */
			msm_bus_scale_client_update_context(
				mgr->data_bus.bus_hdl, false,
				mgr->data_bus.curr_bw_uc_idx);
			trace_rotator_bw_ao_as_context(0);
		} else {
			/* Active Only */
			msm_bus_scale_client_update_context(
				mgr->data_bus.bus_hdl, true,
				mgr->data_bus.curr_bw_uc_idx);
			trace_rotator_bw_ao_as_context(1);
		}
		mutex_unlock(&mgr->bus_lock);
	}
	mutex_unlock(&mgr->clk_lock);

	return ret;
error:
	for (i--; i >= 0; i--)
		clk_disable_unprepare(mgr->rot_clk[i]);
	mutex_unlock(&mgr->clk_lock);
	return ret;
}

int mdss_rotator_resource_ctrl(struct mdss_rot_mgr *mgr, int enable)
{
	int changed = 0;
	int ret = 0;

	mutex_lock(&mgr->clk_lock);
	if (enable) {
		if (mgr->res_ref_cnt == 0)
			changed++;
		mgr->res_ref_cnt++;
	} else {
		if (mgr->res_ref_cnt) {
			mgr->res_ref_cnt--;
			if (mgr->res_ref_cnt == 0)
				changed++;
		} else {
			pr_err("Rot resource already off\n");
		}
	}

	pr_debug("%s: res_cnt=%d changed=%d enable=%d\n",
		__func__, mgr->res_ref_cnt, changed, enable);
	MDSS_XLOG(mgr->res_ref_cnt, changed, enable);

	if (changed) {
		if (enable)
			mdss_rotator_footswitch_ctrl(mgr, true);
		else
			mdss_rotator_footswitch_ctrl(mgr, false);
	}
	mutex_unlock(&mgr->clk_lock);
	return ret;
}

/* caller is expected to hold perf->work_dis_lock lock */
static bool mdss_rotator_is_work_pending(struct mdss_rot_mgr *mgr,
	struct mdss_rot_perf *perf)
{
	int i;

	for (i = 0; i < mgr->queue_count; i++) {
		if (perf->work_distribution[i]) {
			pr_debug("Work is still scheduled to complete\n");
			return true;
		}
	}
	return false;
}

static void mdss_rotator_install_fence_fd(struct mdss_rot_entry_container *req)
{
	int i = 0;

	for (i = 0; i < req->count; i++)
		sync_fence_install(req->entries[i].output_fence,
				req->entries[i].output_fence_fd);
}

static int mdss_rotator_create_fence(struct mdss_rot_entry *entry)
{
	int ret = 0, fd;
	u32 val;
	struct sync_pt *sync_pt;
	struct sync_fence *fence;
	struct mdss_rot_timeline *rot_timeline;

	if (!entry->queue)
		return -EINVAL;

	rot_timeline = &entry->queue->timeline;

	mutex_lock(&rot_timeline->lock);
	val = rot_timeline->next_value + 1;

	sync_pt = sw_sync_pt_create(rot_timeline->timeline, val);
	if (sync_pt == NULL) {
		pr_err("cannot create sync point\n");
		goto sync_pt_create_err;
	}

	/* create fence */
	fence = sync_fence_create(rot_timeline->fence_name, sync_pt);
	if (fence == NULL) {
		pr_err("%s: cannot create fence\n", rot_timeline->fence_name);
		sync_pt_free(sync_pt);
		ret = -ENOMEM;
		goto sync_pt_create_err;
	}

	fd = get_unused_fd_flags(0);
	if (fd < 0) {
		pr_err("get_unused_fd_flags failed error:0x%x\n", fd);
		ret = fd;
		goto get_fd_err;
	}

	rot_timeline->next_value++;
	mutex_unlock(&rot_timeline->lock);

	entry->output_fence_fd = fd;
	entry->output_fence = fence;
	pr_debug("output sync point created at val=%u\n", val);

	return 0;

get_fd_err:
	sync_fence_put(fence);
sync_pt_create_err:
	mutex_unlock(&rot_timeline->lock);
	return ret;
}

static void mdss_rotator_clear_fence(struct mdss_rot_entry *entry)
{
	struct mdss_rot_timeline *rot_timeline;

	if (entry->input_fence) {
		sync_fence_put(entry->input_fence);
		entry->input_fence = NULL;
	}

	rot_timeline = &entry->queue->timeline;

	/* fence failed to copy to user space */
	if (entry->output_fence) {
		sync_fence_put(entry->output_fence);
		entry->output_fence = NULL;
		put_unused_fd(entry->output_fence_fd);

		mutex_lock(&rot_timeline->lock);
		rot_timeline->next_value--;
		mutex_unlock(&rot_timeline->lock);
	}
}

static int mdss_rotator_signal_output(struct mdss_rot_entry *entry)
{
	struct mdss_rot_timeline *rot_timeline;

	if (!entry->queue)
		return -EINVAL;

	rot_timeline = &entry->queue->timeline;

	if (entry->output_signaled) {
		pr_debug("output already signaled\n");
		return 0;
	}

	mutex_lock(&rot_timeline->lock);
	sw_sync_timeline_inc(rot_timeline->timeline, 1);
	mutex_unlock(&rot_timeline->lock);

	entry->output_signaled = true;

	return 0;
}

static int mdss_rotator_wait_for_input(struct mdss_rot_entry *entry)
{
	int ret;

	if (!entry->input_fence) {
		pr_debug("invalid input fence, no wait\n");
		return 0;
	}

	ret = sync_fence_wait(entry->input_fence, ROT_FENCE_WAIT_TIMEOUT);
	sync_fence_put(entry->input_fence);
	entry->input_fence = NULL;
	return ret;
}

static int mdss_rotator_import_buffer(struct mdp_layer_buffer *buffer,
	struct mdss_mdp_data *data, u32 flags, struct device *dev, bool input)
{
	int i, ret = 0;
	struct msmfb_data planes[MAX_PLANES];
	int dir = DMA_TO_DEVICE;

	if (!input)
		dir = DMA_FROM_DEVICE;

	memset(planes, 0, sizeof(planes));

	if (buffer->plane_count > MAX_PLANES) {
		pr_err("buffer plane_count exceeds MAX_PLANES limit:%d\n",
				buffer->plane_count);
		return -EINVAL;
	}

	for (i = 0; i < buffer->plane_count; i++) {
		planes[i].memory_id = buffer->planes[i].fd;
		planes[i].offset = buffer->planes[i].offset;
	}

	ret =  mdss_mdp_data_get_and_validate_size(data, planes,
			buffer->plane_count, flags, dev, true, dir, buffer);
	data->state = MDP_BUF_STATE_READY;
	data->last_alloc = local_clock();

	return ret;
}

static int mdss_rotator_map_and_check_data(struct mdss_rot_entry *entry)
{
	int ret;
	struct mdp_layer_buffer *input;
	struct mdp_layer_buffer *output;
	struct mdss_mdp_format_params *fmt;
	struct mdss_mdp_plane_sizes ps;
	bool rotation;

	input = &entry->item.input;
	output = &entry->item.output;

	rotation = (entry->item.flags &  MDP_ROTATION_90) ? true : false;

	ATRACE_BEGIN(__func__);
	ret = mdss_iommu_ctrl(1);
	if (IS_ERR_VALUE(ret)) {
		ATRACE_END(__func__);
		return ret;
	}

	/* if error during map, the caller will release the data */
	entry->src_buf.state = MDP_BUF_STATE_ACTIVE;
	ret = mdss_mdp_data_map(&entry->src_buf, true, DMA_TO_DEVICE);
	if (ret) {
		pr_err("source buffer mapping failed ret:%d\n", ret);
		goto end;
	}

	entry->dst_buf.state = MDP_BUF_STATE_ACTIVE;
	ret = mdss_mdp_data_map(&entry->dst_buf, true, DMA_FROM_DEVICE);
	if (ret) {
		pr_err("destination buffer mapping failed ret:%d\n", ret);
		goto end;
	}

	fmt = mdss_mdp_get_format_params(input->format);
	if (!fmt) {
		pr_err("invalid input format:%d\n", input->format);
		ret = -EINVAL;
		goto end;
	}

	ret = mdss_mdp_get_plane_sizes(
			fmt, input->width, input->height, &ps, 0, rotation);
	if (ret) {
		pr_err("fail to get input plane size ret=%d\n", ret);
		goto end;
	}

	ret = mdss_mdp_data_check(&entry->src_buf, &ps, fmt);
	if (ret) {
		pr_err("fail to check input data ret=%d\n", ret);
		goto end;
	}

	fmt = mdss_mdp_get_format_params(output->format);
	if (!fmt) {
		pr_err("invalid output format:%d\n", output->format);
		ret = -EINVAL;
		goto end;
	}

	ret = mdss_mdp_get_plane_sizes(
			fmt, output->width, output->height, &ps, 0, rotation);
	if (ret) {
		pr_err("fail to get output plane size ret=%d\n", ret);
		goto end;
	}

	ret = mdss_mdp_data_check(&entry->dst_buf, &ps, fmt);
	if (ret) {
		pr_err("fail to check output data ret=%d\n", ret);
		goto end;
	}

end:
	mdss_iommu_ctrl(0);
	ATRACE_END(__func__);

	return ret;
}

static struct mdss_rot_perf *__mdss_rotator_find_session(
	struct mdss_rot_file_private *private,
	u32 session_id)
{
	struct mdss_rot_perf *perf, *perf_next;
	bool found = false;
	list_for_each_entry_safe(perf, perf_next, &private->perf_list, list) {
		if (perf->config.session_id == session_id) {
			found = true;
			break;
		}
	}
	if (!found)
		perf = NULL;
	return perf;
}

static struct mdss_rot_perf *mdss_rotator_find_session(
	struct mdss_rot_file_private *private,
	u32 session_id)
{
	struct mdss_rot_perf *perf;

	mutex_lock(&private->perf_lock);
	perf = __mdss_rotator_find_session(private, session_id);
	mutex_unlock(&private->perf_lock);
	return perf;
}

static void mdss_rotator_release_data(struct mdss_rot_entry *entry)
{
	struct mdss_mdp_data *src_buf = &entry->src_buf;
	struct mdss_mdp_data *dst_buf = &entry->dst_buf;

	mdss_mdp_data_free(src_buf, true, DMA_TO_DEVICE);
	src_buf->last_freed = local_clock();
	src_buf->state = MDP_BUF_STATE_UNUSED;

	mdss_mdp_data_free(dst_buf, true, DMA_FROM_DEVICE);
	dst_buf->last_freed = local_clock();
	dst_buf->state = MDP_BUF_STATE_UNUSED;
}

static int mdss_rotator_import_data(struct mdss_rot_mgr *mgr,
	struct mdss_rot_entry *entry)
{
	int ret;
	struct mdp_layer_buffer *input;
	struct mdp_layer_buffer *output;
	u32 flag = 0;

	input = &entry->item.input;
	output = &entry->item.output;

	if (entry->item.flags & MDP_ROTATION_SECURE)
		flag = MDP_SECURE_OVERLAY_SESSION;

	ret = mdss_rotator_import_buffer(input, &entry->src_buf, flag,
				&mgr->pdev->dev, true);
	if (ret) {
		pr_err("fail to import input buffer\n");
		return ret;
	}

	/*
	 * driver assumes ouput buffer is ready to be written
	 * immediately
	 */
	ret = mdss_rotator_import_buffer(output, &entry->dst_buf, flag,
				&mgr->pdev->dev, false);
	if (ret) {
		pr_err("fail to import output buffer\n");
		return ret;
	}

	return ret;
}

static struct mdss_rot_hw_resource *mdss_rotator_hw_alloc(
	struct mdss_rot_mgr *mgr, u32 pipe_id, u32 wb_id)
{
	struct mdss_rot_hw_resource *hw;
	struct mdss_data_type *mdata = mdss_mdp_get_mdata();
	u32 pipe_ndx, offset = mdss_mdp_get_wb_ctl_support(mdata, true);
	int ret;

	hw = devm_kzalloc(&mgr->pdev->dev, sizeof(struct mdss_rot_hw_resource),
		GFP_KERNEL);
	if (!hw)
		return ERR_PTR(-ENOMEM);

	hw->ctl = mdss_mdp_ctl_alloc(mdata, offset);
	if (IS_ERR_OR_NULL(hw->ctl)) {
		pr_err("unable to allocate ctl\n");
		ret = -ENODEV;
		goto error;
	}

	if (wb_id == MDSS_ROTATION_HW_ANY)
		hw->wb = mdss_mdp_wb_alloc(MDSS_MDP_WB_ROTATOR, hw->ctl->num);
	else
		hw->wb = mdss_mdp_wb_assign(wb_id, hw->ctl->num);

	if (IS_ERR_OR_NULL(hw->wb)) {
		pr_err("unable to allocate wb\n");
		ret = -ENODEV;
		goto error;
	}
	hw->ctl->wb = hw->wb;
	hw->mixer = mdss_mdp_mixer_assign(hw->wb->num, true, true);

	if (IS_ERR_OR_NULL(hw->mixer)) {
		pr_err("unable to allocate wb mixer\n");
		ret = -ENODEV;
		goto error;
	}
	hw->ctl->mixer_left = hw->mixer;
	hw->mixer->ctl = hw->ctl;

	hw->mixer->rotator_mode = true;

	switch (hw->mixer->num) {
	case MDSS_MDP_WB_LAYERMIXER0:
		hw->ctl->opmode = MDSS_MDP_CTL_OP_ROT0_MODE;
		break;
	case MDSS_MDP_WB_LAYERMIXER1:
		hw->ctl->opmode =  MDSS_MDP_CTL_OP_ROT1_MODE;
		break;
	default:
		pr_err("invalid layer mixer=%d\n", hw->mixer->num);
		ret = -EINVAL;
		goto error;
	}

	hw->ctl->ops.start_fnc = mdss_mdp_writeback_start;
	hw->ctl->power_state = MDSS_PANEL_POWER_ON;
	hw->ctl->wb_type = MDSS_MDP_WB_CTL_TYPE_BLOCK;


	if (hw->ctl->ops.start_fnc)
		ret = hw->ctl->ops.start_fnc(hw->ctl);

	if (ret)
		goto error;

	if (pipe_id >= mdata->ndma_pipes)
		goto error;

	pipe_ndx = mdata->dma_pipes[pipe_id].ndx;
	hw->pipe = mdss_mdp_pipe_assign(mdata, hw->mixer,
			pipe_ndx, MDSS_MDP_PIPE_RECT0);
	if (IS_ERR_OR_NULL(hw->pipe)) {
		pr_err("dma pipe allocation failed\n");
		ret = -ENODEV;
		goto error;
	}

	hw->pipe->mixer_left = hw->mixer;
	hw->pipe_id = hw->wb->num;
	hw->wb_id = hw->wb->num;

	return hw;
error:
	if (!IS_ERR_OR_NULL(hw->pipe))
		mdss_mdp_pipe_destroy(hw->pipe);
	if (!IS_ERR_OR_NULL(hw->ctl)) {
		if (hw->ctl->ops.stop_fnc)
			hw->ctl->ops.stop_fnc(hw->ctl, MDSS_PANEL_POWER_OFF);
		mdss_mdp_ctl_free(hw->ctl);
	}
	devm_kfree(&mgr->pdev->dev, hw);

	return ERR_PTR(ret);
}

static void mdss_rotator_free_hw(struct mdss_rot_mgr *mgr,
	struct mdss_rot_hw_resource *hw)
{
	struct mdss_mdp_mixer *mixer;
	struct mdss_mdp_ctl *ctl;

	mixer = hw->pipe->mixer_left;

	mdss_mdp_pipe_destroy(hw->pipe);

	ctl = mdss_mdp_ctl_mixer_switch(mixer->ctl,
		MDSS_MDP_WB_CTL_TYPE_BLOCK);
	if (ctl) {
		if (ctl->ops.stop_fnc)
			ctl->ops.stop_fnc(ctl, MDSS_PANEL_POWER_OFF);
		mdss_mdp_ctl_free(ctl);
	}

	devm_kfree(&mgr->pdev->dev, hw);
}

struct mdss_rot_hw_resource *mdss_rotator_get_hw_resource(
	struct mdss_rot_queue *queue, struct mdss_rot_entry *entry)
{
	struct mdss_rot_hw_resource *hw = queue->hw;

	if (!hw) {
		pr_err("no hw in the queue\n");
		return NULL;
	}

	mutex_lock(&queue->hw_lock);

	if (hw->workload) {
		hw = ERR_PTR(-EBUSY);
		goto get_hw_resource_err;
	}
	hw->workload = entry;

get_hw_resource_err:
	mutex_unlock(&queue->hw_lock);
	return hw;
}

static void mdss_rotator_put_hw_resource(struct mdss_rot_queue *queue,
	struct mdss_rot_hw_resource *hw)
{
	mutex_lock(&queue->hw_lock);
	hw->workload = NULL;
	mutex_unlock(&queue->hw_lock);
}

/*
 * caller will need to call mdss_rotator_deinit_queue when
 * the function returns error
 */
static int mdss_rotator_init_queue(struct mdss_rot_mgr *mgr)
{
	int i, size, ret = 0;
	char name[32];

	size = sizeof(struct mdss_rot_queue) * mgr->queue_count;
	mgr->queues = devm_kzalloc(&mgr->pdev->dev, size, GFP_KERNEL);
	if (!mgr->queues)
		return -ENOMEM;

	for (i = 0; i < mgr->queue_count; i++) {
		snprintf(name, sizeof(name), "rot_workq_%d", i);
		pr_debug("work queue name=%s\n", name);
		mgr->queues[i].rot_work_queue = alloc_ordered_workqueue("%s",
				WQ_UNBOUND | WQ_MEM_RECLAIM | WQ_HIGHPRI, name);
		if (!mgr->queues[i].rot_work_queue) {
			ret = -EPERM;
			break;
		}

		snprintf(name, sizeof(name), "rot_timeline_%d", i);
		pr_debug("timeline name=%s\n", name);
		mgr->queues[i].timeline.timeline =
			sw_sync_timeline_create(name);
		if (!mgr->queues[i].timeline.timeline) {
			ret = -EPERM;
			break;
		}

		size = sizeof(mgr->queues[i].timeline.fence_name);
		snprintf(mgr->queues[i].timeline.fence_name, size,
				"rot_fence_%d", i);
		mutex_init(&mgr->queues[i].timeline.lock);

		mutex_init(&mgr->queues[i].hw_lock);
	}

	return ret;
}

static void mdss_rotator_deinit_queue(struct mdss_rot_mgr *mgr)
{
	int i;

	if (!mgr->queues)
		return;

	for (i = 0; i < mgr->queue_count; i++) {
		if (mgr->queues[i].rot_work_queue)
			destroy_workqueue(mgr->queues[i].rot_work_queue);

		if (mgr->queues[i].timeline.timeline) {
			struct sync_timeline *obj;
			obj = (struct sync_timeline *)
				mgr->queues[i].timeline.timeline;
			sync_timeline_destroy(obj);
		}
	}
	devm_kfree(&mgr->pdev->dev, mgr->queues);
	mgr->queue_count = 0;
}

/*
 * mdss_rotator_assign_queue() - Function assign rotation work onto hw
 * @mgr:	Rotator manager.
 * @entry:	Contains details on rotator work item being requested
 * @private:	Private struct used for access rot session performance struct
 *
 * This Function allocates hw required to complete rotation work item
 * requested.
 *
 * Caller is responsible for calling cleanup function if error is returned
 */
static int mdss_rotator_assign_queue(struct mdss_rot_mgr *mgr,
	struct mdss_rot_entry *entry,
	struct mdss_rot_file_private *private)
{
	struct mdss_rot_perf *perf;
	struct mdss_rot_queue *queue;
	struct mdss_rot_hw_resource *hw;
	struct mdp_rotation_item *item = &entry->item;
	u32 wb_idx = item->wb_idx;
	u32 pipe_idx = item->pipe_idx;
	int ret = 0;

	/*
	 * todo: instead of always assign writeback block 0, we can
	 * apply some load balancing logic in the future
	 */
	if (wb_idx == MDSS_ROTATION_HW_ANY) {
		wb_idx = 0;
		pipe_idx = 0;
	}

	if (wb_idx >= mgr->queue_count) {
		pr_err("Invalid wb idx = %d\n", wb_idx);
		return -EINVAL;
	}

	queue = mgr->queues + wb_idx;

	mutex_lock(&queue->hw_lock);

	if (!queue->hw) {
		hw = mdss_rotator_hw_alloc(mgr, pipe_idx, wb_idx);
		if (IS_ERR_OR_NULL(hw)) {
			pr_err("fail to allocate hw\n");
			ret = PTR_ERR(hw);
		} else {
			queue->hw = hw;
		}
	}

	if (queue->hw) {
		entry->queue = queue;
		queue->hw->pending_count++;
	}

	mutex_unlock(&queue->hw_lock);

	perf = mdss_rotator_find_session(private, item->session_id);
	if (!perf) {
		pr_err("Could not find session based on rotation work item\n");
		return -EINVAL;
	}

	entry->perf = perf;
	perf->last_wb_idx = wb_idx;

	return ret;
}

static void mdss_rotator_unassign_queue(struct mdss_rot_mgr *mgr,
	struct mdss_rot_entry *entry)
{
	struct mdss_rot_queue *queue = entry->queue;

	if (!queue)
		return;

	entry->queue = NULL;

	mutex_lock(&queue->hw_lock);

	if (!queue->hw) {
		pr_err("entry assigned a queue with no hw\n");
		mutex_unlock(&queue->hw_lock);
		return;
	}

	queue->hw->pending_count--;
	if (queue->hw->pending_count == 0) {
		mdss_rotator_free_hw(mgr, queue->hw);
		queue->hw = NULL;
	}

	mutex_unlock(&queue->hw_lock);
}

static void mdss_rotator_queue_request(struct mdss_rot_mgr *mgr,
	struct mdss_rot_file_private *private,
	struct mdss_rot_entry_container *req)
{
	struct mdss_rot_entry *entry;
	struct mdss_rot_queue *queue;
	unsigned long clk_rate;
	u32 wb_idx;
	int i;

	for (i = 0; i < req->count; i++) {
		entry = req->entries + i;
		queue = entry->queue;
		wb_idx = queue->hw->wb_id;
		mutex_lock(&entry->perf->work_dis_lock);
		entry->perf->work_distribution[wb_idx]++;
		mutex_unlock(&entry->perf->work_dis_lock);
		entry->work_assigned = true;
	}

	clk_rate = mdss_rotator_clk_rate_calc(mgr, private);
	mdss_rotator_set_clk_rate(mgr, clk_rate, MDSS_CLK_ROTATOR_CORE);

	for (i = 0; i < req->count; i++) {
		entry = req->entries + i;
		queue = entry->queue;
		entry->output_fence = NULL;
		queue_work(queue->rot_work_queue, &entry->commit_work);
	}
}

static int mdss_rotator_calc_perf(struct mdss_rot_perf *perf)
{
	struct mdp_rotation_config *config = &perf->config;
	u32 read_bw, write_bw;
	struct mdss_mdp_format_params *in_fmt, *out_fmt;

	in_fmt = mdss_mdp_get_format_params(config->input.format);
	if (!in_fmt) {
		pr_err("invalid input format\n");
		return -EINVAL;
	}
	out_fmt = mdss_mdp_get_format_params(config->output.format);
	if (!out_fmt) {
		pr_err("invalid output format\n");
		return -EINVAL;
	}
	if (!config->input.width ||
		(0xffffffff/config->input.width < config->input.height))
		return -EINVAL;

	perf->clk_rate = config->input.width * config->input.height;

	if (!perf->clk_rate ||
		(0xffffffff/perf->clk_rate < config->frame_rate))
		return -EINVAL;

	perf->clk_rate *= config->frame_rate;
	/* rotator processes 4 pixels per clock */
	perf->clk_rate /= 4;

	read_bw = config->input.width * config->input.height *
		config->frame_rate;
	if (in_fmt->chroma_sample == MDSS_MDP_CHROMA_420)
		read_bw = (read_bw * 3) / 2;
	else
		read_bw *= in_fmt->bpp;

	write_bw = config->output.width * config->output.height *
		config->frame_rate;
	if (out_fmt->chroma_sample == MDSS_MDP_CHROMA_420)
		write_bw = (write_bw * 3) / 2;
	else
		write_bw *= out_fmt->bpp;

	read_bw = apply_comp_ratio_factor(read_bw, in_fmt,
			&config->input.comp_ratio);
	write_bw = apply_comp_ratio_factor(write_bw, out_fmt,
			&config->output.comp_ratio);

	perf->bw = read_bw + write_bw;
	return 0;
}

static int mdss_rotator_update_perf(struct mdss_rot_mgr *mgr)
{
	struct mdss_rot_file_private *priv;
	struct mdss_rot_perf *perf;
	int not_in_suspend_mode;
	u64 total_bw = 0;

	ATRACE_BEGIN(__func__);

	not_in_suspend_mode = !atomic_read(&mgr->device_suspended);

	if (not_in_suspend_mode) {
		mutex_lock(&mgr->file_lock);
		list_for_each_entry(priv, &mgr->file_list, list) {
			mutex_lock(&priv->perf_lock);
			list_for_each_entry(perf, &priv->perf_list, list) {
				total_bw += perf->bw;
			}
			mutex_unlock(&priv->perf_lock);
		}
		mutex_unlock(&mgr->file_lock);
	}

	mutex_lock(&mgr->bus_lock);
	total_bw += mgr->pending_close_bw_vote;
	mdss_rotator_enable_reg_bus(mgr, total_bw);
	mdss_rotator_bus_scale_set_quota(&mgr->data_bus, total_bw);
	mutex_unlock(&mgr->bus_lock);

	ATRACE_END(__func__);
	return 0;
}

static void mdss_rotator_release_from_work_distribution(
		struct mdss_rot_mgr *mgr,
		struct mdss_rot_entry *entry)
{
	if (entry->work_assigned) {
		bool free_perf = false;
		u32 wb_idx = entry->queue->hw->wb_id;

		mutex_lock(&mgr->lock);
		mutex_lock(&entry->perf->work_dis_lock);
		if (entry->perf->work_distribution[wb_idx])
			entry->perf->work_distribution[wb_idx]--;

		if (!entry->perf->work_distribution[wb_idx]
				&& list_empty(&entry->perf->list)) {
			/* close session has offloaded perf free to us */
			free_perf = true;
		}
		mutex_unlock(&entry->perf->work_dis_lock);
		entry->work_assigned = false;
		if (free_perf) {
			mutex_lock(&mgr->bus_lock);
			mgr->pending_close_bw_vote -= entry->perf->bw;
			mutex_unlock(&mgr->bus_lock);
			mdss_rotator_resource_ctrl(mgr, false);
			devm_kfree(&mgr->pdev->dev,
				entry->perf->work_distribution);
			devm_kfree(&mgr->pdev->dev, entry->perf);
			mdss_rotator_update_perf(mgr);
			mdss_rotator_clk_ctrl(mgr, false);
			entry->perf = NULL;
		}
		mutex_unlock(&mgr->lock);
	}
}

static void mdss_rotator_release_entry(struct mdss_rot_mgr *mgr,
	struct mdss_rot_entry *entry)
{
	mdss_rotator_release_from_work_distribution(mgr, entry);
	mdss_rotator_clear_fence(entry);
	mdss_rotator_release_data(entry);
	mdss_rotator_unassign_queue(mgr, entry);
}

static int mdss_rotator_config_dnsc_factor(struct mdss_rot_mgr *mgr,
	struct mdss_rot_entry *entry)
{
	int ret = 0;
	u16 src_w, src_h, dst_w, dst_h, bit;
	struct mdp_rotation_item *item = &entry->item;
	struct mdss_mdp_format_params *fmt;

	src_w = item->src_rect.w;
	src_h = item->src_rect.h;

	if (item->flags & MDP_ROTATION_90) {
		dst_w = item->dst_rect.h;
		dst_h = item->dst_rect.w;
	} else {
		dst_w = item->dst_rect.w;
		dst_h = item->dst_rect.h;
	}

	if (!mgr->has_downscale &&
		(src_w != dst_w || src_h != dst_h)) {
		pr_err("rotator downscale not supported\n");
		ret = -EINVAL;
		goto dnsc_err;
	}

	entry->dnsc_factor_w = 0;
	entry->dnsc_factor_h = 0;

	if ((src_w != dst_w) || (src_h != dst_h)) {
		if ((src_w % dst_w) || (src_h % dst_h)) {
			ret = -EINVAL;
			goto dnsc_err;
		}
		entry->dnsc_factor_w = src_w / dst_w;
		bit = fls(entry->dnsc_factor_w);
		/*
		 * New Chipsets supports downscale upto 1/64
		 * change the Bit check from 5 to 7 to support 1/64 down scale
		 */
		if ((entry->dnsc_factor_w & ~BIT(bit - 1)) || (bit > 7)) {
			ret = -EINVAL;
			goto dnsc_err;
		}
		entry->dnsc_factor_h = src_h / dst_h;
		bit = fls(entry->dnsc_factor_h);
		if ((entry->dnsc_factor_h & ~BIT(bit - 1)) || (bit > 7)) {
			ret = -EINVAL;
			goto dnsc_err;
		}
	}

	fmt =  mdss_mdp_get_format_params(item->output.format);
	if (mdss_mdp_is_ubwc_format(fmt) &&
		(entry->dnsc_factor_h || entry->dnsc_factor_w)) {
		pr_err("ubwc not supported with downscale %d\n",
			item->output.format);
		ret = -EINVAL;
	}

dnsc_err:

	/* Downscaler does not support asymmetrical dnsc */
	if (entry->dnsc_factor_w != entry->dnsc_factor_h)
		ret = -EINVAL;

	if (ret) {
		pr_err("Invalid rotator downscale ratio %dx%d->%dx%d\n",
			src_w, src_h, dst_w, dst_h);
		entry->dnsc_factor_w = 0;
		entry->dnsc_factor_h = 0;
	}
	return ret;
}

static bool mdss_rotator_verify_format(struct mdss_rot_mgr *mgr,
	struct mdss_mdp_format_params *in_fmt,
	struct mdss_mdp_format_params *out_fmt, bool rotation)
{
	u8 in_v_subsample, in_h_subsample;
	u8 out_v_subsample, out_h_subsample;

	if (!mgr->has_ubwc && (mdss_mdp_is_ubwc_format(in_fmt) ||
			mdss_mdp_is_ubwc_format(out_fmt))) {
		pr_err("Rotator doesn't allow ubwc\n");
		return -EINVAL;
	}

	if (!(out_fmt->flag & VALID_ROT_WB_FORMAT)) {
		pr_err("Invalid output format\n");
		return false;
	}

	if (in_fmt->is_yuv != out_fmt->is_yuv) {
		pr_err("Rotator does not support CSC\n");
		return false;
	}

	/* Forcing same pixel depth */
	if (memcmp(in_fmt->bits, out_fmt->bits, sizeof(in_fmt->bits))) {
		/* Exception is that RGB can drop alpha or add X */
		if (in_fmt->is_yuv || out_fmt->alpha_enable ||
			(in_fmt->bits[C2_R_Cr] != out_fmt->bits[C2_R_Cr]) ||
			(in_fmt->bits[C0_G_Y] != out_fmt->bits[C0_G_Y]) ||
			(in_fmt->bits[C1_B_Cb] != out_fmt->bits[C1_B_Cb])) {
			pr_err("Bit format does not match\n");
			return false;
		}
	}

	/* Need to make sure that sub-sampling persists through rotation */
	if (rotation) {
		mdss_mdp_get_v_h_subsample_rate(in_fmt->chroma_sample,
			&in_v_subsample, &in_h_subsample);
		mdss_mdp_get_v_h_subsample_rate(out_fmt->chroma_sample,
			&out_v_subsample, &out_h_subsample);

		if ((in_v_subsample != out_h_subsample) ||
				(in_h_subsample != out_v_subsample)) {
			pr_err("Rotation has invalid subsampling\n");
			return false;
		}
	} else {
		if (in_fmt->chroma_sample != out_fmt->chroma_sample) {
			pr_err("Format subsampling mismatch\n");
			return false;
		}
	}

	pr_debug("in_fmt=%0d, out_fmt=%d, has_ubwc=%d\n",
		in_fmt->format, out_fmt->format, mgr->has_ubwc);
	return true;
}

static int mdss_rotator_verify_config(struct mdss_rot_mgr *mgr,
	struct mdp_rotation_config *config)
{
	struct mdss_mdp_format_params *in_fmt, *out_fmt;
	u8 in_v_subsample, in_h_subsample;
	u8 out_v_subsample, out_h_subsample;
	u32 input, output;
	bool rotation;

	input = config->input.format;
	output = config->output.format;
	rotation = (config->flags & MDP_ROTATION_90) ? true : false;

	in_fmt = mdss_mdp_get_format_params(input);
	if (!in_fmt) {
		pr_err("Unrecognized input format:%u\n", input);
		return -EINVAL;
	}

	out_fmt = mdss_mdp_get_format_params(output);
	if (!out_fmt) {
		pr_err("Unrecognized output format:%u\n", output);
		return -EINVAL;
	}

	mdss_mdp_get_v_h_subsample_rate(in_fmt->chroma_sample,
		&in_v_subsample, &in_h_subsample);
	mdss_mdp_get_v_h_subsample_rate(out_fmt->chroma_sample,
		&out_v_subsample, &out_h_subsample);

	/* Dimension of image needs to be divisible by subsample rate  */
	if ((config->input.height % in_v_subsample) ||
			(config->input.width % in_h_subsample)) {
		pr_err("In ROI, subsample mismatch, w=%d, h=%d, vss%d, hss%d\n",
			config->input.width, config->input.height,
			in_v_subsample, in_h_subsample);
		return -EINVAL;
	}

	if ((config->output.height % out_v_subsample) ||
			(config->output.width % out_h_subsample)) {
		pr_err("Out ROI, subsample mismatch, w=%d, h=%d, vss%d, hss%d\n",
			config->output.width, config->output.height,
			out_v_subsample, out_h_subsample);
		return -EINVAL;
	}

	if (!mdss_rotator_verify_format(mgr, in_fmt,
			out_fmt, rotation)) {
		pr_err("Rot format pairing invalid, in_fmt:%d, out_fmt:%d\n",
			input, output);
		return -EINVAL;
	}

	return 0;
}

static int mdss_rotator_validate_item_matches_session(
	struct mdp_rotation_config *config, struct mdp_rotation_item *item)
{
	int ret;

	ret = __compare_session_item_rect(&config->input,
		&item->src_rect, item->input.format, true);
	if (ret)
		return ret;

	ret = __compare_session_item_rect(&config->output,
		&item->dst_rect, item->output.format, false);
	if (ret)
		return ret;

	ret = __compare_session_rotations(config->flags, item->flags);
	if (ret)
		return ret;

	return 0;
}

static int mdss_rotator_validate_img_roi(struct mdp_rotation_item *item)
{
	struct mdss_mdp_format_params *fmt;
	uint32_t width, height;
	int ret = 0;

	width = item->input.width;
	height = item->input.height;
	if (item->flags & MDP_ROTATION_DEINTERLACE) {
		width *= 2;
		height /= 2;
	}

	/* Check roi bounds */
	if (ROT_CHECK_BOUNDS(item->src_rect.x, item->src_rect.w, width) ||
			ROT_CHECK_BOUNDS(item->src_rect.y, item->src_rect.h,
			height)) {
		pr_err("invalid src flag=%08x img wh=%dx%d rect=%d,%d,%d,%d\n",
			item->flags, width, height, item->src_rect.x,
			item->src_rect.y, item->src_rect.w, item->src_rect.h);
		return -EINVAL;
	}
	if (ROT_CHECK_BOUNDS(item->dst_rect.x, item->dst_rect.w,
			item->output.width) ||
			ROT_CHECK_BOUNDS(item->dst_rect.y, item->dst_rect.h,
			item->output.height)) {
		pr_err("invalid dst img wh=%dx%d rect=%d,%d,%d,%d\n",
			item->output.width, item->output.height,
			item->dst_rect.x, item->dst_rect.y, item->dst_rect.w,
			item->dst_rect.h);
		return -EINVAL;
	}

	fmt = mdss_mdp_get_format_params(item->output.format);
	if (!fmt) {
		pr_err("invalid output format:%d\n", item->output.format);
		return -EINVAL;
	}

	if (mdss_mdp_is_ubwc_format(fmt))
		ret = mdss_mdp_validate_offset_for_ubwc_format(fmt,
			item->dst_rect.x, item->dst_rect.y);

	return ret;
}

static int mdss_rotator_validate_fmt_and_item_flags(
	struct mdp_rotation_config *config, struct mdp_rotation_item *item)
{
	struct mdss_mdp_format_params *fmt;

	fmt = mdss_mdp_get_format_params(item->input.format);
	if ((item->flags & MDP_ROTATION_DEINTERLACE) &&
			mdss_mdp_is_ubwc_format(fmt)) {
		pr_err("cannot perform mdp deinterlace on tiled formats\n");
		return -EINVAL;
	}
	return 0;
}

static int mdss_rotator_validate_entry(struct mdss_rot_mgr *mgr,
	struct mdss_rot_file_private *private,
	struct mdss_rot_entry *entry)
{
	int ret;
	struct mdp_rotation_item *item;
	struct mdss_rot_perf *perf;

	item = &entry->item;

	if (item->wb_idx != item->pipe_idx) {
		pr_err("invalid writeback and pipe idx\n");
		return -EINVAL;
	}

	if (item->wb_idx != MDSS_ROTATION_HW_ANY &&
		item->wb_idx > mgr->queue_count) {
		pr_err("invalid writeback idx\n");
		return -EINVAL;
	}

	perf = mdss_rotator_find_session(private, item->session_id);
	if (!perf) {
		pr_err("Could not find session:%u\n", item->session_id);
		return -EINVAL;
	}

	ret = mdss_rotator_validate_item_matches_session(&perf->config, item);
	if (ret) {
		pr_err("Work item does not match session:%u\n",
			item->session_id);
		return ret;
	}

	ret = mdss_rotator_validate_img_roi(item);
	if (ret) {
		pr_err("Image roi is invalid\n");
		return ret;
	}

	ret = mdss_rotator_validate_fmt_and_item_flags(&perf->config, item);
	if (ret)
		return ret;

	ret = mdss_rotator_config_dnsc_factor(mgr, entry);
	if (ret) {
		pr_err("fail to configure downscale factor\n");
		return ret;
	}
	return ret;
}

/*
 * Upon failure from the function, caller needs to make sure
 * to call mdss_rotator_remove_request to clean up resources.
 */
static int mdss_rotator_add_request(struct mdss_rot_mgr *mgr,
	struct mdss_rot_file_private *private,
	struct mdss_rot_entry_container *req)
{
	struct mdss_rot_entry *entry;
	struct mdp_rotation_item *item;
	u32 flag = 0;
	int i, ret;

	for (i = 0; i < req->count; i++) {
		entry = req->entries + i;
		item = &entry->item;

		if (item->flags & MDP_ROTATION_SECURE)
			flag = MDP_SECURE_OVERLAY_SESSION;

		ret = mdss_rotator_validate_entry(mgr, private, entry);
		if (ret) {
			pr_err("fail to validate the entry\n");
			return ret;
		}

		ret = mdss_rotator_import_data(mgr, entry);
		if (ret) {
			pr_err("fail to import the data\n");
			return ret;
		}

		if (item->input.fence >= 0) {
			entry->input_fence =
				sync_fence_fdget(item->input.fence);
			if (!entry->input_fence) {
				pr_err("invalid input fence fd\n");
				return -EINVAL;
			}
		}

		ret = mdss_rotator_assign_queue(mgr, entry, private);
		if (ret) {
			pr_err("fail to assign queue to entry\n");
			return ret;
		}

		entry->request = req;

		INIT_WORK(&entry->commit_work, mdss_rotator_wq_handler);

		ret = mdss_rotator_create_fence(entry);
		if (ret) {
			pr_err("fail to create fence\n");
			return ret;
		}
		item->output.fence = entry->output_fence_fd;

		pr_debug("Entry added. wbidx=%u, src{%u,%u,%u,%u}f=%u\n"
			"dst{%u,%u,%u,%u}f=%u session_id=%u\n", item->wb_idx,
			item->src_rect.x, item->src_rect.y,
			item->src_rect.w, item->src_rect.h, item->input.format,
			item->dst_rect.x, item->dst_rect.y,
			item->dst_rect.w, item->dst_rect.h, item->output.format,
			item->session_id);
	}

	mutex_lock(&private->req_lock);
	list_add(&req->list, &private->req_list);
	mutex_unlock(&private->req_lock);

	return 0;
}

static void mdss_rotator_remove_request(struct mdss_rot_mgr *mgr,
	struct mdss_rot_file_private *private,
	struct mdss_rot_entry_container *req)
{
	int i;

	mutex_lock(&private->req_lock);
	for (i = 0; i < req->count; i++)
		mdss_rotator_release_entry(mgr, req->entries + i);
	list_del_init(&req->list);
	mutex_unlock(&private->req_lock);
}

/* This function should be called with req_lock */
static void mdss_rotator_cancel_request(struct mdss_rot_mgr *mgr,
	struct mdss_rot_entry_container *req)
{
	struct mdss_rot_entry *entry;
	int i;

	/*
	 * To avoid signal the rotation entry output fence in the wrong
	 * order, all the entries in the same request needs to be cancelled
	 * first, before signaling the output fence.
	 */
	for (i = req->count - 1; i >= 0; i--) {
		entry = req->entries + i;
		cancel_work_sync(&entry->commit_work);
	}

	for (i = req->count - 1; i >= 0; i--) {
		entry = req->entries + i;
		mdss_rotator_signal_output(entry);
		mdss_rotator_release_entry(mgr, entry);
	}

	list_del_init(&req->list);
	devm_kfree(&mgr->pdev->dev, req);
}

static void mdss_rotator_cancel_all_requests(struct mdss_rot_mgr *mgr,
	struct mdss_rot_file_private *private)
{
	struct mdss_rot_entry_container *req, *req_next;
	pr_debug("Canceling all rotator requests\n");

	mutex_lock(&private->req_lock);
	list_for_each_entry_safe(req, req_next, &private->req_list, list)
		mdss_rotator_cancel_request(mgr, req);
	mutex_unlock(&private->req_lock);
}

static void mdss_rotator_free_competed_request(struct mdss_rot_mgr *mgr,
	struct mdss_rot_file_private *private)
{
	struct mdss_rot_entry_container *req, *req_next;

	mutex_lock(&private->req_lock);
	list_for_each_entry_safe(req, req_next, &private->req_list, list) {
		if (atomic_read(&req->pending_count) == 0) {
			list_del_init(&req->list);
			devm_kfree(&mgr->pdev->dev, req);
		}
	}
	mutex_unlock(&private->req_lock);
}

static void mdss_rotator_release_rotator_perf_session(
	struct mdss_rot_mgr *mgr,
	struct mdss_rot_file_private *private)
{
	struct mdss_rot_perf *perf, *perf_next;

	pr_debug("Releasing all rotator request\n");
	mdss_rotator_cancel_all_requests(mgr, private);

	mutex_lock(&private->perf_lock);
	list_for_each_entry_safe(perf, perf_next, &private->perf_list, list) {
		list_del_init(&perf->list);
		devm_kfree(&mgr->pdev->dev, perf->work_distribution);
		devm_kfree(&mgr->pdev->dev, perf);
	}
	mutex_unlock(&private->perf_lock);
}

static void mdss_rotator_release_all(struct mdss_rot_mgr *mgr)
{
	struct mdss_rot_file_private *priv, *priv_next;

	mutex_lock(&mgr->file_lock);
	list_for_each_entry_safe(priv, priv_next, &mgr->file_list, list) {
		mdss_rotator_release_rotator_perf_session(mgr, priv);
		mdss_rotator_resource_ctrl(mgr, false);
		list_del_init(&priv->list);
		priv->file->private_data = NULL;
		devm_kfree(&mgr->pdev->dev, priv);
	}
	mutex_unlock(&rot_mgr->file_lock);

	mdss_rotator_update_perf(mgr);
}

static int mdss_rotator_prepare_hw(struct mdss_rot_hw_resource *hw,
	struct mdss_rot_entry *entry)
{
	struct mdss_mdp_pipe *pipe;
	struct mdss_mdp_ctl *orig_ctl, *rot_ctl;
	int ret;

	pipe = hw->pipe;
	orig_ctl = pipe->mixer_left->ctl;
	if (orig_ctl->shared_lock)
		mutex_lock(orig_ctl->shared_lock);

	rot_ctl = mdss_mdp_ctl_mixer_switch(orig_ctl,
						MDSS_MDP_WB_CTL_TYPE_BLOCK);
	if (!rot_ctl) {
		ret = -EINVAL;
		goto error;
	} else {
		hw->ctl = rot_ctl;
		pipe->mixer_left = rot_ctl->mixer_left;
	}

	return 0;

error:
	if (orig_ctl->shared_lock)
		mutex_unlock(orig_ctl->shared_lock);
	return ret;
}

static void mdss_rotator_translate_rect(struct mdss_rect *dst,
	struct mdp_rect *src)
{
	dst->x = src->x;
	dst->y = src->y;
	dst->w = src->w;
	dst->h = src->h;
}

static u32 mdss_rotator_translate_flags(u32 input)
{
	u32 output = 0;

	if (input & MDP_ROTATION_NOP)
		output |= MDP_ROT_NOP;
	if (input & MDP_ROTATION_FLIP_LR)
		output |= MDP_FLIP_LR;
	if (input & MDP_ROTATION_FLIP_UD)
		output |= MDP_FLIP_UD;
	if (input & MDP_ROTATION_90)
		output |= MDP_ROT_90;
	if (input & MDP_ROTATION_DEINTERLACE)
		output |= MDP_DEINTERLACE;
	if (input & MDP_ROTATION_SECURE)
		output |= MDP_SECURE_OVERLAY_SESSION;
	if (input & MDP_ROTATION_BWC_EN)
		output |= MDP_BWC_EN;

	return output;
}

static int mdss_rotator_config_hw(struct mdss_rot_hw_resource *hw,
	struct mdss_rot_entry *entry)
{
	struct mdss_mdp_pipe *pipe;
	struct mdp_rotation_item *item;
	struct mdss_rot_perf *perf;
	int ret;

	ATRACE_BEGIN(__func__);
	pipe = hw->pipe;
	item = &entry->item;
	perf = entry->perf;

	pipe->flags = mdss_rotator_translate_flags(item->flags);
	pipe->src_fmt = mdss_mdp_get_format_params(item->input.format);
	pipe->img_width = item->input.width;
	pipe->img_height = item->input.height;
	mdss_rotator_translate_rect(&pipe->src, &item->src_rect);
	mdss_rotator_translate_rect(&pipe->dst, &item->src_rect);
	pipe->scaler.enable = 0;
	pipe->frame_rate = perf->config.frame_rate;

	pipe->params_changed++;

	mdss_mdp_smp_release(pipe);

	ret = mdss_mdp_smp_reserve(pipe);
	if (ret) {
		pr_err("unable to mdss_mdp_smp_reserve rot data\n");
		goto done;
	}

	ret = mdss_mdp_overlay_setup_scaling(pipe);
	if (ret) {
		pr_err("scaling setup failed %d\n", ret);
		goto done;
	}

	ret = mdss_mdp_pipe_queue_data(pipe, &entry->src_buf);
	pr_debug("Config pipe. src{%u,%u,%u,%u}f=%u\n"
		"dst{%u,%u,%u,%u}f=%u session_id=%u\n",
		item->src_rect.x, item->src_rect.y,
		item->src_rect.w, item->src_rect.h, item->input.format,
		item->dst_rect.x, item->dst_rect.y,
		item->dst_rect.w, item->dst_rect.h, item->output.format,
		item->session_id);
	MDSS_XLOG(item->input.format, pipe->img_width, pipe->img_height,
		pipe->flags);
done:
	ATRACE_END(__func__);
	return ret;
}

static int mdss_rotator_kickoff_entry(struct mdss_rot_hw_resource *hw,
	struct mdss_rot_entry *entry)
{
	int ret;
	struct mdss_mdp_writeback_arg wb_args = {
		.data = &entry->dst_buf,
		.priv_data = entry,
	};

	ret = mdss_mdp_writeback_display_commit(hw->ctl, &wb_args);
	return ret;
}

static int mdss_rotator_wait_for_entry(struct mdss_rot_hw_resource *hw,
	struct mdss_rot_entry *entry)
{
	int ret;
	struct mdss_mdp_ctl *ctl = hw->ctl;

	ret = mdss_mdp_display_wait4comp(ctl);
	if (ctl->shared_lock)
		mutex_unlock(ctl->shared_lock);
	return ret;
}

static int mdss_rotator_commit_entry(struct mdss_rot_hw_resource *hw,
	struct mdss_rot_entry *entry)
{
	int ret;

	ret = mdss_rotator_prepare_hw(hw, entry);
	if (ret) {
		pr_err("fail to prepare hw resource %d\n", ret);
		return ret;
	}

	ret = mdss_rotator_config_hw(hw, entry);
	if (ret) {
		pr_err("fail to configure hw resource %d\n", ret);
		return ret;
	}

	ret = mdss_rotator_kickoff_entry(hw, entry);
	if (ret) {
		pr_err("fail to do kickoff %d\n", ret);
		return ret;
	}

	ret = mdss_rotator_wait_for_entry(hw, entry);
	if (ret) {
		pr_err("fail to wait for completion %d\n", ret);
		return ret;
	}

	return ret;
}

static int mdss_rotator_handle_entry(struct mdss_rot_hw_resource *hw,
	struct mdss_rot_entry *entry)
{
	int ret;

	ret = mdss_rotator_wait_for_input(entry);
	if (ret) {
		pr_err("wait for input buffer failed %d\n", ret);
		return ret;
	}

	ret = mdss_rotator_map_and_check_data(entry);
	if (ret) {
		pr_err("fail to prepare input/output data %d\n", ret);
		return ret;
	}

	ret = mdss_rotator_commit_entry(hw, entry);
	if (ret)
		pr_err("rotator commit failed %d\n", ret);

	return ret;
}

static void mdss_rotator_wq_handler(struct work_struct *work)
{
	struct mdss_rot_entry *entry;
	struct mdss_rot_entry_container *request;
	struct mdss_rot_hw_resource *hw;
	int ret;

	entry = container_of(work, struct mdss_rot_entry, commit_work);
	request = entry->request;

	if (!request) {
		pr_err("fatal error, no request with entry\n");
		return;
	}

	hw = mdss_rotator_get_hw_resource(entry->queue, entry);
	if (!hw) {
		pr_err("no hw for the queue\n");
		goto get_hw_res_err;
	}

	ret = mdss_rotator_handle_entry(hw, entry);
	if (ret) {
		struct mdp_rotation_item *item = &entry->item;

		pr_err("Rot req fail. src{%u,%u,%u,%u}f=%u\n"
		"dst{%u,%u,%u,%u}f=%u session_id=%u, wbidx%d, pipe_id=%d\n",
		item->src_rect.x, item->src_rect.y,
		item->src_rect.w, item->src_rect.h, item->input.format,
		item->dst_rect.x, item->dst_rect.y,
		item->dst_rect.w, item->dst_rect.h, item->output.format,
		item->session_id, item->wb_idx, item->pipe_idx);
	}

	mdss_rotator_put_hw_resource(entry->queue, hw);

get_hw_res_err:
	mdss_rotator_signal_output(entry);
	mdss_rotator_release_entry(rot_mgr, entry);
	atomic_dec(&request->pending_count);
}

static int mdss_rotator_validate_request(struct mdss_rot_mgr *mgr,
	struct mdss_rot_file_private *private,
	struct mdss_rot_entry_container *req)
{
	int i, ret = 0;
	struct mdss_rot_entry *entry;

	for (i = 0; i < req->count; i++) {
		entry = req->entries + i;
		ret = mdss_rotator_validate_entry(mgr, private,
			entry);
		if (ret) {
			pr_err("fail to validate the entry\n");
			return ret;
		}
	}

	return ret;
}

static u32 mdss_rotator_generator_session_id(struct mdss_rot_mgr *mgr)
{
	u32 id;
	mutex_lock(&mgr->lock);
	id = mgr->session_id_generator++;
	mutex_unlock(&mgr->lock);
	return id;
}

static int mdss_rotator_open_session(struct mdss_rot_mgr *mgr,
	struct mdss_rot_file_private *private, unsigned long arg)
{
	struct mdp_rotation_config config;
	struct mdss_rot_perf *perf;
	int ret;

	ret = copy_from_user(&config, (void __user *)arg, sizeof(config));
	if (ret) {
		pr_err("fail to copy session data\n");
		return ret;
	}

	ret = mdss_rotator_verify_config(mgr, &config);
	if (ret) {
		pr_err("Rotator verify format failed\n");
		return ret;
	}

	perf = devm_kzalloc(&mgr->pdev->dev, sizeof(*perf), GFP_KERNEL);
	if (!perf) {
		pr_err("fail to allocate session\n");
		return -ENOMEM;
	}

	ATRACE_BEGIN(__func__); /* Open session votes for bw */
	perf->work_distribution = devm_kzalloc(&mgr->pdev->dev,
		sizeof(u32) * mgr->queue_count, GFP_KERNEL);
	if (!perf->work_distribution) {
		pr_err("fail to allocate work_distribution\n");
		ret = -ENOMEM;
		goto alloc_err;
	}

	config.session_id = mdss_rotator_generator_session_id(mgr);
	perf->config = config;
	perf->last_wb_idx = -1;
	mutex_init(&perf->work_dis_lock);

	INIT_LIST_HEAD(&perf->list);

	ret = mdss_rotator_calc_perf(perf);
	if (ret) {
		pr_err("error setting the session%d\n", ret);
		goto copy_user_err;
	}

	ret = copy_to_user((void *)arg, &config, sizeof(config));
	if (ret) {
		pr_err("fail to copy to user\n");
		goto copy_user_err;
	}

	mutex_lock(&private->perf_lock);
	list_add(&perf->list, &private->perf_list);
	mutex_unlock(&private->perf_lock);

	ret = mdss_rotator_resource_ctrl(mgr, true);
	if (ret) {
		pr_err("Failed to aqcuire rotator resources\n");
		goto resource_err;
	}

	mdss_rotator_clk_ctrl(rot_mgr, true);
	ret = mdss_rotator_update_perf(mgr);
	if (ret) {
		pr_err("fail to open session, not enough clk/bw\n");
		goto perf_err;
	}
	pr_debug("open session id=%u in{%u,%u}f:%u out{%u,%u}f:%u\n",
		config.session_id, config.input.width, config.input.height,
		config.input.format, config.output.width, config.output.height,
		config.output.format);

	goto done;
perf_err:
	mdss_rotator_clk_ctrl(rot_mgr, false);
	mdss_rotator_resource_ctrl(mgr, false);
resource_err:
	mutex_lock(&private->perf_lock);
	list_del_init(&perf->list);
	mutex_unlock(&private->perf_lock);
copy_user_err:
	devm_kfree(&mgr->pdev->dev, perf->work_distribution);
alloc_err:
	devm_kfree(&mgr->pdev->dev, perf);
done:
	ATRACE_END(__func__);
	return ret;
}

static int mdss_rotator_close_session(struct mdss_rot_mgr *mgr,
	struct mdss_rot_file_private *private, unsigned long arg)
{
	struct mdss_rot_perf *perf;
	bool offload_release_work = false;
	u32 id;

	id = (u32)arg;
	mutex_lock(&mgr->lock);
	mutex_lock(&private->perf_lock);
	perf = __mdss_rotator_find_session(private, id);
	if (!perf) {
		mutex_unlock(&private->perf_lock);
		mutex_unlock(&mgr->lock);
		pr_err("Trying to close session that does not exist\n");
		return -EINVAL;
	}

	ATRACE_BEGIN(__func__);
	mutex_lock(&perf->work_dis_lock);
	if (mdss_rotator_is_work_pending(mgr, perf)) {
		pr_debug("Work is still input.hotator_calc_perf(perf);
	ir_close_se subsamqeue->hw_lock);	mdss_rotator_resource_cte -= entry->perf->bw;
			mex_unlock(&privock);
			mdss_rotator_resource_calse;
	u32 id;

	id = (ss_rotatost);
	mutex_unlock(&private->perf_lock);
copy (mdss_rotator_is_workerf_lock);

	ret = mdss_rotator_resouk)
		lse;
	u32 id;

	id ) mdss_mdp_pipectrl(mgr, false);
resource_err:
	mutex_locdev, perf->work_distribution);
		devm_kfree(&mgr->pdev->ev, perf);
	}
	mutex_unlock(&private(mgr);
	if (ret) {
		pr_err("fait_mgr, false);
	mdss_rotator_resource_c__);
	rrc{%u,%u,%uerf-df:%u out{%u_id"utex_unleturn ret;
}

static in;
	return id;
}

static int mdss_rtator_validate_entry(struct mdf (ret)mgr *mgr,
	struct mdss_rot_file_private *private, unsigned long arg)
{
	struct mdss_rot_perf *pe src_h, dst_w,rf;
	struct mdss_rot_queue *queue;
	st;
	struct mdss_rot_perf *pconfig, (void __user *)arg, sizeof(config));
	if (rYPE_Bt) {
		pr_err("fail to copy session data\n");
		return ret;
	}

	ret = mdss_rotator_verify_config(mgr, &config);
	if (ret) {
		pr_err("Rotator verify format failed\n");
		return ret;
	}

	perf = devm_kzalloc(&mgr	mutex_lock(&private->per_session(private, item->session_id);
	if (g.input.width, cor("fail to allocate sessionNourn ret;
;
	}
%u,%u}clock */
fBOUNidth, config->t.width, cor("f);
		pr_err("Trying to close ACE_BEGIN(__func__);
	mutex_lock(&perf->work_dis_lock);
	iff = __mdss_rotator_find_s->last_wb_idx = -1;
perf(perf);
	if (ret) {
		pr_err("errerf_lock);

	ret = mdss_rotator_resouk)
	etting the session%d\n"ind\n", rett);
		goto copye;
	}

	ret = mdss_mdp_pipe_queue_data(pipe;
	if (ret) {
		pr_err("fa	rrc{%u,%u,%:%u\st_wb:%u out{%u,%u}f:%u\n",
		config.session_id, config.input.width, config.input.height,
		config.input.format, config.output.width, config.output.height,
		config.output.format);

	goto done;
perf_err:
	__);
	return ret;
}

static in;
	return id;
}

static int mdss_rrotator_clainer *req)
{
	int i, ret = 0;
rl(mgr, false);qex_unlgr,
	struct mdss_rot_file_pm *item)
{
	struct mdss_mdp_fors,g;
	mu(!perf-t;

	forsry_container *req, *req_next;

	mutex_loc_from_u!mgr->ial the rota(ROT_C", retrn -
		retuplanss_rotatr neeiv_n givu}f_forancingrx_unlos to beMAX_PLANES limitnt - 1; i >= 0; 0 
		ent>queues[i].rot_work_e->im			syput.wiplanss_rotat>eMAX_PLANES != out_v_s>im			syone;
peplanss_rotat>eMAX_PLANES alidate the entIret);O		retuplanss_rotatexc firsMAX_PLANES limitAL;
	}
nput, ou	}
npuidth, co->wb_i			syput.wiplanss_rotah, co->wb_i			syone;
peplanss_rota

	return ret_errPTR(IN(__fu)

static u3s[i].timeline.ntainer *req, *req_next;

	mute)

ss[i].+timeline.ntainer *req, *req_ne) *
		ueuest;

ev->dev, sizeof(*perf), GFP_KERNEL);
f->work_distribrr("fatal locate work_distribution\n");
	struct mtor_cancel_all_turn ret_errPTR(IN
	}

_hw_resost);

	ret = mdse(&mgr->pdev-i--) {
		en=
		ueuest;

dss_rotato*)
				mgr	for (i = 0; i <) mdizeof(con;

e+imeline.ntainer *req, *req_next;

	mute)dev-i--) nslate_fnslat;ending_coset 0) {
			list_del_in, _rota

	ount; i++) {
		entor_release_ent;

dss_rotat		sypr_err(wb_i			sc void mdss_rqator_handle_entry(struct mdss_rot_hwor_canc;
	ifmgr,
	struct mdss_rot_file_private *private, unsigned long arg)
{
	scontainer *req, *req_next;

	mutex_lockem *item;
	struct mdss_rot_perf sruct mdss_rot_epectrl(mgr, falseruct mdss_rot_mgr *mgrock(&private->pern_queue(mgr, entry, dt_mgr *mgrock(&privatelock(&pril to copy session data\n");
	s_yustruct mtor_cancel_all_trl(mgr, false);_rot_mgr *mgrock(&privatelock(&pri devm_kzalloc(&mgrnt; i++)
		mdss_rotator_release_entwb_i			syone;
pedget(item->;

dss_rotat		sypr_eyone;
pedget(c int mdss_rotator_validate_fmt_and_item_fl_rot_hwor_cancr,
	struct mdss_rot_file_private *private, unsigned long arg)
{
	struct mdp_rotation_config config;
	struct m		returngr->ploc_fr *item;
	struct mdss_rot_perf sgr->pdev->ntainer *req, *req_next;

	mutex_locgr->pdev->om_u!mgr->alloc(	int ret );qe		ueuestntainer *reqtex_utytatim= mdss_ms(item->inpum= md(ribrr("fam= mde;
}ndf (r		list_rify format failed\n")		return;
	}

. H}ndf ((perf);
	}

	ATRACE_BEGINPERunc__); _pending(inpusd);
ieq__cnt(ownscale not suppocelled
	 *ce on mit,
		dett);
sec;
		rif (ctENOMEM;
	}

	ATRACE_BEGINPERunc__); nfig, (void __user *)agr->ploczeof(config));
	if (rYPE_Be_count, r->ploc("fail to copy session data\n");
		retustruct mtor_cancel_all_turn retator_verify_qtator_frngr->ploc.		ueuestork_e!y_qtator_ = -EIy_qtator_f>eMAX_LAYE < rUNoutput format\n"i ratio %dx%d->%locgator_f:;
	}

	reqs_rota

	reACE_BEGIN(__func__);
he rotaherr->wess_rota		retuofto beerf sgso;
		rewefore		retancingme requ(i = req->corst, requ;
ieq_"ind_);
stat.   Oreqrwis	scocinwx_unme havemove_requeulti, vs, &config, snt - 1;s[i].timeline.ntainer *struct mdss_rot) *
);qe		ueuesterf sgr->dev, sizeof(*perf), GFP_KERNEL);
f->work_distrib("Unrecrf srocate work_distribution\n");
	struct mtcrf s	}

	ATRACE_BEGIN(__func__) nfig, (void __user *)_fors,ngr->ploc.t);
	mL);
"fail to copy session data\n");
		retustruct mtcrf s	}

	ATRss_md_rot_hwor_canc;user((void *q_ctrl(mgr, true);
qex_unl rotatfors,ngr->ploc.(!perf-tr->ploc.rn ret;

	retIS__errOR_>pde(loc("ocate work_distribution\n");
	struct mtor_cancel_all_turn_ctPTR__er(ck(&pri ss_md_rot_hwor_canc;user((void	mutex_lock(&private->peail to cqECURE)
			flitem->wb_idxREQUEret		pr_ATEoto error;
	quest(struct mdss_rot_mgr *mgrock(&privatelock(&pri ss_md_rot_hwor_canc;use1pe_queue_data(pipe;
	if (r_rot_hwor_canc;
	ifmgrock(&privatelock(tatfors"fail to copy session data\n");
	_rot_htor_cancel_all_tss_md_rot_hwor_canc;use1pe_queue_data, &config, sigr->ploc.t);
	mtfors,nL);
"fail to copy session data\n");
		retug
	 * order, _user_err;
	}

rl(mgr, false);_rot_mgr *mgrock(&privatelock(&pri ss_md_rot_hwor_canc;use1pe_queuding(mgr, perfnvalllEntry add(ck(&priding(mgr, per;
	if mgr *mgrock(&privatelock(&prin;
	return id;
}

static in->ev, perf);
	}
	mutex_unlock(tfors"faiurn retator_
_rot_hwor_canc;use1:in;
	return id;
}

static in_rot_hwor_canc;use->dev, perf);
done:
	ATRACE_ENDtfors"faiev, req);
		}
	}
	mutex_unlock(&prit mdss_rotator_validate_fmt_and_item_fl,%u}.ntainerin>dev*in>depm *item)nsig *nsige_private *priv, *priv_next;

	mutex_loged++;
-EINVAL;
ock(&TRACE_BEGIN(_DEV++;
-EINg_count) == 0)ss_rotat(not_in_suspend_m)ATRACE_BEGINPERunc
	t;

	mutr->dev, sizeof(*)ss_rotat, GFP_KERNEL);
	if (rivate-rYPE>work_distribution) rivate-ocate work_distribution\n");
	struct mtnsig t;

	mut	}

	ret = mdss_rotIN(__func__) is_lock);

	I_for_each_entry_safeis_lock);

	I_for_eacss_rotator_fist);

	ret = mdssx_unlock(&private->rst);

	ret = mdssx_unlockk(&private->pst);

	ret = mdssx_unlocktor_calc_	mutex_lock()ss_rotatch_entry_safe(prive->perx_unlocktor_, ()ss_rotatch_entvate->pL;
		devm_kfree(&mgr-x_loged++	rx_unlocknsig =tnsig in;
	return id;
)ss_rotatch_entry_safnt mdss_rtator_validat_format(struct mdsv_next;

_ -EINegr)
{
	u32 id;
	mutex_locky *entry)
{
	if (entv_next;

	mutex_loe_private *priv, *priv_next;

	mute_x_lock(_&mgr->file_= confi_datadp_get_fock);
	list_for_each_entry_safe(priv, priv_next, &mgr->f_x_lock_&mgr->fil {
		mdss_rotator_release_rottion_&mgrt(&rx_loegoto dnsc_er(&entry-	b) =k

static _lock);
	}

	mutex_lock(&mgr->bust mdss_rotator_close_session(struct mdss_rot.ntainerin>dev*in>depm *item)nsig *nsige_private *priv, *priv_next;

	mutex_loged++;
-EINVAL;
ock(&TRACE_BEGIN(_DEV++;
-EIN!L;
		devm_kfree(&m)	reACE_BEGIN(__func
	t;

	mutr-r)
{
	u32 id;
	muv_next;

	mute)L;
		devm_kfree(&m++;
-EIN!(at(struct mdsv_next;

_ -EINegrotator_re rivate-("ocate work_diC -Et);
not ex;
	}
ut:%u\n", out
	muv_next;

	mu	}

	ATRACE_BEGIN(__func__);
ry(rot_mgr, entry);
	entry->qu		mdss_rotatotator_re rivate-alc_	mutex_lock()ss_rotatch_entry_safe(priv	priv->file->pnlocktor_caliev, req);
		)ss_rotat, GFP_KERNE		pr_err("fL;
		devm_kfree(&mgr->pdev->ile_lock);

	mdss_rotator_update_perf(mgr);
}

static int mdssAL;
ock(int mdss_rtator_#ifdef CONFIG< rMPAT_validate_fmt_and_item_fl_rot_hwor_canc32r,
	struct mdss_rot_file_private *private, unsigned long arg)
{
	struct mdp_rotation_config config;
	struct m		retur32-tr->ploc32_fr *item;
	struct mdss_rot_perf sgr->pdev->ntainer *req, *req_next;

	mutex_locgr->pdev->om_u!mgr->alloc(	int ret );qe		ueues; _pending(inpusd);
ieq__cnt(ownscale not suppocelled
	 *ce on mit,
		dett);
sec;
		rif (ctENOMEM;
	}

	ATRACE_BEGINPERunc__); nfig, (void __user *)agr->ploc32zeof(config));
	if (rYPE_Be_count, r->ploc32("fail to copy session data\n");
		retustruct mtor_cancel_all_turn retator_verify_qtator_frngr->ploc32.		ueuestork_e!y_qtator_ = -EIy_qtator_f>eMAX_LAYE < rUNoutput format\n"i ratio %dx%d->%locgator_f:;
	}

	reqs_rota

	reACE_BEGIN(__func__);
s[i].timeline.ntainer *struct mdss_rot) *
);qe		ueuesterf sgr->dev, sizeof(*perf), GFP_KERNEL);
f->work_distrib("Unrecrf srocate work_distribution\n");
	struct mtcrf s	}

	ATRACE_BEGIN(__func__) nfig, (void __user *)_fors,ns(cont_ptr, r->ploc32.t);
)	mL);
"fail to copy session data\n");
		retustruct mtcrf s	}

	ATRss_md_rot_hwor_canc reuser((void *q_ctrl(mgr, true);
qex_unl rotatfors,ngr->ploc32.		ueurYPEgr->ploc32.rn ret;

	retIS__errOR_>pde(loc("ocate work_distribution\n");
	struct mtor_cancel_all_turn_ctPTR__er(ck(&pri ss_md_rot_hwor_canc reuser((void	mutex_lock(&private->peail to cqECURE)
			flitem->wb_idxREQUEret		pr_ATEoto error;
	quest(struct mdss_rot_mgr *mgrock(&privatelock(&pri ss_md_rot_hwor_canc reuse1pe_queue_data(pipe;
	if (r_rot_hwor_canc;
	ifmgrock(&privatelock(tatfors"fail to copy session data\n");
	_rot_htor_cancel_all_tss_md_rot_hwor_canc reuse1pe_queue_data, &config, sis(cont_ptr, r->ploc32.t);
)	mtfors,nL);
"fail to copy session data\n");
		retug
	 * order, _user_err;
	}

rl(mgr, false);_rot_mgr *mgrock(&privatelock(&pri ss_md_rot_hwor_canc reuse1pe_queuding(mgr, perfnvalllEntry add(ck(&priding(mgr, per;
	if mgr *mgrock(&privatelock(&prin;
	return id;
}

static in->ev, perf);
	}
	mutex_unlock(tfors"faiurn retator_
_rot_hwor_canc reuse1:in;
	return id;
}

static in_rot_hwor_canc reuse->dev, perf);
done:
	ATRACE_ENDtfors"faiev, req);
		}
	}
	mutex_unlock(&prit mdss_rotator_validatruct mdp_om_u__do_s(cont_iol,
	mgr(ruct mdp_om_ucmd32(&mgr-uct mdp_om_ucmdct.w;SS_MD (cmd32(y sec);
	flitem->wb_idxREQUEre32:;

	md &
		item->wb_idxREQUErepri b) =k

sc);
	flitem->wb_idxOPEN32:;

	md &
		item->wb_idxOPENpri b) =k

sc);
	flitem->wb_idxCLOSE32:;

	md &
		item->wb_idxCLOSEpri b) =k

sc);
	flitem->wb_idxCONFIG32:;

	md &
		item->wb_idxCONFIGpri b) =k

sdefault:;

	md &
cmd32pri b) =k

sc u32 mdss_cmdctor_validatrotaty(hw, entry);
	iont_iol,
( *item)nsig *nsig, -uct mdp_om_ucmd,gr-uct mdp__rot_perf *perf;
	bool offloav_next;

	mutex_loged++e src_h, dspr_err("InvalidVAL;
ock(&TRACE_BEGIN(_DEV++;
-EINg_count) == 0)ss_rotat(not_in_suspend_m)ATRACE_BEGINPERunc
	-EIN!L;
		devm_kfree(&m)	reACE_BEGIN(__func
	t;

	mutr-r)
{
	u32 id;
	muv_next;

	mute)L;
		devm_kfree(&m++;
-EIN!(at(struct mdsv_next;

_ -EINegrotator_re rivate-("ocate work_diC -Et);
iol,
x;
	}
ut:%u\n", out
	muv_next;

	mu	}

	ATRACE_BEGIN(__func__);
	md &
__do_s(cont_iol,
	mgr(	md)ct.w;SS_MD (cmd(y sec);
	flitem->wb_idxREQUEre:;

	mutex_lock(&"r, true);
qcanc r_all_turn_ctt_and_item_fl_rot_hwor_canc32rotator_re rivate,_perf;;

	mutex_et;
"r, true);
qcanc r_all_tb) =k

sc);
	flitem->wb_idxOPEN:l_turn_ctt_and_item_fl_rot_mgr *mgrotator_re rivate,_perf;;

b) =k

sc);
	flitem->wb_idxCLOSE:l_turn_ctt_and_item_fls_rot_mgr *mgrotator_re rivate,_perf;;

b) =k

sc);
	flitem->wb_idxCONFIG:l_turn_ctt_and_item_fls (ret)mgr *mgrotator_re rivate,_perf;;

b) =k

sdefault:;

dp_smp_resexpec,
		IOCTLe;
	}

		md)ct__); _penrmmit failed %d\n", retiol,
=%dn;
	}

,entr=;
	}

		md
	ret = murn retator_
}
#erf)fr_validatrotaty(hw, entry);iol,
( *item)nsig *nsig, -uct mdp_om_ucmd,grrrrrrtruct mdss_rot_perf *perf;
	bool offloav_next;

	mutex_loged++e src_h, dspr_err("InvalidVAL;
ock(&TRACE_BEGIN(_DEV++;
-EINg_count) == 0)ss_rotat(not_in_suspend_m)ATRACE_BEGINPERunc
	-EIN!L;
		devm_kfree(&m)	reACE_BEGIN(__func
	t;

	mutr-r)
{
	u32 id;
	muv_next;

	mute)L;
		devm_kfree(&m++;
-EIN!(at(struct mdsv_next;

_ -EINegrotator_re rivate-("ocate work_diC -Et);
iol,
x;
	}
ut:%u\n", out
	muv_next;

	mu	}

	ATRACE_BEGIN(__func__);
;SS_MD (cmd(y sec);
	flitem->wb_idxREQUEre:;

	mutex_lock(&"r, true);
qcanc_all_turn_ctt_and_item_fl_rot_hwor_cancrotator_re rivate,_perf;;

	mutex_et;
"r, true);
qcanc_all_tb) =k

sc);
	flitem->wb_idxOPEN:l_turn_ctt_and_item_fl_rot_mgr *mgrotator_re rivate,_perf;;

b) =k

sc);
	flitem->wb_idxCLOSE:l_turn_ctt_and_item_fls_rot_mgr *mgrotator_re rivate,_perf;;

b) =k

sc);
	flitem->wb_idxCONFIG:l_turn_ctt_and_item_fls (ret)mgr *mgrotator_re rivate,_perf;;

b) =k

sdefault:;

dp_smp_resexpec,
		IOCTLe;
	}

		md)ct__); _penrmmit failed %d\n", retiol,
=%dn;
	}

,entr=;
	}

		md
	ret = murn retator_or_validatsL);
_fmt_and_item_flshow_capabilitiegr,
	stru(not_i *->queue_
	stru(not_i_at(&mgr-i *at(&
		har *pipe *per);
_fmledev-PAGE_SIZE++e srccn_ROTATION_NOPVAL;
ock(&TRACE_BEGceues;#define SPRINTx, it ...) \ mdicn_R+timcn rintf(pip +Gceu,mlede-Gceu,m, it ##_t		_ARGS__m)A
	SPRINTx"wbe		ueu=;
	}

	)ss_rotat"invalid wri;
	SPRINTx"return re=;
	}

	)ss_rotat
}

return resafnt mdss_rceuesor_validatDEVIex_ATTR(caps,nS_IRUGO,mt_and_item_flshow_capabilitieg,->pdesafnvalidats
	struat(&mgr-i *at(struct mdsvs_at(&s[]st_buf&(no_at(&_caps.at(&

	>pde
}afnvalidats
	struat(&mgr-i_group at(struct mdsvs_at(&_group t_buf.at(&te_flags(item->flvs_at(&s
}afnvalidats (stm *item)nsigl_rorem->flflags(item->flvopdst_buf.etuerst_THIS_MODULE,uf.epedev-t_and_item_fl_rot,uf.ntry);
_ctt_and_item_fls_rot,uf.urn ided;iol,
_ctt_and_item_fliol,
,_#ifdef CONFIG< rMPAT_	.s(cont_iol,
ntry(hw, entry);
	iont_iol,
,
#erf)fr}afnvalidat_hw(struct mdss_roarot_dtry,gr,
	struct mdss_rot_file_private *pplatrf_e_(not_i *->qf *perf;
	bo(not_i_n>dev*n>de++e src_h, ds0->ial= confi_gisterry,g_* fied++e srcusec);
sprin;otat(ex_uy,g.y,g_rn re_p= mdss_msmry,g_cl(inpup= md(->qf;

	retIS__errOR_>pde(;otat(ex_uy,g.y,g_rn re_p= md)oto error;
	PTR__er(;otat(ex_uy,g.y,g_rn re_p= md);rottion! copy ses	_h, dspr_err("Iate the entmsmry,g_cl(inpup= mdn;
	}

. _h,=;
	}

	ret = mdn;otat(ex_uy,g.y,g_rn re_p= mdss_>pdev->datic u32 gisterry,g_* fiedss_ofxt;_rortyt) ==_ con(mutex_un.ofxn>dep->d"q
	i,ct m-
}
-2 g-y,g""fail to cgisterry,g_* fiedpy sesn>devs_ofxinpuchil=_ y_name( mdn    mutex_un.ofxn>dep "q
	i,ct m-ss_-2 g-y,g""faittion!n>depy ses	;otat2 guy,g.y,g_rn re_p= mdss_0)ss_2 guy,g_rn re_terve= mdnusec);
sator++;
2 guy,g.y,g_rn re_p= md->nuuser c);
sprRLAYt; i++)
		mdss_er c);
sps[i].rot_w		)ss_2 guy,g_er c);
s		synuusonthsato1;t_w		)ss_2 guy,g_er c);
s		syve	}
	sitem->i	0)ss_2 guy,g_ve	}
	s		sc otator_actl;
		pip	;otat2 guy,g.y,g_rn re_p= mdssem->ims