/*
 * This software is licensed under the terms of the MIT-License
 * See COPYING for further information.
 * ---
 * Copyright (c) 2011-2018, Lukas Weber <laochailan@web.de>.
 * Copyright (c) 2012-2018, Andrei Alexeyev <akari@alienslab.net>.
 */

#include "taisei.h"

#include <png.h>

#include "texture.h"
#include "resource.h"
#include "global.h"
#include "vbo.h"

char* texture_path(const char *name) {
	return strjoin(TEX_PATH_PREFIX, name, TEX_EXTENSION, NULL);
}

bool check_texture_path(const char *path) {
	return strendswith(path, TEX_EXTENSION);
}

typedef struct ImageData {
	int width;
	int height;
	int depth;
	uint32_t *pixels;
} ImageData;

static ImageData* load_png(const char *filename);

void* load_texture_begin(const char *path, unsigned int flags) {
	return load_png(path);
}

#if SDL_BYTEORDER == SDL_BIG_ENDIAN
	#define A_OFS 0
	#define B_OFS 8
	#define G_OFS 16
	#define R_OFS 24
#else
	#define A_OFS 24
	#define B_OFS 16
	#define G_OFS 8
	#define R_OFS 0
#endif

#define CLRVAL(byte,clr) ((uint_fast32_t)(byte) << clr##_OFS)
#define CLRGETVAL(src,clr) (((src) >> clr##_OFS) & 0xff)
#define CLRMASK(clr) CLRVAL(0xff, clr)
#define CLRPACK(r,g,b,a) (CLRVAL(r, R) | CLRVAL(g, G) | CLRVAL(b, B) | CLRVAL(a, A))
#define CLRUNPACK(src,r,g,b,a) do { \
	r = CLRGETVAL(src, R); \
	g = CLRGETVAL(src, G); \
	b = CLRGETVAL(src, B); \
	a = CLRGETVAL(src, A); \
} while(0)

#include "video.h"

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
	SDL_Surface *surface;
	ImageData *img = opaque;

	if(!img) {
		return NULL;
	}

	surface = SDL_CreateRGBSurfaceFrom(img->pixels, img->width, img->height, img->depth * 4, 0, CLRMASK(R), CLRMASK(G), CLRMASK(B), CLRMASK(A));

	if(!surface) {
		log_warn("SDL_CreateRGBSurfaceFrom(): failed: %s", SDL_GetError());
		free(img);
		return NULL;
	}

	Texture *texture = malloc(sizeof(Texture));

	load_sdl_surf(surface, texture);
	free(surface->pixels);
	SDL_FreeSurface(surface);
	free(img);

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

static ImageData* load_png_p(const char *filename, SDL_RWops *rwops) {
#define PNGFAIL(msg) { log_warn("Couldn't load '%s': %s", filename, msg); return NULL; }
	png_structp png_ptr;
	if(!(png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL))) {
		PNGFAIL("png_create_read_struct() failed")
	}

	png_setup_error_handlers(png_ptr);

	png_infop info_ptr;
	if(!(info_ptr = png_create_info_struct(png_ptr))) {
		png_destroy_read_struct(&png_ptr, NULL, NULL);
		PNGFAIL("png_create_info_struct() failed")
	}

	png_infop end_info;
	if(!(end_info = png_create_info_struct(png_ptr))) {
		png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
		PNGFAIL("png_create_info_struct() failed")
	}

	png_init_rwops_read(png_ptr, rwops);
	png_read_info(png_ptr, info_ptr);

	int colortype = png_get_color_type(png_ptr,info_ptr);

	if(colortype == PNG_COLOR_TYPE_PALETTE)
		png_set_expand(png_ptr);
	if (colortype == PNG_COLOR_TYPE_GRAY ||
			colortype == PNG_COLOR_TYPE_GRAY_ALPHA)
		png_set_gray_to_rgb(png_ptr);
	if(!(colortype & PNG_COLOR_MASK_ALPHA))
		png_set_filler(png_ptr, 0xff, PNG_FILLER_AFTER);

	png_read_update_info(png_ptr, info_ptr);

	int width = png_get_image_width(png_ptr, info_ptr);
	int height = png_get_image_height(png_ptr, info_ptr);
	int depth = png_get_bit_depth(png_ptr, info_ptr);

	png_bytep row_pointers[height];

	uint32_t *pixels = malloc(sizeof(uint32_t)*width*height);

	for(int i = 0; i < height; i++)
		row_pointers[i] = (png_bytep)(pixels+i*width);

	png_read_image(png_ptr, row_pointers);
	png_read_end(png_ptr, end_info);
	png_destroy_read_struct(&png_ptr, &info_ptr, &end_info);

	ImageData *img = malloc(sizeof(ImageData));
	img->width = width;
	img->height = height;
	img->depth = depth;
	img->pixels = pixels;

	return img;
#undef PNGFAIL
}

static ImageData* load_png(const char *filename) {
	SDL_RWops *rwops = vfs_open(filename, VFS_MODE_READ);

	if(!rwops) {
		log_warn("VFS error: %s", vfs_get_error());
		return NULL;
	}

	ImageData *img = load_png_p(filename, rwops);

	SDL_RWclose(rwops);
	return img;
}

void load_sdl_surf(SDL_Surface *surface, Texture *texture) {
	glGenTextures(1, &texture->gltex);
	glBindTexture(GL_TEXTURE_2D, texture->gltex);

	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	int nw = surface->w;
	int nh = surface->h;

	uint32_t *tex = calloc(sizeof(uint32_t), nw*nh);
	uint32_t clr;
	uint32_t x, y;

	for(y = 0; y < nh; y++) {
		for(x = 0; x < nw; x++) {
			if(y < surface->h && x < surface->w) {
				clr = ((uint32_t*)surface->pixels)[y*surface->w+x];
			} else {
				clr = '\0';
			}

			tex[y*nw+x] = clr;
		}
	}

	texture->w = surface->w;
	texture->h = surface->h;

	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, nw, nh, 0, GL_RGBA, GL_UNSIGNED_BYTE, tex);

	free(tex);
}

void free_texture(Texture *tex) {
	glDeleteTextures(1, &tex->gltex);
	free(tex);
}

static struct draw_texture_state {
	bool drawing;
	bool texture_matrix_tainteed;
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
		draw_texture_state.texture_matrix_tainteed = true;

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

	if(draw_texture_state.texture_matrix_tainteed) {
		draw_texture_state.texture_matrix_tainteed = false;
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

	bool texture_matrix_tainteed = false;

	if(xoff || yoff || rw != 1 || rh != 1 || angle) {
		texture_matrix_tainteed = true;
		glMatrixMode(GL_TEXTURE);

		if(xoff || yoff) {
			glTranslatef(xoff,yoff, 0);
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

	if(texture_matrix_tainteed) {
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
