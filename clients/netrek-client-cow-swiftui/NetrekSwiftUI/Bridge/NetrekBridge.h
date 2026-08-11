/* NetrekBridge.h - Bridging header for Swift ↔ C interop
 *
 * This header is imported by Swift and exposes the C functions
 * needed for the SwiftUI frontend to communicate with the COW engine.
 */

#ifndef NetrekBridge_h
#define NetrekBridge_h

#include <CoreGraphics/CoreGraphics.h>
#include <pthread.h>
#include <stdint.h>

/* Maximum number of virtual windows */
#define BRIDGE_MAX_WINDOWS 256

/* Snapshot data published by W_Flush() for SwiftUI to display.
 * Access protected by snapshot_mutex. */
extern CGImageRef snapshots[BRIDGE_MAX_WINDOWS];
extern int snapshot_positions[BRIDGE_MAX_WINDOWS][4]; /* x, y, w, h */
extern int snapshot_mapped[BRIDGE_MAX_WINDOWS];
extern int snapshot_count;
extern uint64_t snapshot_version;
extern pthread_mutex_t snapshot_mutex;

/* Start the COW engine on a background thread.
 * argv[0] should be the program name; argv contains -h host -p port etc. */
void engine_start(int argc, char **argv);

/* Inject events from SwiftUI into the C engine's event queue.
 * window_id: index into windows[] array (-1 for "find by coordinates")
 * type: 1=expose, 2=key, 3=button, 5=motion_button */
void swiftui_inject_key(int window_id, unsigned char key, int x, int y,
                        int type, int modifier);
void swiftui_inject_button(int window_id, int button, int x, int y,
                           int modifier);
void swiftui_inject_expose(int window_id);

/* Get the number of active windows */
int swiftui_get_num_windows(void);

/* Get window info for layout purposes */
void swiftui_get_window_info(int window_id, int *x, int *y,
                             int *width, int *height, int *mapped);

/* Find which window contains the given screen coordinate */
int swiftui_find_window_at(int x, int y);

#endif /* NetrekBridge_h */
