#include"Core/application.hpp"

Application::Application():window(1200,1200,"GFX_2D"),
vbo(
    GL_ARRAY_BUFFER,
    sizeof(float)*6,
    nullptr, 
    GL_STATIC_DRAW
),
vao(),
shader(
    "assets/shaders/vertex.glsl",
    "assets/shaders/fragment.glsl"
),
renderer(){
    float vertices[] = {
        0.0f, 0.5f,
        -0.5f, -0.5f,
        0.5f, -0.5f
    };
    glBufferData(
        GL_ARRAY_BUFFER,
        sizeof(vertices),
        vertices,
        GL_STATIC_DRAW
    );

    vao.Bind();
    vbo.Bind();

    vao.AddAttribute(
        0,
        2,
        GL_FLOAT,
        GL_FALSE,
        2* sizeof(float),
        0
    );

    vao.Unbind();
    vbo.Unbind();

}
    

void Application::RunApplication(){
    GameLoop();
}

void Application::GameLoop(){
    while(!window.ShouldClose()){
        window.PollEvents();
        renderer.Clear(0.1f, 0.1f, 0.1f, 1.0f);

        shader.Bind();
        vao.Bind();

        renderer.Draw(GL_TRIANGLES, 0, 3);

        vao.Unbind();
        shader.Unbind();
        window.SwapBuffers();
    }
}