#version 330 core

in vec2 vert_texcoord;

out vec4 frag_color;

uniform sampler2D Display;

void main()
{
	frag_color = vec4(vert_texcoord, 0.0, 1.0);
}