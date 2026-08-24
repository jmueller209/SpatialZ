#include <stdint.h>
#include <math.h>
#include "utils.h"
#include "../include/spatial_z.h"

#define GRID_MAX_UINT 4294967295.0

/* 
 * 1. DIE SCHLEUSEN
 * Diese Funktionen übersetzen zwischen der wilden Welt des Nutzers 
 * und dem perfekten internen Kugelmodell (0-180, 0-360).
 */

void to_internal_sphere(float user_y, float user_x, SpatialzCtx ctx, 
                        float *internal_y, float *internal_x) 
{
    // Auf den mathematischen Nullpunkt verschieben
    float y = user_y - ctx.min_axis1;
    float x = user_x - ctx.min_axis2;

    // Y-Achse um die Kugel wickeln
    y = fmod(y, 360.0);
    if (y < 0.0) y += 360.0;

    if (y > 180.0) {
        y = 360.0 - y; // Am Pol spiegeln
        x += 180.0;    // Auf die Rückseite rotieren
    }

    // X-Achse in 360-Grad-Fenster zwingen
    x = fmod(x, 360.0);
    if (x < 0.0) x += 360.0;

    *internal_y = y; // Garantiert [0.0, 180.0]
    *internal_x = x; // Garantiert [0.0, 360.0)
}

void to_user_space(float internal_y, float internal_x, SpatialzCtx ctx, 
                   float *user_y, float *user_x) 
{
    // Die reinen Kugelkoordinaten einfach wieder ins User-System schieben
    *user_y = internal_y + ctx.min_axis1;
    *user_x = internal_x + ctx.min_axis2;
}


/* 
 * 2. DER DUMME KERN (GRID-FUNKTIONEN)
 * Diese Funktionen kennen keinen Kontext mehr. Sie wissen nur:
 * Y ist exakt 180 Grad lang, X ist exakt 360 Grad lang.
 */

uint32_t axis1_to_grid(float internal_y)
{
    const float norm = internal_y / 180.0;

    if (norm <= 0.0) return 0U;
    if (norm >= 1.0) return UINT32_MAX;

    return (uint32_t)(norm * GRID_MAX_UINT);
}

uint32_t axis2_to_grid(float internal_x)
{
    const float norm = internal_x / 360.0;

    if (norm <= 0.0) return 0U;
    if (norm >= 1.0) return UINT32_MAX;

    return (uint32_t)(norm * GRID_MAX_UINT);
}

float grid_to_axis1(uint32_t grid_y)
{
    const float norm = (float)grid_y / GRID_MAX_UINT;
    return norm * 180.0;
}

float grid_to_axis2(uint32_t grid_x)
{
    const float norm = (float)grid_x / GRID_MAX_UINT;
    return norm * 360.0;
}
