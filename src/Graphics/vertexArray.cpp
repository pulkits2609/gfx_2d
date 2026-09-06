#include"../include/Graphics/vertexArray.hpp"

VertexArray::VertexArray():vao(0){
    glGenVertexArrays(1, &vao);
}

VertexArray::~VertexArray(){
    glDeleteVertexArrays(1,&vao);
}

void VertexArray::Bind(){
    glBindVertexArray(vao);
}

void VertexArray::Unbind(){
    glBindVertexArray(0); //unbind the currently active vao
}

void VertexArray::AddAttribute(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void* pointer){
    //this will consist of glVertexAttribPointer and glEnableVertexAttribArray

    // glVertexAttribPointer(
    //     0, //attribute index
    //     2, //components (size)
    //     GL_FLOAT, // type
    //     GL_FALSE, //normalized ?
    //     2* sizeof(float), //stride
    //     0 // offset
    // );

    //index :
    //Which shader attribute to configure.
    //Must match the layout(location = N) in our vertex shader (or the value from glGetAttribLocation). 

    //size :
    //number of components per attribute

    //type : 
    //data type for each component

    //normalized : 
    //only matters for integer types

    //stride:
    //Byte offset from the start of one vertex's attribute to the start of the next vertex's same attribute.  0 = tightly packed (no interleaving).

    //offset
    //where inside each vertex does this attribute begin

    glVertexAttribPointer(
        index, size, type, normalized, stride, pointer
    );
    glEnableVertexAttribArray(index);
}