#include"Graphics/vertexBuffer.hpp"


//the constructor should encapsulate
// glGenBuffers(...)
// glBindBuffer(...)
// glBufferData(...)

//for doing glBufferData it requires data, size, usage
// for eg
// glBufferData(
//     GL_ARRAY_BUFFER,  //target GLenum
//     sizeof(vertices), //size GLsizeiptr
//     vertices, //const GLvoid* //pointer to the source data
//     GL_STATIC_DRAW //usage //GLenum
// );
VertexBuffer::VertexBuffer(GLenum target, GLsizeiptr size, const GLvoid* data, GLenum usage):
    vbo(0), target(target){
    glGenBuffers(1,&vbo);
    glBindBuffer(target, vbo);
    glBufferData(
        target,
        size,
        data,
        usage
    );
}

VertexBuffer::~VertexBuffer(){
    glDeleteBuffers(1,&vbo);
}

void VertexBuffer::Bind(){
    glBindBuffer(target, vbo);
    //here we are explicitly saying that make my VBO the buffer that is currently bound to this target
}

void VertexBuffer::Unbind(){
    //openGL does not have inbuilt function to unbind buffer to a target, so we just bind the target to 0, 0 represents that no buffer object is currently bound to that target.
    glBindBuffer(target, 0);
}