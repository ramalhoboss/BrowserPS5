/* Copyright (C) 2026 BrowserPS5 contributors

This program is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation; either version 3, or (at your option) any
later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; see the file COPYING. If not, see
<http://www.gnu.org/licenses/>.  */

// gl_soft PoC (risk #2, the CPU path in docs/01-inventario-port-webkit.md).
//
// The console's GPU is closed to homebrew (GLSlim refuses payloads, AGC
// does not start), so WebKit's compositor (TextureMapper, which needs
// OpenGL ES 2) gets its GL from Mesa drawing on the CPU. This payload
// checks that, as a plain payload, the way the WebProcess composites:
//   1. EGL on Mesa's surfaceless platform (no window: WebKit draws to FBOs)
//   2. an OpenGL ES context: renderer, version, the extensions it has
//   3. shaders compiled at runtime and a 1920x1080 framebuffer object
//   4. a frame the way a compositor draws one: layers blended over a clear,
//      read back with glReadPixels, and pixels checked
//   5. how long that takes: uploading a full-screen tile, drawing a frame,
//      and reading it back (what the UIProcess would show)
//
// Built with -DGL_ZINK (../gl_zink), the same checks run on the console's
// GPU: Mesa's zink over RADV, drawing through the owner title
// (../gpu_broker); step 2 then also checks that zink is the renderer.

#ifdef GL_ZINK
#define POC_NAME "gl_zink"
#else
#define POC_NAME "gl_soft"
#endif

#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>

#include "poc_log.h"
#ifdef SHOW_ON_TV
#include "video_out.h"
#endif

#define WIDTH      1920
#define HEIGHT     1080
#define FRAMES     30
#define TILE       512
#define TV_SECONDS 10

#define RGBA(r, g, b, a) ((uint32_t)(a) << 24 | (b) << 16 | (g) << 8 | (r))


static const char* VERTEX_SHADER =
  "attribute vec2 pos;\n"
  "uniform vec4 rect;\n"            // x, y, width, height in clip space
  "varying vec2 uv;\n"
  "void main() {\n"
  "  uv = pos;\n"
  "  gl_Position = vec4(rect.xy + pos * rect.zw, 0.0, 1.0);\n"
  "}\n";

static const char* FRAGMENT_SHADER =
  "precision mediump float;\n"
  "uniform sampler2D tex;\n"
  "uniform float opacity;\n"
  "varying vec2 uv;\n"
  "void main() {\n"
  "  gl_FragColor = texture2D(tex, uv) * opacity;\n"  // premultiplied alpha
  "}\n";

// Extensions TextureMapper and WebKit's GL code look for.
static const char* WANTED_EXTENSIONS[] = {
  "GL_OES_vertex_array_object", "GL_EXT_texture_format_BGRA8888",
  "GL_OES_packed_depth_stencil", "GL_OES_texture_npot",
  "GL_EXT_unpack_subimage", "GL_OES_rgb8_rgba8", "GL_OES_depth24",
  "GL_EXT_map_buffer_range", "GL_OES_EGL_image",
};


typedef struct layer {
  GLuint tex;
  float x, y, w, h;   // pixels, origin at the bottom left
  float opacity;
} layer_t;


static GLuint
compile(GLenum type, const char* source) {
  GLuint shader = glCreateShader(type);
  char log[512] = {0};
  GLint ok = 0;

  glShaderSource(shader, 1, &source, 0);
  glCompileShader(shader);
  glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
  if(!ok) {
    glGetShaderInfoLog(shader, sizeof(log) - 1, 0, log);
    POC_LOG("shader compile error: %s", log);
    glDeleteShader(shader);
    return 0;
  }
  return shader;
}


static GLuint
solid_texture(int size, uint32_t color) {
  uint32_t* px = malloc(size * size * sizeof(uint32_t));
  GLuint tex;

  for(int i=0; i<size*size; i++) {
    px[i] = color;
  }
  glGenTextures(1, &tex);
  glBindTexture(GL_TEXTURE_2D, tex);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, size, size, 0, GL_RGBA,
	       GL_UNSIGNED_BYTE, px);
  free(px);
  return tex;
}


/**
 * A page-like tile: light background with darker horizontal stripes, so
 * texturing and filtering do real work.
 **/
static void
page_pixels(uint32_t* px, int w, int h, int seed) {
  for(int y=0; y<h; y++) {
    uint32_t c = ((y + seed) / 24) % 2 ? RGBA(240, 240, 240, 255) :
      RGBA(200, 210, 230, 255);

    for(int x=0; x<w; x++) {
      px[y * w + x] = c;
    }
  }
}


static void
draw_layers(GLuint prog, const layer_t* layers, int count) {
  GLint rect = glGetUniformLocation(prog, "rect");
  GLint opacity = glGetUniformLocation(prog, "opacity");

  glClearColor(0, 0, 1, 1);
  glClear(GL_COLOR_BUFFER_BIT);
  for(int i=0; i<count; i++) {
    const layer_t* l = &layers[i];

    glBindTexture(GL_TEXTURE_2D, l->tex);
    glUniform4f(rect, l->x / WIDTH * 2 - 1, l->y / HEIGHT * 2 - 1,
		l->w / WIDTH * 2, l->h / HEIGHT * 2);
    glUniform1f(opacity, l->opacity);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  }
}


#ifdef SHOW_ON_TV
/**
 * Step 6 (bigapp_soft.elf only, inside an app process): the same frames on
 * the TV for TV_SECONDS, the page scrolling and the pop-up sliding, read
 * back from GL and flipped with sceVideoOut, the way the UIProcess would
 * show what the WebProcess composited.
 **/
static void
show_on_tv(GLuint prog, layer_t* layers, GLuint page, uint32_t* px) {
  double gl_ms = 0, tile_ms = 0, wait_ms = 0, start, t0;
  int frames = 0, missed = 0, err = 0;
  vout_t vout;

  POC_NOTIFY("step 6: showing the composited frames for %d s, look at the "
	     "TV", TV_SECONDS);
  if((err=vout_open(&vout))) {
    POC_RESULT(0, "screen output: 0x%x", err);
    return;
  }

  start = poc_now_ms();
  while(poc_now_ms() - start < TV_SECONDS * 1000.0 && missed < 3) {
    int travel = WIDTH - (int)layers[3].w;
    int pos = (frames * 12) % (2 * travel);

    layers[3].x = pos < travel ? pos : 2 * travel - pos;
    t0 = poc_now_ms();
    page_pixels(px, WIDTH, HEIGHT, frames * 8);
    glBindTexture(GL_TEXTURE_2D, page);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, WIDTH, HEIGHT, GL_RGBA,
		    GL_UNSIGNED_BYTE, px);
    draw_layers(prog, layers, 4);
    glReadPixels(0, 0, WIDTH, HEIGHT, GL_RGBA, GL_UNSIGNED_BYTE, px);
    gl_ms += poc_now_ms() - t0;

    // GL's first row is the bottom one.
    if((err=vout_show(&vout, px + (HEIGHT - 1) * WIDTH, -WIDTH, &tile_ms,
		      &wait_ms)) > 1) {
      POC_RESULT(0, "sceVideoOutSubmitFlip(frame %d) = 0x%x", frames, err);
      break;
    }
    missed += err;
    frames++;
  }
  vout_close(&vout);

  if(frames) {
    double secs = (poc_now_ms() - start) / 1000.0;

    POC_NOTIFY("%s %d frames on the TV in %.1f s (%.1f fps), %d without a "
	       "flip event", missed ? "FAIL" : "PASS", frames, secs,
	       frames / secs, missed);
    POC_LOG("INFO per frame: GL (page upload, 4 layers, read back) %.2f ms, "
	    "linear->tiled copy %.2f ms, wait for flip %.2f ms",
	    gl_ms / frames, tile_ms / frames, wait_ms / frames);
    POC_LOG("Tell me what the TV showed. Expected: light page stripes "
	    "moving up, a dark bar across the top, a black box in the middle "
	    "and a white box sliding left and right over it.");
  }
}
#endif


static int
pixel_is(const uint32_t* px, int x, int y, int r, int g, int b) {
  uint32_t p = px[y * WIDTH + x];
  int pr = p & 0xff, pg = (p >> 8) & 0xff, pb = (p >> 16) & 0xff;
  int ok = abs(pr - r) <= 2 && abs(pg - g) <= 2 && abs(pb - b) <= 2;

  POC_LOG("  pixel (%4d, %4d) = %3d %3d %3d, expected %3d %3d %3d %s", x, y,
	  pr, pg, pb, r, g, b, ok ? "ok" : "WRONG");
  return ok;
}


int
main(void) {
  static const GLfloat quad[] = {0, 0, 1, 0, 0, 1, 1, 1};
  EGLint major = 0, minor = 0, count = 0, gles = 3;
  double upload_ms = 0, draw_ms = 0, read_ms = 0, t0;
  GLuint vs, fs, prog, fbo, target, page;
  layer_t layers[4];
  const char* exts;
  EGLContext ctx;
  EGLConfig cfg;
  EGLDisplay dpy;
  uint32_t* px;
  GLint linked;
  int ok;

  POC_LOG("started (pid %d)", getpid());

  POC_LOG("--- step 1: EGL on the surfaceless platform ---");
  dpy = eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA,
			      EGL_DEFAULT_DISPLAY, 0);
  if(dpy == EGL_NO_DISPLAY || !eglInitialize(dpy, &major, &minor)) {
    POC_RESULT(0, "EGL: eglGetPlatformDisplay/eglInitialize, error 0x%x",
	       eglGetError());
    return 1;
  }
  POC_RESULT(1, "EGL %d.%d, %s", major, minor,
	     eglQueryString(dpy, EGL_VENDOR));
  POC_LOG("EGL extensions: %s", eglQueryString(dpy, EGL_EXTENSIONS));

  POC_LOG("--- step 2: an OpenGL ES context ---");
  {
    // EGL_SURFACE_TYPE defaults to windows, and surfaceless has none.
    static const EGLint cfg_attr[] = {
      EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
      EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
      EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
      EGL_ALPHA_SIZE, 8, EGL_NONE
    };

    if(!eglBindAPI(EGL_OPENGL_ES_API)) {
      POC_RESULT(0, "eglBindAPI(EGL_OPENGL_ES_API), error 0x%x",
		 eglGetError());
      return 1;
    }
    if(!eglChooseConfig(dpy, cfg_attr, &cfg, 1, &count) || !count) {
      // EGL_KHR_no_config_context: a context for FBOs needs no config.
      POC_LOG("eglChooseConfig: %d configs (error 0x%x), using "
	      "EGL_NO_CONFIG_KHR", count, eglGetError());
      cfg = EGL_NO_CONFIG_KHR;
    }
  }
  // WebKit uses ES 3 when there is one, else ES 2.
  for(ctx=EGL_NO_CONTEXT; ctx == EGL_NO_CONTEXT && gles >= 2; gles--) {
    EGLint ctx_attr[] = {EGL_CONTEXT_MAJOR_VERSION, gles, EGL_NONE};

    ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctx_attr);
  }
  gles++;
  if(ctx == EGL_NO_CONTEXT ||
     !eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx)) {
    POC_RESULT(0, "eglCreateContext/eglMakeCurrent (no surface), error 0x%x",
	       eglGetError());
    return 1;
  }
  POC_RESULT(1, "OpenGL ES %d context without a surface", gles);
  POC_LOG("GL_RENDERER: %s", (const char*)glGetString(GL_RENDERER));
#ifdef GL_ZINK
  POC_RESULT(glGetString(GL_RENDERER) &&
	     strstr((const char*)glGetString(GL_RENDERER), "zink"),
	     "zink is the renderer: the GPU draws, not the CPU");
#endif
  POC_LOG("GL_VERSION:  %s", (const char*)glGetString(GL_VERSION));
  POC_LOG("GLSL:        %s",
	  (const char*)glGetString(GL_SHADING_LANGUAGE_VERSION));
  exts = (const char*)glGetString(GL_EXTENSIONS);
  for(size_t i=0; i<sizeof(WANTED_EXTENSIONS)/sizeof(*WANTED_EXTENSIONS);
      i++) {
    POC_LOG("  %-32s %s", WANTED_EXTENSIONS[i],
	    exts && strstr(exts, WANTED_EXTENSIONS[i]) ? "yes" : "no");
  }

  POC_LOG("--- step 3: shaders and a %dx%d framebuffer object ---", WIDTH,
	  HEIGHT);
  vs = compile(GL_VERTEX_SHADER, VERTEX_SHADER);
  fs = compile(GL_FRAGMENT_SHADER, FRAGMENT_SHADER);
  prog = glCreateProgram();
  if(vs && fs) {
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
  }
  glBindAttribLocation(prog, 0, "pos");
  glLinkProgram(prog);
  glGetProgramiv(prog, GL_LINK_STATUS, &linked);
  POC_RESULT(vs && fs && linked, "shaders compiled and linked at runtime");
  if(!linked) {
    return 1;
  }

  glGenTextures(1, &target);
  glBindTexture(GL_TEXTURE_2D, target);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, WIDTH, HEIGHT, 0, GL_RGBA,
	       GL_UNSIGNED_BYTE, 0);
  glGenFramebuffers(1, &fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
			 target, 0);
  ok = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
  POC_RESULT(ok, "framebuffer object complete (0x%x)",
	     glCheckFramebufferStatus(GL_FRAMEBUFFER));
  if(!ok) {
    return 1;
  }
  glViewport(0, 0, WIDTH, HEIGHT);
  glUseProgram(prog);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, quad);
  glEnableVertexAttribArray(0);
  glEnable(GL_BLEND);
  glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

  POC_LOG("--- step 4: a compositor frame, checked pixel by pixel ---");
  // Opaque red on the left, half-transparent green on the right, the clear
  // colour (blue) in between.
  layers[0] = (layer_t){solid_texture(64, RGBA(255, 0, 0, 255)),
			100, 100, 600, 400, 1.0f};
  layers[1] = (layer_t){solid_texture(64, RGBA(0, 255, 0, 255)),
			1200, 100, 600, 400, 0.5f};
  draw_layers(prog, layers, 2);
  px = malloc(WIDTH * HEIGHT * sizeof(uint32_t));
  glReadPixels(0, 0, WIDTH, HEIGHT, GL_RGBA, GL_UNSIGNED_BYTE, px);
  ok = pixel_is(px, 400, 300, 255, 0, 0);
  ok &= pixel_is(px, 1500, 300, 0, 128, 127);
  ok &= pixel_is(px, 950, 300, 0, 0, 255);
  ok &= pixel_is(px, 400, 800, 0, 0, 255);
  POC_RESULT(ok && glGetError() == GL_NO_ERROR,
	     "blended layers read back with the right colours");

  POC_LOG("--- step 5: timings at %dx%d (%d frames) ---", WIDTH, HEIGHT,
	  FRAMES);
  // A frame like a scrolling page: one full-screen tile uploaded and drawn,
  // and three smaller layers (a header, a video-sized box, a pop-up) over it.
  glGenTextures(1, &page);
  glBindTexture(GL_TEXTURE_2D, page);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, WIDTH, HEIGHT, 0, GL_RGBA,
	       GL_UNSIGNED_BYTE, 0);
  layers[0] = (layer_t){page, 0, 0, WIDTH, HEIGHT, 1.0f};
  layers[1] = (layer_t){solid_texture(TILE, RGBA(30, 30, 30, 255)),
			0, HEIGHT - 120, WIDTH, 120, 1.0f};
  layers[2] = (layer_t){solid_texture(TILE, RGBA(0, 0, 0, 255)),
			320, 200, 1280, 720, 1.0f};
  layers[3] = (layer_t){solid_texture(TILE, RGBA(255, 255, 255, 255)),
			660, 340, 600, 400, 0.9f};

  for(int f=0; f<FRAMES; f++) {
    page_pixels(px, WIDTH, HEIGHT, f * 8);
    t0 = poc_now_ms();
    glBindTexture(GL_TEXTURE_2D, page);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, WIDTH, HEIGHT, GL_RGBA,
		    GL_UNSIGNED_BYTE, px);
    glFinish();
    upload_ms += poc_now_ms() - t0;

    t0 = poc_now_ms();
    draw_layers(prog, layers, 4);
    glFinish();
    draw_ms += poc_now_ms() - t0;

    t0 = poc_now_ms();
    glReadPixels(0, 0, WIDTH, HEIGHT, GL_RGBA, GL_UNSIGNED_BYTE, px);
    read_ms += poc_now_ms() - t0;
  }
  ok = glGetError() == GL_NO_ERROR;
  POC_RESULT(ok, "%d frames without GL errors", FRAMES);
  POC_LOG("INFO per frame: upload a full-screen tile %.2f ms, draw 4 layers "
	  "%.2f ms, read back %.2f ms; total %.2f ms (%.1f fps)",
	  upload_ms / FRAMES, draw_ms / FRAMES, read_ms / FRAMES,
	  (upload_ms + draw_ms + read_ms) / FRAMES,
	  1000.0 * FRAMES / (upload_ms + draw_ms + read_ms));

#ifdef SHOW_ON_TV
  show_on_tv(prog, layers, page, px);
#endif

  free(px);
  eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
  eglDestroyContext(dpy, ctx);
  eglTerminate(dpy);
  POC_LOG("done");
  return 0;
}
