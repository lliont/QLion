/** @file src/video/ql_pointer_stub.c
 * Temporary mouse stub for the first QL builds : reports no pointer,
 * so the mouse stays where the game puts it. Replace with the real
 * Pointer Environment / QIMI / register version later. */

#include "types.h"

bool QL_Pointer_Read(int16 *x, int16 *y, uint8 *buttons)
{
	(void)x; (void)y; (void)buttons;
	return false;
}
