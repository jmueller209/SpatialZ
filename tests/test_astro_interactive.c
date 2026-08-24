#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include "../include/spatial_z.h"

#define MAX_RANGES 128

int main(void) {
    printf("=== ASTRO RANGE INTERACTIVE TESTER ===\n");
    
    // Wir nutzen den Astronomie-Kontext (Dec: -90 bis 90, RA: 0 bis 360)
    SpatialzCtx ctx = spatial_create_celestial_ctx();

    char buffer[128];

    while (true) {
        float dec, ra, radius;

        printf("\n----------------------------------------\n");
        printf("Gib 'q' zum Beenden ein oder druecke Enter für neue Abfrage.\n");
        printf("Declination (dec) [-90 bis 90]: ");
        
        if (!fgets(buffer, sizeof(buffer), stdin)) break;
        if (buffer[0] == 'q' || buffer[0] == 'Q') break;
        if (sscanf(buffer, "%f", &dec) != 1) continue;

        printf("Right Ascension (ra) [0 bis 360]: ");
        if (!fgets(buffer, sizeof(buffer), stdin)) break;
        if (sscanf(buffer, "%f", &ra) != 1) continue;

        printf("Radius (Grad): ");
        if (!fgets(buffer, sizeof(buffer), stdin)) break;
        if (sscanf(buffer, "%f", &radius) != 1) continue;

        MortonRange ranges[MAX_RANGES];
        int num_ranges = 0;

        bool success = spatial_get_radius_ranges(
            dec, ra, radius,
            ranges, &num_ranges,
            MAX_RANGES, &ctx
        );

        printf("\n--- ERGEBNIS ---\n");
        printf("Success: %s\n", success ? "true" : "false");
        printf("Anzahl gefundener Ranges: %d\n", num_ranges);

        for (int i = 0; i < num_ranges; i++) {
            printf("  [%d] Start: %llu, End: %llu\n", 
                   i, 
                   (unsigned long long)ranges[i].start_code, 
                   (unsigned long long)ranges[i].end_code);
        }
    }

    printf("Test beendet.\n");
    return 0;
}
