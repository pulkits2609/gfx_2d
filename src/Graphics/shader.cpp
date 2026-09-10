#include<Graphics/shader.hpp>
#include <glm/gtc/type_ptr.hpp>

Shader::Shader(const std::string& vertexFile, const std::string& fragmentFile):shaderProgram(0){
    std::string vertexData = readShader(vertexFile);
    std::string fragmentData = readShader(fragmentFile);

    //extracting shader source from file and converting it into required format

    GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
    GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);

    CompileShader(vertexShader, vertexData);
    CompileShader(fragmentShader, fragmentData);

    //after successfull shader compilation, create shader Program
    shaderProgram = glCreateProgram();
    LinkProgram(shaderProgram, vertexShader, fragmentShader);

    //deleting a shader object after it has successfully compiled and linked
    //does not delete the program data
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);
}

Shader::~Shader(){
    glDeleteProgram(shaderProgram);
}

std::string Shader::readShader(const std::string& shaderName){
    std::ifstream file(shaderName);
    if(!file.is_open()){
        throw std::runtime_error("Error Opening Shader File");
    }
    std::string result;
    std::string line;
    while(std::getline(file,line)){
        result+=line+"\n";
    }
    // file.close(); //this is completely optional because when file goes out of scope, it deletes automatically
    return result;
}

void Shader::CompileShader(GLuint shaderToCompile, const std::string& shaderData){
    //taking shader source from file and converting it into required format
    const GLchar* shaderString = shaderData.c_str();
    const GLint shaderLen = static_cast<GLint>(shaderData.size());

    // glShaderSource(
    //     shader, //GLuint
    //     count, //GLsizei
    //     string, //const GLchar*
    //     length //GLint
    // );

    glShaderSource(
        shaderToCompile, 1, &shaderString, &shaderLen
    );

    glCompileShader(shaderToCompile);
    GLint Success = 0;
    glGetShaderiv(
        shaderToCompile, GL_COMPILE_STATUS, &Success
    );
    if(!Success){
        //create infolog message to display error
        std::string error = "Error Compiling Shader : ";
        GLint logLength;
        glGetShaderiv(shaderToCompile, GL_INFO_LOG_LENGTH, &logLength);
        GLchar* infoLog = new GLchar[logLength+1]; //+1 for null terminator
        glGetShaderInfoLog(
            shaderToCompile, logLength, nullptr, infoLog
        );
        error+=infoLog;
        delete[] infoLog;
        throw std::runtime_error(error);
    }
    std::cout<<"Shader Compiled Successfully\n";
}

void Shader::LinkProgram(GLuint shaderProgram, GLuint vertexShader, GLuint fragmentShader){
    //program currently empty, waiting for shaders
    glAttachShader(shaderProgram, vertexShader);
    glAttachShader(shaderProgram, fragmentShader);

    glLinkProgram(shaderProgram);
    GLint Success = 0;
    glGetProgramiv(
        shaderProgram, GL_LINK_STATUS, &Success
    );

    if(!Success){
        std::string error = "Error Linking Shader Program : ";
        GLint logLength;
        glGetProgramiv(shaderProgram, GL_INFO_LOG_LENGTH, &logLength);
        GLchar* infoLog = new GLchar[logLength+1]; //+1 for null terminator
        glGetProgramInfoLog(
            shaderProgram, logLength, nullptr, infoLog
        );
        error+=infoLog;
        delete[] infoLog;
        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);
        throw std::runtime_error(error);        
    }
}

void Shader::Bind(){
    glUseProgram(shaderProgram);
}

void Shader::Unbind(){
    glUseProgram(0); //this will unbind the currently binded program
}

void Shader::SetMat4(const std::string& name, const glm::mat4& matrix){

    GLint location = glGetUniformLocation(shaderProgram, name.c_str());

    glUniformMatrix4fv(
        location,
        1,
        GL_FALSE,
        glm::value_ptr(matrix)
    );
}