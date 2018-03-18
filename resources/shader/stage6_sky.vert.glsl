#version 330 core

layout(std140) uniform RenderContext {
	mat4 modelViewMatrix;
	mat4 projectionMatrix;
	mat4 textureMatrix;
	vec4 color;
} ctx;

layout(location=0) in vec3 position;
layout(location=2) in vec2 texCoordRawIn;

out vec3 posRaw;
out vec2 texCoord;

void main(void) {
	gl_Position = ctx.projectionMatrix * ctx.modelViewMatrix * vec4(position, 1.0);
	posRaw = position;
	texCoord = (ctx.textureMatrix * vec4(texCoordRawIn, 0.0, 1.0)).xy;
	
}
