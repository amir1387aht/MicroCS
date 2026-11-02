# Optional LVGL 8.x / 9.x (docs/LVGL.md): C# calls the LVGL C API with the C names (lv_obj_create,
# LV_ALIGN_CENTER, lv_style_t...). LVGL is not bundled: `make fetch-lvgl` clones it into
# build/third_party, or point LVGL_DIR at any checkout (8.0 ... 9.x). mcs-lvgl compiles LVGL itself
# with tests/lvgl/v8 or tests/lvgl/v9 lv_conf.h (picked by the version found) and links
# build/mcs_lvgl, whose `--lvgl[=WxH]` option gives scripts an in-memory display + pointer.
#   make mcs-lvgl LVGL_DIR=/path/to/lvgl      (-jN builds LVGL in parallel)  -> build/mcs_lvgl
#   make lvgl-test LVGL_DIR=/path/to/lvgl     tests/lvgl/*.cs against their .out
LVGL_VERSION ?= v9.2.2
LVGL_DIR ?= build/third_party/lvgl-$(LVGL_VERSION)
LVGL_API  = $(if $(wildcard $(LVGL_DIR)/src/display/lv_display.h),v9,v8)
LVGL_ID   = $(LVGL_API)-$(shell echo "$(abspath $(LVGL_DIR))" | cksum | cut -d' ' -f1)
LVGL_OUT  = build/lvgl-$(LVGL_ID)
LVGL_CFLAGS = -O1 -w -ffunction-sections -fdata-sections -DLV_CONF_INCLUDE_SIMPLE -Itests/lvgl/$(LVGL_API) -I$(LVGL_DIR) -I$(LVGL_DIR)/src
fetch-lvgl:
	@mkdir -p build/third_party && [ -f build/third_party/lvgl-$(LVGL_VERSION)/lvgl.h ] || \
	  git clone -q --depth 1 --branch $(LVGL_VERSION) https://github.com/lvgl/lvgl build/third_party/lvgl-$(LVGL_VERSION)
	@echo "LVGL $(LVGL_VERSION) in build/third_party/lvgl-$(LVGL_VERSION)  (make mcs-lvgl LVGL_DIR=build/third_party/lvgl-$(LVGL_VERSION))"
# LVGL_TABLES=single (default): tables for exactly this LVGL; =all: the one file for every 8.x/9.x release
# (modules/lvgl/mcs_lvgl_api.inc, python3 tools/lvgl_gen/build_api.py --all) - what lvgl-test and the releases use
LVGL_TABLES ?= single
LVGL_INC = $(if $(filter all,$(LVGL_TABLES)),$(abspath modules/lvgl/mcs_lvgl_api.inc),$(abspath $(LVGL_OUT))/mcs_lvgl_api.inc)
modules/lvgl/mcs_lvgl_api.inc: tools/lvgl_gen/gen.py tools/lvgl_gen/extract.py tools/lvgl_gen/build_api.py
	python3 tools/lvgl_gen/build_api.py --all
$(LVGL_OUT)/mcs_lvgl_api.inc: tools/lvgl_gen/gen.py tools/lvgl_gen/extract.py tools/lvgl_gen/build_api.py $(LVGL_DIR)/lvgl.h
	@mkdir -p $(LVGL_OUT)
	python3 tools/lvgl_gen/build_api.py --lvgl "$(LVGL_DIR)" --work $(LVGL_OUT)/api --out $@
# the generated tables for every LVGL release (modules/lvgl/mcs_lvgl_api.inc, needs network + pycparser)
lvgl-api:
	python3 tools/lvgl_gen/build_api.py --all
mcs-lvgl: | build
	@[ -f "$(LVGL_DIR)/lvgl.h" ] || { echo "make: LVGL not found in '$(LVGL_DIR)' - run 'make fetch-lvgl [LVGL_VERSION=v8.3.11]' or pass LVGL_DIR=<LVGL checkout>"; exit 1; }
	@mkdir -p $(LVGL_OUT)/lib
	@$(MAKE) --no-print-directory $(if $(filter all,$(LVGL_TABLES)),modules/lvgl/mcs_lvgl_api.inc,$(LVGL_OUT)/mcs_lvgl_api.inc) LVGL_DIR=$(LVGL_DIR)
	@cd "$(LVGL_DIR)" && find src -name '*.c' | sort > $(CURDIR)/$(LVGL_OUT)/srcs.txt
	@echo "LVGL ($(LVGL_API)): $$(wc -l < $(LVGL_OUT)/srcs.txt) sources -> $(LVGL_OUT)/lib"
	@cat $(LVGL_OUT)/srcs.txt | xargs -P $$(nproc 2>/dev/null || echo 2) -I{} sh -c 'o=$(LVGL_OUT)/lib/$$(echo {} | tr / _ | sed s/\\.c$$/.o/); \
	  [ "$$o" -nt "$(LVGL_DIR)/{}" ] && [ "$$o" -nt tests/lvgl/$(LVGL_API)/lv_conf.h ] || $(CC) $(LVGL_CFLAGS) -c "$(LVGL_DIR)/{}" -o $$o || { echo "LVGL BUILD FAIL {}"; exit 255; }'
	@rm -f $(LVGL_OUT)/liblvgl.a && ar rcs $(LVGL_OUT)/liblvgl.a $(LVGL_OUT)/lib/*.o
	$(CC) -std=gnu99 -O1 -g -Wall -Wextra -Werror -Iinclude -DMCS_ENABLE_LVGL=1 '-DMCS_LVGL_API_INC="$(LVGL_INC)"' -DLV_CONF_INCLUDE_SIMPLE -Itests/lvgl/$(LVGL_API) -I$(LVGL_DIR) \
	  $(SRC) $(MOD_SRC) ports/unix/main.c ports/unix/mcs_lvgl_host.c $(LVGL_OUT)/liblvgl.a -lm -Wl,--gc-sections -o build/mcs_lvgl_$(LVGL_API)
	@cp build/mcs_lvgl_$(LVGL_API) build/mcs_lvgl
	@echo "built build/mcs_lvgl and build/mcs_lvgl_$(LVGL_API) (LVGL from $(LVGL_DIR), $(LVGL_API) config)"


# tests/lvgl/*.cs against their .out on the host display (mcs --lvgl); first line `// args: ...`, an optional
# second line `// only: v8` / `// only: v9`. Test files for the file-system tests come from tests/lvgl/make_assets.py.
lvgl-test: LVGL_TABLES = all
lvgl-test: mcs-lvgl
	@rm -rf build/lvglfs && python3 tests/lvgl/make_assets.py build/lvglfs --lvgl "$(LVGL_DIR)"
	@n=0; for t in tests/lvgl/*.cs; do o=$$(head -n 1 $$t | sed -n 's|^// args: *||p'); \
	  only=$$(sed -n '2s|^// only: *||p' $$t); if [ -n "$$only" ] && [ "$$only" != "$(LVGL_API)" ]; then echo "SKIP $$t (only $$only)"; continue; fi; \
	  ./build/mcs_lvgl_$(LVGL_API) --fs build/lvglfs $$o $$t > build/lvgl.txt 2>&1; \
	  cmp -s build/lvgl.txt $${t%.cs}.out && echo "PASS $$t ($(LVGL_API))" || { echo "FAIL $$t ($(LVGL_API))"; diff build/lvgl.txt $${t%.cs}.out | head -40; exit 1; }; n=$$((n+1)); done; echo "lvgl-test: $$n passed on LVGL $(LVGL_API)"
.PHONY: lvgl-api lvgl-test fetch-lvgl mcs-lvgl
