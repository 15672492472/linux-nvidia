/*
 * Copyright (c) 2015, NVIDIA CORPORATION. All rights reserved
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files
 * (the "Software"), to deal in the Software without restriction,
 * including without limitation the rights to use, copy, modify, merge,
 * publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/tegra-powergate.h>
#include <dt-bindings/soc/nvidia,tegra186-powergate.h>
#include <soc/tegra/tegra_bpmp.h>

#include "powergate-priv.h"

#define UPDATE_LOGIC_STATE	0x1
#define LOGIC_STATE_ON		0x1
#define LOGIC_STATE_OFF		0x0

#define UPDATE_SRAM_STATE	0x1
#define SRAM_STATE_SD		(1 << 0)
#define SRAM_STATE_SLP		(1 << 1)
#define SRAM_STATE_DSLP		(1 << 2)
#define SRAM_STATE_ON		0x0

#define UPDATE_CLK_STATE	0x1
#define CLK_STATE_ON		0x1
#define CLK_STATE_OFF		0x0

const char *partition_names[] = {
	[TEGRA_POWERGATE_APE] = "audio",
	[TEGRA_POWERGATE_DFD] = "dfd",
	[TEGRA_POWERGATE_DISA] = "disp",
	[TEGRA_POWERGATE_DISB] = "dispb",
	[TEGRA_POWERGATE_DISC] = "dispc",
	[TEGRA_POWERGATE_ISPA] = "ispa",
	[TEGRA_POWERGATE_NVDEC] = "nvdec",
	[TEGRA_POWERGATE_NVJPG] = "nvjpg",
	[TEGRA_POWERGATE_NVENC] = "nvenc",
	[TEGRA_POWERGATE_PCIE] = "pcie",
	[TEGRA_POWERGATE_SATA] = "sata",
	[TEGRA_POWERGATE_VE] = "ve",
	[TEGRA_POWERGATE_VIC] = "vic",
	[TEGRA_POWERGATE_XUSBA] = "xusba",
	[TEGRA_POWERGATE_XUSBB] = "xusbb",
	[TEGRA_POWERGATE_XUSBC] = "xusbc",
	[TEGRA_POWERGATE_GPU] = "gpu",
};

struct powergate_request {
	uint32_t partition_id;
	uint32_t logic_state;
	uint32_t sram_state;
	uint32_t clk_state;
};

struct powergate_state {
	uint32_t logic_state;
	uint32_t sram_state;
};

static int tegra186_pg_powergate_partition(int id)
{
	int ret;
	struct powergate_request req;

	req.partition_id = id;
	req.logic_state = UPDATE_LOGIC_STATE | (1 << LOGIC_STATE_OFF);
	req.sram_state = UPDATE_SRAM_STATE | (1 << SRAM_STATE_SD);
	req.clk_state = 0;

	ret = tegra_bpmp_send_receive(MRQ_PG_UPDATE_STATE, &req, sizeof(req), NULL, 0);
	if (ret)
		return ret;

	return 0;
}

static int tegra186_pg_unpowergate_partition(int id)
{
	int ret;
	struct powergate_request req;

	req.partition_id = id;
	req.logic_state = UPDATE_LOGIC_STATE | (1 << LOGIC_STATE_ON);
	req.sram_state = UPDATE_SRAM_STATE | (1 << SRAM_STATE_ON);
	req.clk_state = 0;

	ret = tegra_bpmp_send_receive(MRQ_PG_UPDATE_STATE, &req, sizeof(req), NULL, 0);
	if (ret)
		return ret;

	return 0;
}

static int tegra186_pg_powergate_clk_off(int id)
{
	int ret;
	struct powergate_request req;

	req.partition_id = id;
	req.logic_state = UPDATE_LOGIC_STATE | (1 << LOGIC_STATE_OFF);
	req.sram_state = UPDATE_SRAM_STATE | (1 << SRAM_STATE_SD);
	req.clk_state = UPDATE_CLK_STATE | (1 << CLK_STATE_OFF);

	ret = tegra_bpmp_send_receive(MRQ_PG_UPDATE_STATE, &req, sizeof(req), NULL, 0);
	if (ret)
		return ret;

	return 0;
}

static int tegra186_pg_unpowergate_clk_on(int id)
{
	int ret;
	struct powergate_request req;

	req.partition_id = id;
	req.logic_state = UPDATE_LOGIC_STATE | (1 << LOGIC_STATE_ON);
	req.sram_state = UPDATE_SRAM_STATE | (1 << SRAM_STATE_ON);
	req.clk_state = UPDATE_CLK_STATE | (1 << CLK_STATE_ON);

	ret = tegra_bpmp_send_receive(MRQ_PG_UPDATE_STATE, &req, sizeof(req), NULL, 0);
	if (ret)
		return ret;

	return 0;
}

static const char *tegra186_pg_get_name(int id)
{
	return partition_names[id];
}

static bool tegra186_pg_is_powered(int id)
{
	int ret;
	struct powergate_state msg;

	ret = tegra_bpmp_send_receive(MRQ_PG_READ_STATE, &id, sizeof(id), &msg, sizeof(msg));
	if (ret)
		return 0;

	return !!msg.logic_state;
}

static struct powergate_ops tegra186_pg_ops = {
	.soc_name = "tegra186",

	.num_powerdomains = TEGRA_NUM_POWERGATE,

	.get_powergate_domain_name = tegra186_pg_get_name,

	.powergate_partition = tegra186_pg_powergate_partition,
	.unpowergate_partition = tegra186_pg_unpowergate_partition,

	.powergate_partition_with_clk_off = tegra186_pg_powergate_clk_off,
	.unpowergate_partition_with_clk_on = tegra186_pg_unpowergate_clk_on,

	.powergate_is_powered = tegra186_pg_is_powered,
};

struct powergate_ops *tegra186_powergate_init_chip_support(void)
{
	return &tegra186_pg_ops;
}
