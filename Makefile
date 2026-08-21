CC = gcc
CFLAGS = -Wall -Wextra -O3 -std=c99
LDFLAGS = -lm

SRC = src/codec.c src/distances.c src/ranges.c src/utils.c
BUILD_DIR = build
LOGS_DIR = logs

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

test_codec: $(BUILD_DIR) tests/test_codec_roundtrip.c $(SRC)
	$(CC) $(CFLAGS) tests/test_codec_roundtrip.c $(SRC) -o $(BUILD_DIR)/test_codec_roundtrip $(LDFLAGS)
	./$(BUILD_DIR)/test_codec_roundtrip

test_range_coverage: $(BUILD_DIR) tests/test_range_coverage.c $(SRC)
	$(CC) $(CFLAGS) tests/test_range_coverage.c $(SRC) -o $(BUILD_DIR)/test_range_coverage $(LDFLAGS)
	./$(BUILD_DIR)/test_range_coverage

plot_benchmark:
	python3 scripts/plot_benchmark.py

plot_interactive:
	python3 scripts/plot_interactive.py

plot_terminal:
	python3 scripts/plot_terminal.py

clean:
	rm -rf $(BUILD_DIR)
	rm -rf $(LOGS_DIR)
