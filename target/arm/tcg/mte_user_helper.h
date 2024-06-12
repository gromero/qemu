/*
 * ARM MemTag convenience functions.
 *
 * Copyright (c) 2024 Linaro, Ltd.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef MTE_H
#define MTE_H

#ifdef CONFIG_TCG
#ifdef CONFIG_USER_ONLY
#include "sys/prctl.h"

static void arm_set_mte_tcf0(CPUArchState *env, abi_long value)
{
    /*
     * Write PR_MTE_TCF to SCTLR_EL1[TCF0].
     *
     * The kernel has a per-cpu configuration for the sysadmin,
     * /sys/devices/system/cpu/cpu<N>/mte_tcf_preferred,
     * which qemu does not implement.
     *
     * Because there is no performance difference between the modes, and
     * because SYNC is most useful for debugging MTE errors, choose SYNC
     * as the preferred mode.  With this preference, and the way the API
     * uses only two bits, there is no way for the program to select
     * ASYMM mode.
     */
    unsigned tcf = 0;
    if (value & PR_MTE_TCF_SYNC) {
        tcf = 1;
    } else if (value & PR_MTE_TCF_ASYNC) {
        tcf = 2;
    }
    env->cp15.sctlr_el[1] = deposit64(env->cp15.sctlr_el[1], 38, 2, tcf);
}
#endif /* CONFIG_USER_ONLY */
#endif /* CONFIG_TCG */

#endif /* MTE_H */
