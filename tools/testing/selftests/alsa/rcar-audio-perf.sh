#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# rcar-audio-perf.sh - Performance test script for R-Car audio driver
#                      on r8a78000-ironhide (R-Car X5H) board
#
# The r8a78000-ironhide board uses:
#   - R-Car sound driver (rsnd) with SSI5
#   - AK4619 codec (Asahi Kasei)
#   - Simple audio mux (GP6_21) for switching Playback/Capture direction
#   - Sound card name: "rcar-sound"
#
# Usage:
#   ./rcar-audio-perf.sh [OPTIONS]
#
# Options:
#   -c <card>   ALSA card name or index (default: rcar-sound)
#   -d <sec>    Test duration in seconds per test case (default: 5)
#   -o <file>   Output report file (default: rcar-audio-perf-report.txt)
#   -p          Run playback tests only
#   -r          Run capture tests only
#   -v          Verbose output
#   -h          Show this help message

set -e

CARD_NAME="rcar-sound"
TEST_DURATION=5
REPORT_FILE="rcar-audio-perf-report.txt"
RUN_PLAYBACK=1
RUN_CAPTURE=1
VERBOSE=0
PASS=0
FAIL=0
SKIP=0

# Playback: DAC supports S16_LE, S20_LE, S24_LE, S32_LE
PLAYBACK_FORMATS="S16_LE S24_LE S32_LE"
# Capture: ADC supports S16_LE, S20_LE, S24_LE
CAPTURE_FORMATS="S16_LE S24_LE"
# AK4619 supports 8000–192000 Hz; test common rates
TEST_RATES="8000 16000 44100 48000 96000 192000"
# Channels supported: 1–2
TEST_CHANNELS="1 2"
# Buffer/period configurations for latency and throughput testing (in frames)
BUFFER_SIZES="256 512 1024 2048 4096"

usage() {
	cat <<EOF
Usage: $(basename "$0") [OPTIONS]

R-Car audio driver performance test for r8a78000-ironhide (R-Car X5H)

Options:
  -c <card>   ALSA card name or index (default: $CARD_NAME)
  -d <sec>    Test duration in seconds per test case (default: $TEST_DURATION)
  -o <file>   Output report file (default: $REPORT_FILE)
  -p          Run playback tests only
  -r          Run capture tests only
  -v          Verbose output
  -h          Show this help message
EOF
}

log() {
	echo "$*" | tee -a "$REPORT_FILE"
}

log_verbose() {
	if [ "$VERBOSE" -eq 1 ]; then
		echo "$*" | tee -a "$REPORT_FILE"
	fi
}

pass() {
	log "  PASS: $*"
	PASS=$((PASS + 1))
}

fail() {
	log "  FAIL: $*"
	FAIL=$((FAIL + 1))
}

skip() {
	log "  SKIP: $*"
	SKIP=$((SKIP + 1))
}

check_dependencies() {
	local missing=""

	for cmd in aplay arecord amixer cat grep awk; do
		if ! command -v "$cmd" > /dev/null 2>&1; then
			missing="$missing $cmd"
		fi
	done

	if [ -n "$missing" ]; then
		echo "ERROR: Missing required commands:$missing"
		echo "Please install alsa-utils package."
		exit 1
	fi
}

resolve_card_index() {
	local name="$1"
	local idx

	# If already a number, use it directly
	if echo "$name" | grep -qE '^[0-9]+$'; then
		echo "$name"
		return
	fi

	# Look up by card name in /proc/asound/cards
	idx=$(grep -i "$name" /proc/asound/cards 2>/dev/null | \
		awk 'NR==1 { gsub(/[^0-9]/, "", $1); print $1 }')

	if [ -z "$idx" ]; then
		echo ""
		return
	fi
	echo "$idx"
}

check_audio_card() {
	log ""
	log "=== Audio Card Detection ==="

	if [ ! -f /proc/asound/cards ]; then
		log "ERROR: /proc/asound/cards not found. ALSA not available."
		exit 1
	fi

	log "Available ALSA cards:"
	cat /proc/asound/cards | tee -a "$REPORT_FILE"

	CARD_INDEX=$(resolve_card_index "$CARD_NAME")
	if [ -z "$CARD_INDEX" ]; then
		log "ERROR: Audio card '$CARD_NAME' not found."
		log "Available cards:"
		cat /proc/asound/cards
		exit 1
	fi

	log "Using card: $CARD_NAME (index $CARD_INDEX)"

	# Verify the R-Car sound driver is loaded
	if ! grep -qi "rcar\|rsnd" /proc/asound/cards 2>/dev/null; then
		log "WARNING: R-Car sound driver may not be loaded."
	fi

	# Show PCM devices
	log ""
	log "PCM devices for card $CARD_INDEX:"
	if [ -f /proc/asound/card${CARD_INDEX}/pcm0p/info ]; then
		cat /proc/asound/card${CARD_INDEX}/pcm0p/info | tee -a "$REPORT_FILE"
	fi
}

check_driver_info() {
	log ""
	log "=== R-Car Audio Driver Information ==="

	# Show ALSA card info
	if [ -f /proc/asound/card${CARD_INDEX}/id ]; then
		log "Card ID: $(cat /proc/asound/card${CARD_INDEX}/id)"
	fi

	# Show mixer controls
	log ""
	log "Mixer controls:"
	amixer -c "$CARD_INDEX" | tee -a "$REPORT_FILE"

	# Check for rcar-specific debug info
	local dbg_path="/sys/kernel/debug/asoc"
	if [ -d "$dbg_path" ]; then
		log ""
		log "ASoC debug info ($dbg_path):"
		for f in "$dbg_path"/*; do
			[ -f "$f" ] || continue
			log "  $(basename "$f"):"
			cat "$f" | sed 's/^/    /' | tee -a "$REPORT_FILE"
		done
	fi
}

setup_playback_mixer() {
	log_verbose "  Setting up mixer for playback..."
	amixer -c "$CARD_INDEX" set "MUX" "Playback" > /dev/null 2>&1 || true
	amixer -c "$CARD_INDEX" set "DAC 1" 85% > /dev/null 2>&1 || true
}

setup_capture_mixer() {
	log_verbose "  Setting up mixer for capture..."
	amixer -c "$CARD_INDEX" set "MUX" "Capture" > /dev/null 2>&1 || true
	amixer -c "$CARD_INDEX" set "Mic 1" 50% > /dev/null 2>&1 || true
	amixer -c "$CARD_INDEX" set "ADC 1" on > /dev/null 2>&1 || true
	amixer -c "$CARD_INDEX" set "ADC 1" 80% > /dev/null 2>&1 || true
}

# Generate a silent test audio file for playback
generate_test_audio() {
	local rate="$1"
	local fmt="$2"
	local channels="$3"
	local duration="$4"
	local outfile="$5"

	# Use /dev/zero as silent PCM source and trim to desired length using dd
	local bytes_per_sample
	case "$fmt" in
		S16_LE) bytes_per_sample=2 ;;
		S20_LE) bytes_per_sample=4 ;;
		S24_LE) bytes_per_sample=4 ;;
		S32_LE) bytes_per_sample=4 ;;
		*)      bytes_per_sample=2 ;;
	esac

	local total_bytes=$((rate * channels * bytes_per_sample * duration))
	dd if=/dev/zero of="$outfile" bs=1 count="$total_bytes" > /dev/null 2>&1
}

# Measure CPU usage of a background process for a given duration
measure_cpu_usage() {
	local pid="$1"
	local duration="$2"
	local sample_interval=1
	local total_cpu=0
	local samples=0

	local end_time=$(($(date +%s) + duration))
	while [ "$(date +%s)" -lt "$end_time" ]; do
		if [ ! -d /proc/"$pid" ]; then
			break
		fi
		local cpu
		cpu=$(ps -p "$pid" -o %cpu= 2>/dev/null | tr -d ' ') || break
		if [ -n "$cpu" ]; then
			total_cpu=$(awk "BEGIN { printf \"%.2f\", $total_cpu + $cpu }")
			samples=$((samples + 1))
		fi
		sleep "$sample_interval"
	done

	if [ "$samples" -gt 0 ]; then
		awk "BEGIN { printf \"%.2f\", $total_cpu / $samples }"
	else
		echo "N/A"
	fi
}

# Count xruns from ALSA PCM status for a given card/device
get_xrun_count() {
	local card_idx="$1"
	local dev="${2:-0}"
	local direction="${3:-p}"  # p=playback, c=capture

	local stat_file="/proc/asound/card${card_idx}/pcm${dev}${direction}/sub0/status"
	if [ -f "$stat_file" ]; then
		grep -i "xrun" "$stat_file" 2>/dev/null | \
			awk '{ print $NF }' | head -1 || echo "0"
	else
		echo "N/A"
	fi
}

test_playback_format() {
	local rate="$1"
	local fmt="$2"
	local channels="$3"
	local buf_size="$4"
	local test_name="playback rate=${rate}Hz fmt=${fmt} ch=${channels} buf=${buf_size}"
	local tmpfile
	tmpfile=$(mktemp /tmp/rcar-audio-test-XXXXXX.raw)
	local result="FAIL"
	local cpu_avg="N/A"
	local xruns_before xruns_after xrun_diff

	log_verbose "  Testing: $test_name"

	# Generate silent audio data for the test
	generate_test_audio "$rate" "$fmt" "$channels" "$TEST_DURATION" "$tmpfile"

	xruns_before=$(get_xrun_count "$CARD_INDEX" 0 "p")

	# Run aplay with the specified parameters
	if aplay -q \
		-D "hw:${CARD_INDEX},0" \
		-r "$rate" \
		-f "$fmt" \
		-c "$channels" \
		--buffer-size="$buf_size" \
		"$tmpfile" > /dev/null 2>&1; then
		result="PASS"
	fi

	xruns_after=$(get_xrun_count "$CARD_INDEX" 0 "p")

	# Calculate xruns during this test
	if [ "$xruns_before" != "N/A" ] && [ "$xruns_after" != "N/A" ]; then
		xrun_diff=$((xruns_after - xruns_before))
	else
		xrun_diff="N/A"
	fi

	rm -f "$tmpfile"

	if [ "$result" = "PASS" ]; then
		log "  PASS: $test_name | xruns=$xrun_diff"
		PASS=$((PASS + 1))
	else
		log "  FAIL: $test_name"
		FAIL=$((FAIL + 1))
	fi
}

test_capture_format() {
	local rate="$1"
	local fmt="$2"
	local channels="$3"
	local buf_size="$4"
	local test_name="capture rate=${rate}Hz fmt=${fmt} ch=${channels} buf=${buf_size}"
	local tmpfile
	tmpfile=$(mktemp /tmp/rcar-audio-test-XXXXXX.raw)
	local result="FAIL"
	local xruns_before xruns_after xrun_diff
	local bytes_captured=0
	local throughput="N/A"

	log_verbose "  Testing: $test_name"

	xruns_before=$(get_xrun_count "$CARD_INDEX" 0 "c")

	# Run arecord for the test duration
	if timeout $((TEST_DURATION + 2)) \
		arecord -q \
		-D "hw:${CARD_INDEX},0" \
		-r "$rate" \
		-f "$fmt" \
		-c "$channels" \
		--buffer-size="$buf_size" \
		-d "$TEST_DURATION" \
		"$tmpfile" > /dev/null 2>&1; then
		result="PASS"
		# Calculate throughput
		if [ -f "$tmpfile" ]; then
			bytes_captured=$(wc -c < "$tmpfile")
			throughput=$(awk "BEGIN { printf \"%.1f\", $bytes_captured / $TEST_DURATION / 1024 }")
			throughput="${throughput} KB/s"
		fi
	fi

	xruns_after=$(get_xrun_count "$CARD_INDEX" 0 "c")

	if [ "$xruns_before" != "N/A" ] && [ "$xruns_after" != "N/A" ]; then
		xrun_diff=$((xruns_after - xruns_before))
	else
		xrun_diff="N/A"
	fi

	rm -f "$tmpfile"

	if [ "$result" = "PASS" ]; then
		log "  PASS: $test_name | throughput=$throughput xruns=$xrun_diff"
		PASS=$((PASS + 1))
	else
		log "  FAIL: $test_name"
		FAIL=$((FAIL + 1))
	fi
}

test_playback_latency() {
	local rate="$1"
	local fmt="$2"
	local channels="$3"
	local test_name="playback_latency rate=${rate}Hz fmt=${fmt} ch=${channels}"
	local tmpfile
	tmpfile=$(mktemp /tmp/rcar-audio-test-XXXXXX.raw)
	local latency_us="N/A"

	generate_test_audio "$rate" "$fmt" "$channels" "$TEST_DURATION" "$tmpfile"

	# aplay --dump-hw-params reports the negotiated hardware buffer and period sizes
	local hw_params
	hw_params=$(aplay -q \
		-D "hw:${CARD_INDEX},0" \
		-r "$rate" \
		-f "$fmt" \
		-c "$channels" \
		--dump-hw-params \
		"$tmpfile" 2>&1 || true)

	rm -f "$tmpfile"

	# Extract period size to compute latency
	local period_size
	period_size=$(echo "$hw_params" | grep -i "PERIOD_SIZE" | \
		awk '{ print $NF }' | head -1)
	if [ -n "$period_size" ]; then
		# Latency = period_size / rate (in microseconds)
		latency_us=$(awk "BEGIN { printf \"%.1f\", ($period_size / $rate) * 1000000 }")
		log "  INFO: $test_name | period_size=${period_size} latency=${latency_us}us"
	else
		log_verbose "  INFO: $test_name | could not determine period size"
	fi
}

test_playback_cpu_usage() {
	local rate="$1"
	local fmt="$2"
	local channels="$3"
	local test_name="playback_cpu rate=${rate}Hz fmt=${fmt} ch=${channels}"
	local tmpfile
	tmpfile=$(mktemp /tmp/rcar-audio-test-XXXXXX.raw)

	generate_test_audio "$rate" "$fmt" "$channels" "$TEST_DURATION" "$tmpfile"

	# Start aplay in background and measure CPU
	aplay -q \
		-D "hw:${CARD_INDEX},0" \
		-r "$rate" \
		-f "$fmt" \
		-c "$channels" \
		"$tmpfile" > /dev/null 2>&1 &
	local aplay_pid=$!

	local cpu_avg
	cpu_avg=$(measure_cpu_usage "$aplay_pid" "$TEST_DURATION")

	wait "$aplay_pid" 2>/dev/null || true
	rm -f "$tmpfile"

	log "  INFO: $test_name | avg_cpu=${cpu_avg}%"
}

test_capture_cpu_usage() {
	local rate="$1"
	local fmt="$2"
	local channels="$3"
	local test_name="capture_cpu rate=${rate}Hz fmt=${fmt} ch=${channels}"
	local tmpfile
	tmpfile=$(mktemp /tmp/rcar-audio-test-XXXXXX.raw)

	# Start arecord in background and measure CPU
	arecord -q \
		-D "hw:${CARD_INDEX},0" \
		-r "$rate" \
		-f "$fmt" \
		-c "$channels" \
		-d "$TEST_DURATION" \
		"$tmpfile" > /dev/null 2>&1 &
	local arecord_pid=$!

	local cpu_avg
	cpu_avg=$(measure_cpu_usage "$arecord_pid" "$TEST_DURATION")

	wait "$arecord_pid" 2>/dev/null || true
	rm -f "$tmpfile"

	log "  INFO: $test_name | avg_cpu=${cpu_avg}%"
}

run_playback_tests() {
	log ""
	log "=== Playback Performance Tests ==="
	log "(SSI5 → AK4619 DAC, requires MUX=Playback)"

	setup_playback_mixer

	# Format and rate sweep
	log ""
	log "--- Playback: Format/Rate Sweep ---"
	for fmt in $PLAYBACK_FORMATS; do
		for rate in $TEST_RATES; do
			for ch in $TEST_CHANNELS; do
				test_playback_format "$rate" "$fmt" "$ch" "4096"
			done
		done
	done

	# Buffer size sweep (latency vs. stability)
	log ""
	log "--- Playback: Buffer Size Sweep (48000 Hz, S16_LE, stereo) ---"
	for buf in $BUFFER_SIZES; do
		test_playback_format "48000" "S16_LE" "2" "$buf"
	done

	# Latency measurements
	log ""
	log "--- Playback: Latency Measurements ---"
	for rate in 8000 48000 192000; do
		test_playback_latency "$rate" "S16_LE" "2"
	done

	# CPU usage measurements
	log ""
	log "--- Playback: CPU Usage ---"
	for rate in 48000 192000; do
		for fmt in S16_LE S32_LE; do
			test_playback_cpu_usage "$rate" "$fmt" "2"
		done
	done
}

run_capture_tests() {
	log ""
	log "=== Capture Performance Tests ==="
	log "(AK4619 ADC → SSI5, requires MUX=Capture)"

	setup_capture_mixer

	# Format and rate sweep (ADC does not support S32_LE)
	log ""
	log "--- Capture: Format/Rate Sweep ---"
	for fmt in $CAPTURE_FORMATS; do
		for rate in $TEST_RATES; do
			for ch in $TEST_CHANNELS; do
				test_capture_format "$rate" "$fmt" "$ch" "4096"
			done
		done
	done

	# Buffer size sweep
	log ""
	log "--- Capture: Buffer Size Sweep (48000 Hz, S16_LE, stereo) ---"
	for buf in $BUFFER_SIZES; do
		test_capture_format "48000" "S16_LE" "2" "$buf"
	done

	# CPU usage measurements
	log ""
	log "--- Capture: CPU Usage ---"
	for rate in 48000 192000; do
		test_capture_cpu_usage "$rate" "S16_LE" "2"
	done
}

print_summary() {
	log ""
	log "======================================="
	log "         Test Summary"
	log "======================================="
	log "  PASS: $PASS"
	log "  FAIL: $FAIL"
	log "  SKIP: $SKIP"
	log "  Total: $((PASS + FAIL + SKIP))"
	log "======================================="
	log "Report saved to: $REPORT_FILE"

	if [ "$FAIL" -gt 0 ]; then
		return 1
	fi
	return 0
}

# Parse arguments
while getopts "c:d:o:prvh" opt; do
	case "$opt" in
		c) CARD_NAME="$OPTARG" ;;
		d) TEST_DURATION="$OPTARG" ;;
		o) REPORT_FILE="$OPTARG" ;;
		p) RUN_PLAYBACK=1; RUN_CAPTURE=0 ;;
		r) RUN_PLAYBACK=0; RUN_CAPTURE=1 ;;
		v) VERBOSE=1 ;;
		h) usage; exit 0 ;;
		*) usage; exit 1 ;;
	esac
done

# Initialize report
{
	echo "======================================="
	echo " R-Car Audio Driver Performance Report"
	echo " Machine : r8a78000-ironhide (R-Car X5H)"
	echo " Date    : $(date)"
	echo "======================================="
} > "$REPORT_FILE"

check_dependencies
check_audio_card
check_driver_info

if [ "$RUN_PLAYBACK" -eq 1 ]; then
	run_playback_tests
fi

if [ "$RUN_CAPTURE" -eq 1 ]; then
	run_capture_tests
fi

print_summary
