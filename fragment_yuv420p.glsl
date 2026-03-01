#version 330 core
in vec2 TexCoord;
out vec4 FragColor;

uniform sampler2D texY;
uniform sampler2D texU;
uniform sampler2D texV;

void main() {
    // 使用现代语法
    vec2 flippedTex = vec2(TexCoord.x, 1.0 - TexCoord.y);

    float y = texture(texY, flippedTex).r;
    float u = texture(texU, flippedTex).r - 0.5;
    float v = texture(texV, flippedTex).r - 0.5;

    // YUV转RGB
    float r = y + 1.402 * v;
    float g = y - 0.344 * u - 0.714 * v;
    float b = y + 1.772 * u;

    FragColor = vec4(r, g, b, 1.0);
}
