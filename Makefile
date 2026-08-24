CC = gcc
BASE_CFLAGS = -Wall -Wextra -std=c99
LDFLAGS = -lm

# -----------------------------------------------------
# Optional Debug Configuration
# Usage: make DEBUG=1 [target] (e.g., make test_astro_interactive DEBUG=1)
# -----------------------------------------------------
DEBUG ?= 0
ifeq ($(DEBUG), 1)
    CFLAGS = $(BASE_CFLAGS) -g -O0 -DSPATIALZ_DEBUG -DDEBUG
    $(info >>> DEBUG MODE ENABLED (-g -O0 -DSPATIALZ_DEBUG) <<<)
else
    CFLAGS = $(BASE_CFLAGS) -O3
endif

SRC = src/codec.c src/context.c src/distances.c src/ranges.c src/utils.c
OBJ = $(SRC:.c=.o)

BUILD_DIR = build
LOGS_DIR = logs
LIB_NAME = $(BUILD_DIR)/libspatial_z.a

all: lib

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# -----------------------------------------------------
# Library Generation
# -----------------------------------------------------
lib: $(BUILD_DIR) $(LIB_NAME)

$(LIB_NAME): $(OBJ)
	ar rcs $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# -----------------------------------------------------
# Tests
# -----------------------------------------------------
test_codec: $(BUILD_DIR) tests/test_codec_roundtrip.c $(SRC)
	$(CC) $(CFLAGS) tests/test_codec_roundtrip.c $(SRC) -o $(BUILD_DIR)/test_codec_roundtrip $(LDFLAGS)
	./$(BUILD_DIR)/test_codec_roundtrip

test_range_generation: $(BUILD_DIR) tests/test_range_generation.c $(SRC)
	$(CC) $(CFLAGS) tests/test_range_generation.c $(SRC) -o $(BUILD_DIR)/test_range_generation $(LDFLAGS)
	./$(BUILD_DIR)/test_range_generation

test_point_in_radius: $(BUILD_DIR) tests/test_point_in_radius.c $(SRC)
	$(CC) $(CFLAGS) tests/test_point_in_radius.c $(SRC) -o $(BUILD_DIR)/test_point_in_radius $(LDFLAGS)
	./$(BUILD_DIR)/test_point_in_radius

test_astro_interactive: $(BUILD_DIR) tests/test_astro_interactive.c $(SRC)
	$(CC) $(CFLAGS) tests/test_astro_interactive.c $(SRC) -o $(BUILD_DIR)/test_astro_interactive $(LDFLAGS)
	./$(BUILD_DIR)/test_astro_interactive

# -----------------------------------------------------
# Python Scripts
# -----------------------------------------------------
plot_benchmark:
	python3 scripts/plot_benchmark.py

plot_interactive:
	python3 scripts/plot_interactive.py

# -----------------------------------------------------
# Cleanup
# -----------------------------------------------------
clean:
	rm -rf $(BUILD_DIR)
	rm -rf $(LOGS_DIR)
	rm -f $(OBJ)
