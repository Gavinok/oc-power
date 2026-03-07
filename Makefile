DEVICE   ?= fenix7
PORT     ?= /dev/ttyUSB0
PRG_DIR  := connect_iq_power
ESP_DIR  := power_meter

.PHONY: all watch esp flash monitor clean

all: watch esp

# ── Connect IQ ───────────────────────────────────────────────────────────────

watch: $(PRG_DIR)/PaddlePower.prg $(PRG_DIR)/StrokeRate.prg $(PRG_DIR)/DebugPower.prg mount install

$(PRG_DIR)/PaddlePower.prg: $(wildcard $(PRG_DIR)/source/*.mc) \
                             $(wildcard $(PRG_DIR)/resources/**/*) \
                             $(PRG_DIR)/monkey.jungle
	cd $(PRG_DIR) && ./monkeyc -d $(DEVICE) -o PaddlePower.prg

$(PRG_DIR)/StrokeRate.prg: $(wildcard $(PRG_DIR)/source/*.mc) \
                            $(wildcard $(PRG_DIR)/resources/**/*) \
                            $(PRG_DIR)/stroke_rate.jungle
	cd $(PRG_DIR) && ./monkeyc --stroke-rate -d $(DEVICE) -o StrokeRate.prg

$(PRG_DIR)/DebugPower.prg: $(wildcard $(PRG_DIR)/source/*.mc) \
                            $(wildcard $(PRG_DIR)/resources/**/*) \
                            $(PRG_DIR)/debug.jungle
	cd $(PRG_DIR) && ./monkeyc --debug -d $(DEVICE) -o DebugPower.prg

mount: ~/garmin
	jmtpfs ~/garmin

install: ~/garmin $(PRG_DIR)/PaddlePower.prg $(PRG_DIR)/DebugPower.prg
	cp $(PRG_DIR)/PaddlePower.prg ~/garmin/Internal\ Storage/GARMIN/Apps/
	cp $(PRG_DIR)/DebugPower.prg ~/garmin/Internal\ Storage/GARMIN/Apps/
	fusermount -u ~/garmin
# ── ESP32 ─────────────────────────────────────────────────────────────────────

esp:
	. /opt/esp-idf/export.sh && cd $(ESP_DIR) && idf.py build

flash:
	. /opt/esp-idf/export.sh && cd $(ESP_DIR) && idf.py -p $(PORT) flash

monitor:
	. /opt/esp-idf/export.sh && cd $(ESP_DIR) && idf.py -p $(PORT) monitor

flash-monitor:
	. /opt/esp-idf/export.sh && cd $(ESP_DIR) && idf.py -p $(PORT) flash monitor

# ── Housekeeping ──────────────────────────────────────────────────────────────

clean:
	cd $(PRG_DIR) && rm -f PaddlePower.prg PaddlePower.prg.debug.xml \
	                        StrokeRate.prg StrokeRate.prg.debug.xml \
	                        DebugPower.prg DebugPower.prg.debug.xml
	-. /opt/esp-idf/export.sh && cd $(ESP_DIR) && idf.py fullclean
