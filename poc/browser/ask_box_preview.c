// On the build machine: ask_box.c's windows as pictures (PPM), over a grey
// page, to look at before the console draws them.
//   gcc -O2 -o ask_box_preview ask_box_preview.c ask_box.c title/plutovg/source/*.c \
//       -DPLUTOVG_BUILD -DPLUTOVG_BUILD_STATIC -Ititle/plutovg/include -lm
//   ./ask_box_preview <fonts folder> <out folder>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ask_box.h"

static void
write_ppm(const char* path, const ask_box_t* b) {
  const int W = b->width + 120, H = b->height + 120;
  unsigned char* rgb = (unsigned char*)malloc((size_t)W * H * 3);
  for(int y = 0; y < H; y++) {
    for(int x = 0; x < W; x++) {
      unsigned bg = ((x / 40 + y / 40) & 1) ? 0x5a : 0x4a, r = bg, g = bg, bl = bg + 10;
      const int bx = x - 60, by = y - 60;
      if(bx >= 0 && by >= 0 && bx < b->width && by < b->height) {
        const uint32_t p = b->pixels[(size_t)by * b->width + bx];
        const unsigned a = p >> 24;
        r = ((p >> 16) & 255) + r * (255 - a) / 255;
        g = ((p >> 8) & 255) + g * (255 - a) / 255;
        bl = (p & 255) + bl * (255 - a) / 255;
      }
      unsigned char* o = rgb + ((size_t)y * W + x) * 3;
      o[0] = (unsigned char)(r > 255 ? 255 : r);
      o[1] = (unsigned char)(g > 255 ? 255 : g);
      o[2] = (unsigned char)(bl > 255 ? 255 : bl);
    }
  }
  FILE* f = fopen(path, "wb");
  fprintf(f, "P6\n%d %d\n255\n", W, H);
  fwrite(rgb, 3, (size_t)W * H, f);
  fclose(f);
  free(rgb);
}

int
main(int argc, char** argv) {
  if(argc < 3) {
    return 2;
  }
  static const struct {
    const char* name;
    const char* title;
    const char* text;
    const char* value;
    const char* cancel;
    uint32_t accent;
  } cases[] = {
    {"alert", "example.com diz", "A sessão expirou. Volte a iniciar sessão para continuar.", 0, 0, 0x00e5ff},
    {"confirm", "web.whatsapp.com diz",
     "Tem a certeza de que quer apagar esta conversa?\nEsta ação não pode ser anulada e as mensagens serão removidas de todos os seus dispositivos ligados.",
     0, "Cancelar", 0xff2ed1},
    {"prompt", "en.wikipedia.org says", "Name of the new list:", "PlayStation 5 reading list", "Cancel", 0x2f6dff},
    {"long", "example.com diz",
     "Linha um de um texto muito comprido que serve para ver onde a janela corta o que não cabe. "
     "Supercalifragilisticexpialidociousumapalavramuitocompridasemespacosquenaocabenumalinhasozinhaedeveserpartida. "
     "Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod tempor incididunt ut labore et dolore magna aliqua. "
     "Ut enim ad minim veniam, quis nostrud exercitation ullamco laboris nisi ut aliquip ex ea commodo consequat. "
     "Duis aute irure dolor in reprehenderit in voluptate velit esse cillum dolore eu fugiat nulla pariatur. "
     "Excepteur sint occaecat cupidatat non proident, sunt in culpa qui officia deserunt mollit anim id est laborum. "
     "Mais uma frase para passar das nove linhas e aparecerem as reticências no fim.",
     0, "Cancelar", 0x00e5ff},
  };
  for(size_t i = 0; i < sizeof(cases) / sizeof(*cases); i++) {
    ask_box_t b;
    char path[600];
    if(ask_box_draw(&b, argv[1], cases[i].title, cases[i].text, cases[i].value, "OK", cases[i].cancel, cases[i].accent)) {
      printf("%s: not drawn\n", cases[i].name);
      return 1;
    }
    snprintf(path, sizeof(path), "%s/ask-%s.ppm", argv[2], cases[i].name);
    write_ppm(path, &b);
    printf("%s: %dx%d\n", cases[i].name, b.width, b.height);
    free(b.pixels);
  }
  return 0;
}
