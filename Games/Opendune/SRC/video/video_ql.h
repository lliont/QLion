/** @file src/video/video_ql.h QLion hardware-sprite mouse cursor (QL build only). */

#ifndef VIDEO_QL_H
#define VIDEO_QL_H

/* true when opendune.ini has hwcursor=1 */
extern bool QL_HWCursor_Enabled(void);

/* Convert a decoded mouse sprite (g_mouseSprite) into QLion sprites 0-1. */
extern void QL_HWCursor_SetImage(const uint8 *sprite);

/* Show the cursor with its top-left corner at game coordinates left, top. */
extern void QL_HWCursor_Show(int left, int top);

extern void QL_HWCursor_Hide(void);

#endif /* VIDEO_QL_H */
