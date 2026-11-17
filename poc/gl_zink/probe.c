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

// gl_zink_probe: which of the CPU's data zink's draws see on the console's
// GPU. gl_zink's first run (2026-11-04) cleared and read back right, but its
// textured layers left nothing; there each draw used client-side vertices,
// uniforms and a texture at once. Here each draw adds one of them to a draw
// that needs none (positions from gl_VertexID, a constant colour), into a
// 64x64 framebuffer object, and its centre pixel is read back.
//
// ZINK_ENV ("NAME=VALUE;NAME=VALUE") is set before EGL starts: the Makefile
// builds one probe per zink setting to compare (descriptor mode, shader
// objects, synchronous submission, Gallium's thread).

#define POC_NAME "gl_zink_probe"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>

#include "poc_log.h"

#ifndef ZINK_ENV
#define ZINK_ENV ""
#endif

#define SIZE 64


static const char* VS_VERTEX_ID =
  "#version 300 es\n"
  "out vec2 uv;\n"
  "void main() {\n"
  "  vec2 p = vec2(float(gl_VertexID & 1), float(gl_VertexID >> 1));\n"
  "  uv = p;\n"
  "  gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);\n"
  "}\n";

static const char* VS_ATTRIBUTE =
  "#version 300 es\n"
  "in vec2 pos;\n"
  "void main() {\n"
  "  gl_Position = vec4(pos * 2.0 - 1.0, 0.0, 1.0);\n"
  "}\n";

static const char* FS_GREEN =
  "#version 300 es\n"
  "precision mediump float;\n"
  "out vec4 color;\n"
  "void main() {\n"
  "  color = vec4(0.0, 1.0, 0.0, 1.0);\n"
  "}\n";

static const char* FS_YELLOW =
  "#version 300 es\n"
  "precision mediump float;\n"
  "out vec4 color;\n"
  "void main() {\n"
  "  color = vec4(1.0, 1.0, 0.0, 1.0);\n"
  "}\n";

static const char* FS_UNIFORM =
  "#version 300 es\n"
  "precision mediump float;\n"
  "uniform vec4 tint;\n"
  "out vec4 color;\n"
  "void main() {\n"
  "  color = tint;\n"
  "}\n";

static const char* FS_TEXTURE =
  "#version 300 es\n"
  "precision mediump float;\n"
  "uniform sampler2D tex;\n"
  "in vec2 uv;\n"
  "out vec4 color;\n"
  "void main() {\n"
  "  color = texture(tex, uv);\n"
  "}\n";


static unsigned passed, failed;


static GLuint
shader(GLenum type, const char* source) {
  GLuint s = glCreateShader(type);
  char log[512] = {0};
  GLint ok = 0;

  glShaderSource(s, 1, &source, 0);
  glCompileShader(s);
  glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
  if(!ok) {
    glGetShaderInfoLog(s, sizeof(log) - 1, 0, log);
    POC_LOG("shader compile error: %s", log);
  }
  return s;
}


static GLuint
program(const char* vs, const char* fs) {
  GLuint p = glCreateProgram();
  char log[512] = {0};
  GLint ok = 0;

  glAttachShader(p, shader(GL_VERTEX_SHADER, vs));
  glAttachShader(p, shader(GL_FRAGMENT_SHADER, fs));
  glBindAttribLocation(p, 0, "pos");
  glLinkProgram(p);
  glGetProgramiv(p, GL_LINK_STATUS, &ok);
  if(!ok) {
    glGetProgramInfoLog(p, sizeof(log) - 1, 0, log);
    POC_LOG("program link error: %s", log);
  }
  return p;
}


static void
clear(void) {
  glClearColor(0, 0, 0, 1);
  glClear(GL_COLOR_BUFFER_BIT);
}


// The centre pixel of the bound framebuffer.
static void
check(const char* what, int r, int g, int b) {
  uint8_t px[4] = {0};
  GLenum err;
  int ok;

  glReadPixels(SIZE / 2, SIZE / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
  err = glGetError();
  ok = abs(px[0] - r) <= 2 && abs(px[1] - g) <= 2 && abs(px[2] - b) <= 2 &&
    err == GL_NO_ERROR;
  POC_RESULT(ok, "%-44s %3d %3d %3d, expected %3d %3d %3d (GL error 0x%x)",
	     what, px[0], px[1], px[2], r, g, b, err);
  if(ok) {
    passed++;
  } else {
    failed++;
  }
}


static GLuint
target(GLuint tex) {
  GLuint fbo;

  glGenFramebuffers(1, &fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
			 tex, 0);
  return fbo;
}


static GLuint
texture(const uint32_t* px) {
  GLuint tex;

  glGenTextures(1, &tex);
  glBindTexture(GL_TEXTURE_2D, tex);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, SIZE, SIZE, 0, GL_RGBA,
	       GL_UNSIGNED_BYTE, px);
  return tex;
}


static void
set_zink_env(void) {
  char settings[256];

  strncpy(settings, ZINK_ENV, sizeof(settings) - 1);
  settings[sizeof(settings) - 1] = 0;
  for(char* s=strtok(settings, ";"); s; s=strtok(0, ";")) {
    char* value = strchr(s, '=');

    if(value) {
      *value++ = 0;
      setenv(s, value, 1);
      POC_LOG("%s=%s", s, value);
    }
  }
}


int
main(void) {
  static const GLfloat quad[] = {0, 0, 1, 0, 0, 1, 1, 1};
  static uint32_t orange[SIZE * SIZE], blue[SIZE * SIZE];
  static const EGLint ctx_attr[] = {EGL_CONTEXT_MAJOR_VERSION, 3, EGL_NONE};
  GLuint fbo, fbo_tex, tex, vbo, green, yellow, tinted, sampled;
  EGLint major = 0, minor = 0;
  EGLContext ctx;
  EGLDisplay dpy;

  POC_LOG("started (pid %d), zink settings: \"%s\"", getpid(), ZINK_ENV);
  set_zink_env();

  dpy = eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA,
			      EGL_DEFAULT_DISPLAY, 0);
  if(dpy == EGL_NO_DISPLAY || !eglInitialize(dpy, &major, &minor) ||
     !eglBindAPI(EGL_OPENGL_ES_API)) {
    POC_RESULT(0, "EGL, error 0x%x", eglGetError());
    return 1;
  }
  ctx = eglCreateContext(dpy, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, ctx_attr);
  if(ctx == EGL_NO_CONTEXT ||
     !eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx)) {
    POC_RESULT(0, "OpenGL ES 3 context, error 0x%x", eglGetError());
    return 1;
  }
  POC_LOG("GL_RENDERER: %s", (const char*)glGetString(GL_RENDERER));

  for(int i=0; i<SIZE*SIZE; i++) {
    orange[i] = 0xff0080ff;  // RGBA 255 128 0 255
    blue[i] = 0xffff0000;    // RGBA 0 0 255 255
  }

  glGenTextures(1, &fbo_tex);
  glBindTexture(GL_TEXTURE_2D, fbo_tex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, SIZE, SIZE, 0, GL_RGBA,
	       GL_UNSIGNED_BYTE, 0);
  fbo = target(fbo_tex);
  glViewport(0, 0, SIZE, SIZE);

  green = program(VS_VERTEX_ID, FS_GREEN);
  yellow = program(VS_ATTRIBUTE, FS_YELLOW);
  tinted = program(VS_VERTEX_ID, FS_UNIFORM);
  sampled = program(VS_VERTEX_ID, FS_TEXTURE);

  glClearColor(10 / 255.0f, 20 / 255.0f, 30 / 255.0f, 1);
  glClear(GL_COLOR_BUFFER_BIT);
  check("clear", 10, 20, 30);

  clear();
  glUseProgram(green);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  check("draw, no CPU data (gl_VertexID, constant)", 0, 255, 0);

  clear();
  glUseProgram(tinted);
  glUniform4f(glGetUniformLocation(tinted, "tint"), 1, 0, 1, 1);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  check("draw with a uniform", 255, 0, 255);

  clear();
  glUseProgram(yellow);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, quad);
  glEnableVertexAttribArray(0);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  check("draw with client-side vertices", 255, 255, 0);

  clear();
  glGenBuffers(1, &vbo);
  glBindBuffer(GL_ARRAY_BUFFER, vbo);
  glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glDisableVertexAttribArray(0);
  check("draw with vertices in a buffer object", 255, 255, 0);

  tex = texture(orange);
  target(tex);
  check("texture uploaded, read through an FBO", 255, 128, 0);

  glBindFramebuffer(GL_FRAMEBUFFER, fbo);
  clear();
  glUseProgram(sampled);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, tex);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  check("draw sampling that texture", 255, 128, 0);

  glBindTexture(GL_TEXTURE_2D, tex);
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, SIZE, SIZE, GL_RGBA,
		  GL_UNSIGNED_BYTE, blue);
  target(tex);
  check("texture updated (glTexSubImage2D)", 0, 0, 255);

  POC_RESULT(!failed, "%u probes passed, %u failed", passed, failed);
  eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
  eglDestroyContext(dpy, ctx);
  eglTerminate(dpy);
  POC_LOG("done");
  return failed ? 1 : 0;
}
