/** @file src/video/ql_pointer.c
 * QLion mouse for OpenDUNE : step counters filled by the external
 * interrupt server in ql_mouse.s, buttons read straight from the QIMI
 * button register. Replaces ql_pointer_stub.c.
 */

#include "types.h"

extern volatile int16 mouse_x, mouse_y;		/* ql_mouse.s */
extern void QL_Mouse_Install(void);
extern void QL_Mouse_Remove(void);

#define MOUSE_BTN ((volatile uint8 *)0x1BF9CUL)	/* bit5 L, bit4 R, 0 = pressed */

static bool s_installed = false;

bool QL_Pointer_Read(int16 *x, int16 *y, uint8 *buttons)
{
	uint8 b;

	if (!s_installed) {
		QL_Mouse_Install();
		s_installed = true;
	}

	b = (uint8)~*MOUSE_BTN;
	/* ql_mouse.s already counts in screen directions (right +x, down +y).
	 * If an axis moves the wrong way, swap the direction test for it in
	 * ql_mouse.s (beq <-> bne after the xi/yi btst). */
	*x = mouse_x;
	*y = mouse_y;
	*buttons = (uint8)(((b >> 5) & 1) | (((b >> 4) & 1) << 1));
	return true;
}

/* Call from Video_Uninit() so the server is removed before the program
 * exits; a server left in the list after the job is gone crashes QDOS. */
void QL_Pointer_Uninit(void)
{
	if (s_installed) {
		QL_Mouse_Remove();
		s_installed = false;
	}
}
