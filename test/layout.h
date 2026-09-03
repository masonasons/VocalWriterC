/* the host layout of the synthesiser context, for the differential test */
#ifndef VW_LAYOUT_H
#define VW_LAYOUT_H
#include <stddef.h>
typedef struct {
    const char *name;
    unsigned guest_off;      /* in the original (STABS) */
    size_t host_off;         /* offsetof here */
    const char *kind;        /* i16, u16, i32, u32, f32, f64, i8, u8, ptr */
    unsigned count;          /* array length, or 1 */
} vw_field;
extern const vw_field vw_formantVar_fields[];
#endif
