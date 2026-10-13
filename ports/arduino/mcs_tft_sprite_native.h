/* Internal real-library adapter, not a replacement sprite implementation.
 * Inspected TFT_eSPI pin:16e37595040eac69cd628e4bffb56fc30cad6299.
 * Upstream createPalette writes after an unchecked calloc. Preallocate the
 * protected palette with its required allocator; actual upstream destructor
 * frees it even if subsequent createSprite fails. No vendor source rewrite. */
#ifndef MCS_TFT_SPRITE_NATIVE_H
#define MCS_TFT_SPRITE_NATIVE_H
#include <TFT_eSPI.h>
#include <stdlib.h>
class mcs_tft_safe_sprite : public TFT_eSprite {
  public:
    explicit mcs_tft_safe_sprite(TFT_eSPI *parent) : TFT_eSprite(parent) {}
    void *createChecked(int16_t width, int16_t height, int8_t depth) {
        if (created() || width < 1 || width > 4096 || height < 1 || height > 4096 ||
            (depth != 1 && depth != 4 && depth != 8 && depth != 16))
            return NULL;
        setColorDepth(depth);
        if (depth == 4 && !_colorMap) {
            _colorMap = (uint16_t *)calloc(16, sizeof(uint16_t));
            if (!_colorMap)
                return NULL;
        }
        void *pixels = createSprite(width, height, 1);
        if (pixels && depth == 4)
            createPalette(
                static_cast<const uint16_t *>(NULL)); // real default palette, no allocation
        return pixels;
    }
};
#endif
