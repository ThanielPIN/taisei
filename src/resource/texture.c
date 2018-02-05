/*
 * This software is licensed under the terms of the MIT-License
 * See COPYING for further information.
 * ---
 * Copyright (c) 2011-2018, Lukas Weber <laochailan@web.de>.
 * Copyright (c) 2012-2018, Andrei Alexeyev <akari@alienslab.net>.
 */

#include "taisei.h"

#include <SDL_image.h>

#include "texture.h"
#include "resource.h"
#include "global.h"
#include "vbo.h"
#include "video.h"

static const char *texture_image_exts[] = {
	// more are usable if you explicitly specify the source in a .tex file,
	// but these are the ones we officially support, and are tried in this
	// order for source auto-detection.
	".png",
	".jpg",
	".jpeg",
	NULL
};

static char* texture_image_path(const char *name) {
	char *p = NULL;

	for(const char **ext = texture_image_exts; *ext; ++ext) {
		if((p = try_path(TEX_PATH_PREFIX, name, *ext))) {
			return p;
		}
	}

	return NULL;
}

char* texture_path(const char *name) {
	char *p = NULL;

	if((p = try_path(TEX_PATH_PREFIX, name, TEX_EXTENSION))) {
		return p;
	}

	return texture_image_path(name);
}

bool check_texture_path(const char *path) {
	if(strendswith(path, TEX_EXTENSION)) {
		return true;
	}

	return strendswith_any(path, texture_image_exts);
}

typedef struct ImageData {
	int width;
	int height;
	int depth;
	uint32_t *pixels;
} ImageData;

void* load_texture_begin(const char *path, unsigned int flags) {
	const char *source = path;
	char *source_allocated = NULL;
	SDL_Surface *surf = NULL;
	SDL_RWops *srcrw = NULL;

	if(strendswith(path, TEX_EXTENSION)) {
		if(!parse_keyvalue_file_with_spec(path, (KVSpec[]) {
			{ "source", .out_str = &source_allocated },
			// TODO: more parameters, e.g. filtering, wrap modes, post-load shaders, mipmaps, compression, etc.
			{ NULL }
		})) {
			free(source_allocated);
			return NULL;
		}

		if(!source_allocated) {
			char *basename = resource_util_basename(TEX_PATH_PREFIX, path);
			source_allocated = texture_image_path(basename);

			if(!source_allocated) {
				log_warn("%s: couldn't infer source path from texture name", basename);
			} else {
				log_warn("%s: inferred source path from texture name: %s", basename, source_allocated);
			}

			free(basename);

			if(!source_allocated) {
				return NULL;
			}
		}

		source = source_allocated;
	}

	srcrw = vfs_open(source, VFS_MODE_READ | VFS_MODE_SEEKABLE);

	if(!srcrw) {
		log_warn("VFS error: %s", vfs_get_error());
		free(source_allocated);
		return NULL;
	}

	if(strendswith(source, ".tga")) {
		surf = IMG_LoadTGA_RW(srcrw);
	} else {
		surf = IMG_Load_RW(srcrw, false);
	}

	if(!surf) {
		log_warn("IMG_Load_RW failed: %s", IMG_GetError());
	}

	SDL_RWclose(srcrw);

	SDL_Surface *converted_surf = SDL_ConvertSurfaceFormat(surf, SDL_PIXELFORMAT_RGBA32, 0);
	SDL_FreeSurface(surf);

	if(!converted_surf) {
		log_warn("SDL_ConvertSurfaceFormat(): failed: %s", SDL_GetError());
		return NULL;
	}

	return converted_surf;
}

static void texture_post_load(Texture *tex) {
	// this is a bit hacky and not very efficient,
	// but it's still much faster than fixing up the texture on the CPU

	GLuint fbotex, fbo;
	Shader *sha = get_shader("texture_post_load");

	glEnable(GL_TEXTURE_2D);
	glDisable(GL_BLEND);
	glGenTextures(1, &fbotex);
	glBindTexture(GL_TEXTURE_2D, fbotex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, tex->w, tex->h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
	glGenFramebuffers(1, &fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fbotex, 0);
	glBindTexture(GL_TEXTURE_2D, tex->gltex);
	glUseProgram(sha->prog);
	glUniform1i(uniloc(sha, "width"), tex->w);
	glUniform1i(uniloc(sha, "height"), tex->h);
	glPushMatrix();
	glLoadIdentity();
	glViewport(0, 0, tex->w, tex->h);
	set_ortho_ex(tex->w, tex->h);
	glTranslatef(tex->w * 0.5, tex->h * 0.5, 0);
	glScalef(tex->w, tex->h, 1);
	glDrawArrays(GL_QUADS, 4, 4);
	glPopMatrix();
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glDeleteFramebuffers(1, &fbo);
	glDeleteTextures(1, &tex->gltex);
	glUseProgram(0);
	video_set_viewport();
	set_ortho();
	glEnable(GL_BLEND);
	tex->gltex = fbotex;
}

void* load_texture_end(void *opaque, const char *path, unsigned int flags) {
	SDL_Surface *surface = opaque;

	if(!surface) {
		return NULL;
	}

	Texture *texture = malloc(sizeof(Texture));

	load_sdl_surf(surface, texture);
	SDL_FreeSurface(surface);

	texture_post_load(texture);
	return texture;
}

Texture* get_tex(const char *name) {
	return get_resource(RES_TEXTURE, name, RESF_DEFAULT | RESF_UNSAFE)->texture;
}

Texture* prefix_get_tex(const char *name, const char *prefix) {
	char *full = strjoin(prefix, name, NULL);
	Texture *tex = get_tex(full);
	free(full);
	return tex;
}

void load_sdl_surf(SDL_Surface *surface, Texture *texture) {
	glGenTextures(1, &texture->gltex);
	glBindTexture(GL_TEXTURE_2D, texture->gltex);

	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	SDL_LockSurface(surface);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, surface->w, surface->h, 0, GL_RGBA, GL_UNSIGNED_BYTE, surface->pixels);
	SDL_UnlockSurface(surface);

	texture->w = surface->w;
	texture->h = surface->h;
}

void free_texture(Texture *tex) {
	glDeleteTextures(1, &tex->gltex);
	free(tex);
}

static struct draw_texture_state {
	bool drawing;
	bool texture_matrix_tainted;
} draw_texture_state;

void begin_draw_texture(FloatRect dest, FloatRect frag, Texture *tex) {
	if(draw_texture_state.drawing) {
		log_fatal("Already drawing. Did you forget to call end_draw_texture, or call me on the wrong thread?");
	}

	draw_texture_state.drawing = true;

	glBindTexture(GL_TEXTURE_2D, tex->gltex);
	glPushMatrix();

	float x = dest.x;
	float y = dest.y;
	float w = dest.w;
	float h = dest.h;

	float s = frag.w/tex->w;
	float t = frag.h/tex->h;

	if(s != 1 || t != 1 || frag.x || frag.y) {
		draw_texture_state.texture_matrix_tainted = true;

		glMatrixMode(GL_TEXTURE);
		glLoadIdentity();

		glScalef(1.0/tex->w, 1.0/tex->h, 1);

		if(frag.x || frag.y) {
			glTranslatef(frag.x, frag.y, 0);
		}

		if(s != 1 || t != 1) {
			glScalef(frag.w, frag.h, 1);
		}

		glMatrixMode(GL_MODELVIEW);
	}

	if(x || y) {
		glTranslatef(x, y, 0);
	}

	if(w != 1 || h != 1) {
		glScalef(w, h, 1);
	}
}

void end_draw_texture(void) {
	if(!draw_texture_state.drawing) {
		log_fatal("Not drawing. Did you forget to call begin_draw_texture, or call me on the wrong thread?");
	}

	if(draw_texture_state.texture_matrix_tainted) {
		draw_texture_state.texture_matrix_tainted = false;
		glMatrixMode(GL_TEXTURE);
		glLoadIdentity();
		glMatrixMode(GL_MODELVIEW);
	}

	glPopMatrix();
	draw_texture_state.drawing = false;
}

void draw_texture_p(float x, float y, Texture *tex) {
	begin_draw_texture((FloatRect){ x, y, tex->w, tex->h }, (FloatRect){ 0, 0, tex->w, tex->h }, tex);
	draw_quad();
	end_draw_texture();
}

void draw_texture(float x, float y, const char *name) {
	draw_texture_p(x, y, get_tex(name));
}

void draw_texture_with_size_p(float x, float y, float w, float h, Texture *tex) {
	begin_draw_texture((FloatRect){ x, y, w, h }, (FloatRect){ 0, 0, tex->w, tex->h }, tex);
	draw_quad();
	end_draw_texture();
}

void draw_texture_with_size(float x, float y, float w, float h, const char *name) {
	draw_texture_with_size_p(x, y, w, h, get_tex(name));
}

void fill_viewport(float xoff, float yoff, float ratio, const char *name) {
	fill_viewport_p(xoff, yoff, ratio, 1, 0, get_tex(name));
}

void fill_viewport_p(float xoff, float yoff, float ratio, float aspect, float angle, Texture *tex) {
	glBindTexture(GL_TEXTURE_2D, tex->gltex);

	float rw, rh;

	if(ratio == 0) {
		rw = aspect;
		rh = 1;
	} else {
		rw = ratio * aspect;
		rh = ratio;
	}

	bool texture_matrix_tainted = false;

	if(xoff || yoff || rw != 1 || rh != 1 || angle) {
		texture_matrix_tainted = true;
		glMatrixMode(GL_TEXTURE);

		if(xoff || yoff) {
			glTranslatef(xoff, yoff, 0);
		}

		if(rw != 1 || rh != 1) {
			glScalef(rw, rh, 1);
		}

		if(angle) {
			glTranslatef(0.5, 0.5, 0);
			glRotatef(angle, 0, 0, 1);
			glTranslatef(-0.5, -0.5, 0);
		}

		glMatrixMode(GL_MODELVIEW);
	}

	glPushMatrix();
	glTranslatef(VIEWPORT_W*0.5, VIEWPORT_H*0.5, 0);
	glScalef(VIEWPORT_W, VIEWPORT_H, 1);
	draw_quad();
	glPopMatrix();

	if(texture_matrix_tainted) {
		glMatrixMode(GL_TEXTURE);
		glLoadIdentity();
		glMatrixMode(GL_MODELVIEW);
	}
}

void fill_screen(const char *name) {
	fill_screen_p(get_tex(name));
}

void fill_screen_p(Texture *tex) {
	begin_draw_texture((FloatRect){ SCREEN_W*0.5, SCREEN_H*0.5, SCREEN_W, SCREEN_H }, (FloatRect){ 0, 0, tex->w, tex->h }, tex);
	draw_quad();
	end_draw_texture();
}

// draws a thin, w-width rectangle from point A to point B with a texture that
// moving along the line.
// As with fill_screen, the texture’s dimensions must be powers of two for the
// loop to be gapless.
//
void loop_tex_line_p(complex a, complex b, float w, float t, Texture *texture) {
	complex d = b-a;
	complex c = (b+a)/2;
	glPushMatrix();
	glTranslatef(creal(c),cimag(c),0);
	glRotatef(180/M_PI*carg(d),0,0,1);
	glScalef(cabs(d),w,1);

	glMatrixMode(GL_TEXTURE);
	glLoadIdentity();
	glTranslatef(t, 0, 0);
	glMatrixMode(GL_MODELVIEW);

	glBindTexture(GL_TEXTURE_2D, texture->gltex);

	draw_quad();

	glMatrixMode(GL_TEXTURE);
		glLoadIdentity();
	glMatrixMode(GL_MODELVIEW);

	glPopMatrix();
}

void loop_tex_line(complex a, complex b, float w, float t, const char *texture) {
	loop_tex_line_p(a, b, w, t, get_tex(texture));
}
