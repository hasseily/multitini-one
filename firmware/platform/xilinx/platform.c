/* SPDX-License-Identifier: GPL-3.0-only */
#include "platform.h"

#include "bspconfig.h"
#include "xparameters.h"

#ifndef STDIN_BASEADDRESS
#error "The standalone BSP must define STDIN_BASEADDRESS"
#endif
#ifndef STDOUT_BASEADDRESS
#error "The standalone BSP must define STDOUT_BASEADDRESS"
#endif
#if STDIN_BASEADDRESS != XPAR_XUARTPS_0_BASEADDR
#error "The diagnostic recovery console must use PS UART0 for input"
#endif
#if STDOUT_BASEADDRESS != XPAR_XUARTPS_0_BASEADDR
#error "The diagnostic recovery console must use PS UART0 for output"
#endif

/*
 * The Zynq FSBL initializes the PS clocks, MIO, DDR, and UART before handing
 * control to this standalone application.  The standalone BSP startup owns
 * the ARM caches and MMU.  Vitis' generic hello-world platform hook is a
 * no-op for ARM targets as well; keeping the hook here makes that boot
 * contract explicit and avoids a template-generated platform.c/platform.h
 * dependency.
 */
void init_platform(void)
{
}

void cleanup_platform(void)
{
}
