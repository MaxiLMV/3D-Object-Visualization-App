#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <glm.hpp>
#include <gtc/matrix_transform.hpp>
#include <gtc/type_ptr.hpp>
#include <iostream>
#include <vector>
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <tinyfiledialogs.h>
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

const unsigned int VIEWPORT_WIDTH = 800, VIEWPORT_HEIGHT = 800;
const unsigned int OBJECT_PROPERTIES_PANEL_WIDTH = 250, OBJECT_LIST_PANEL_WIDTH = 250, WINDOW_WIDTH = OBJECT_PROPERTIES_PANEL_WIDTH + OBJECT_LIST_PANEL_WIDTH + VIEWPORT_WIDTH;
const unsigned int WINDOW_HEIGHT = VIEWPORT_HEIGHT;

double lastMouseX, lastMouseY;
bool isCameraMoving = false, isTargetMoving = false, isRolling = false;
glm::mat4 projection;

float normalSpeed = 0.1f;
float fastSpeed = 0.5f;
bool isConstrained = false;
glm::vec3 movementBoundsMin(-50.0f, -50.0f, -50.0f);
glm::vec3 movementBoundsMax(50.0f, 50.0f, 50.0f);

glm::vec3 cameraTarget(0.0f), cameraPos(0.0f, 0.0f, 5.0f), cameraUp(0.0f, 1.0f, 0.0f), cameraFront = glm::normalize(cameraTarget - cameraPos);
float cameraYaw = -90.0f, cameraPitch = 0.0f;

GLuint gridVAO, gridVBO, VAO, VBO, EBO;
std::vector<float> gridVertices;

struct Object {
    glm::vec3 position, rotation, scale;
    Object(glm::vec3 pos = glm::vec3(0.0f), glm::vec3 rot = glm::vec3(0.0f), glm::vec3 scl = glm::vec3(1.0f))
        : position(pos), rotation(rot), scale(scl) {}
};

struct ImportedObject {
    GLuint VAO, VBO, EBO;
    int indexCount;
    glm::vec3 position, rotation, scale;
    ImportedObject()
        : position(0.0f), rotation(0.0f), scale(1.0f), VAO(0), VBO(0), EBO(0), indexCount(0) {}
};

struct SelectedObject {
    enum Type {
        NONE,
        IMPORTED_OBJECT
    } type;

    int index;

    SelectedObject() : type(NONE), index(-1) {}

    void clear() {
        type = NONE;
        index = -1;
    }

    bool isSelected() const {
        return type != NONE && index >= 0;
    }
};

std::vector<Object> objects;
std::vector<ImportedObject> importedObjects;
SelectedObject selectedObject;
int selectedObjectIndex = -1;

GLFWwindow* initGLFW() {
    if (!glfwInit()) return nullptr;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

    GLFWwindow* window = glfwCreateWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "Computer Graphics Assignment 2", nullptr, nullptr);
    if (!window) {
        std::cerr << "Failed to create GLFW window!" << std::endl;
        glfwTerminate();
        return nullptr;
    }
    glfwMakeContextCurrent(window);
    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) return nullptr;

    const GLFWvidmode* mode = glfwGetVideoMode(glfwGetPrimaryMonitor());
    glfwSetWindowPos(window, (mode->width - 1300) / 2, (mode->height - 800) / 2);

    glfwSwapInterval(1);
    return window;
}

bool intersectRayAABB(const glm::vec3& rayOrigin, const glm::vec3& rayDir, const glm::vec3& boxMin, const glm::vec3& boxMax, const glm::mat4& modelMatrix, float& t) {
    glm::vec3 transformedRayOrigin = glm::vec3(glm::inverse(modelMatrix) * glm::vec4(rayOrigin, 1.0f));
    glm::vec3 transformedRayDir = glm::normalize(glm::vec3(glm::inverse(modelMatrix) * glm::vec4(rayDir, 0.0f)));

    float tMin = (boxMin.x - transformedRayOrigin.x) / transformedRayDir.x;
    float tMax = (boxMax.x - transformedRayOrigin.x) / transformedRayDir.x;
    if (tMin > tMax) std::swap(tMin, tMax);

    float tyMin = (boxMin.y - transformedRayOrigin.y) / transformedRayDir.y;
    float tyMax = (boxMax.y - transformedRayOrigin.y) / transformedRayDir.y;
    if (tyMin > tyMax) std::swap(tyMin, tyMax);

    if ((tMin > tyMax) || (tyMin > tMax)) return false;
    if (tyMin > tMin) tMin = tyMin;
    if (tyMax < tMax) tMax = tyMax;

    float tzMin = (boxMin.z - transformedRayOrigin.z) / transformedRayDir.z;
    float tzMax = (boxMax.z - transformedRayOrigin.z) / transformedRayDir.z;
    if (tzMin > tzMax) std::swap(tzMin, tzMax);

    if ((tMin > tzMax) || (tzMin > tMax)) return false;
    if (tzMin > tMin) tMin = tzMin;
    if (tzMax < tMax) tMax = tzMax;

    t = tMin;
    return t >= 0.0f;
}

glm::vec3 getRayFromScreenCoords(double mouseX, double mouseY, int screenWidth, int screenHeight, const glm::mat4& projection, const glm::mat4& view) {

    float x = (2.0f * mouseX) / screenWidth - 1.0f;
    float y = 1.0f - (2.0f * mouseY) / screenHeight;
    glm::vec4 rayNDC(x, y, -1.0f, 1.0f);

    glm::vec4 rayEye = glm::inverse(projection) * rayNDC;
    rayEye.z = -1.0f;
    rayEye.w = 0.0f;

    glm::vec3 rayWorld = glm::vec3(glm::inverse(view) * rayEye);
    return glm::normalize(rayWorld);
}

class Renderer {
public:
    void render(const std::vector<ImportedObject>& objects, GLuint shaderProgram, const glm::mat4& view, const glm::mat4& projection) {
        glUseProgram(shaderProgram);

        for (const auto& obj : objects) {
            if (obj.indexCount == 0) continue;

            glm::mat4 model = glm::translate(glm::mat4(1.0f), obj.position);
            model = glm::rotate(model, glm::radians(obj.rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
            model = glm::rotate(model, glm::radians(obj.rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
            model = glm::rotate(model, glm::radians(obj.rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));
            model = glm::scale(model, obj.scale);

            glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "model"), 1, GL_FALSE, glm::value_ptr(model));
            glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "view"), 1, GL_FALSE, glm::value_ptr(view));
            glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "projection"), 1, GL_FALSE, glm::value_ptr(projection));

            glBindVertexArray(obj.VAO);
            glDrawElements(GL_TRIANGLES, obj.indexCount, GL_UNSIGNED_INT, nullptr);
        }

        glBindVertexArray(0);
    }
};

bool intersectRayTriangle(const glm::vec3& rayOrigin, const glm::vec3& rayDir,
    const glm::vec3& v0, const glm::vec3& v1, const glm::vec3& v2, float& t) {
    const float EPSILON = 1e-8f;
    glm::vec3 edge1 = v1 - v0;
    glm::vec3 edge2 = v2 - v0;
    glm::vec3 h = glm::cross(rayDir, edge2);
    float a = glm::dot(edge1, h);

    if (a > -EPSILON && a < EPSILON) return false;

    float f = 1.0f / a;
    glm::vec3 s = rayOrigin - v0;
    float u = f * glm::dot(s, h);

    if (u < 0.0f || u > 1.0f) return false;

    glm::vec3 q = glm::cross(s, edge1);
    float v = f * glm::dot(rayDir, q);

    if (v < 0.0f || u + v > 1.0f) return false;

    t = f * glm::dot(edge2, q);
    return t > EPSILON;
}

void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods) {
    ImGuiIO& io = ImGui::GetIO();
    io.AddMouseButtonEvent(button, action == GLFW_PRESS);

    if (io.WantCaptureMouse) return;

    if (action == GLFW_PRESS) {
        if (glfwGetKey(window, GLFW_KEY_R) == GLFW_PRESS && button == GLFW_MOUSE_BUTTON_LEFT) {
            isRolling = true;
            glfwGetCursorPos(window, &lastMouseX, &lastMouseY);
        }
        else if (glfwGetKey(window, GLFW_KEY_1) == GLFW_PRESS && button == GLFW_MOUSE_BUTTON_LEFT) {
            isTargetMoving = true;
            glfwGetCursorPos(window, &lastMouseX, &lastMouseY);
        }
        else if (glfwGetKey(window, GLFW_KEY_1) == GLFW_PRESS && button == GLFW_MOUSE_BUTTON_RIGHT) {
            isCameraMoving = true;
            glfwGetCursorPos(window, &lastMouseX, &lastMouseY);
        }
        if (button == GLFW_MOUSE_BUTTON_LEFT) {
            double mouseX, mouseY;
            glfwGetCursorPos(window, &mouseX, &mouseY);

            int screenWidth, screenHeight;
            glfwGetFramebufferSize(window, &screenWidth, &screenHeight);

            glm::mat4 view = glm::lookAt(cameraPos, cameraTarget, cameraUp);
            glm::vec3 rayOrigin = cameraPos;
            glm::vec3 rayDirection = getRayFromScreenCoords(mouseX, mouseY, screenWidth, screenHeight, projection, view);

            int closestObjectIndex = -1;
            float closestDistance = std::numeric_limits<float>::max();

            for (size_t i = 0; i < importedObjects.size(); ++i) {
                const auto& obj = importedObjects[i];
                float closestObjectT = std::numeric_limits<float>::max();

                glBindVertexArray(obj.VAO);
                glBindBuffer(GL_ARRAY_BUFFER, obj.VBO);
                glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, obj.EBO);

                std::vector<float> vertices(obj.indexCount * 3);
                std::vector<unsigned int> indices(obj.indexCount);

                glGetBufferSubData(GL_ARRAY_BUFFER, 0, vertices.size() * sizeof(float), vertices.data());
                glGetBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, indices.size() * sizeof(unsigned int), indices.data());

                glm::mat4 modelMatrix = glm::translate(glm::mat4(1.0f), obj.position) *
                    glm::rotate(glm::mat4(1.0f), glm::radians(obj.rotation.x), glm::vec3(1.0f, 0.0f, 0.0f)) *
                    glm::rotate(glm::mat4(1.0f), glm::radians(obj.rotation.y), glm::vec3(0.0f, 1.0f, 0.0f)) *
                    glm::rotate(glm::mat4(1.0f), glm::radians(obj.rotation.z), glm::vec3(0.0f, 0.0f, 1.0f)) *
                    glm::scale(glm::mat4(1.0f), obj.scale);

                for (size_t j = 0; j < indices.size(); j += 3) {
                    glm::vec3 v0 = glm::vec3(modelMatrix * glm::vec4(vertices[indices[j] * 3 + 0], vertices[indices[j] * 3 + 1], vertices[indices[j] * 3 + 2], 1.0f));
                    glm::vec3 v1 = glm::vec3(modelMatrix * glm::vec4(vertices[indices[j + 1] * 3 + 0], vertices[indices[j + 1] * 3 + 1], vertices[indices[j + 1] * 3 + 2], 1.0f));
                    glm::vec3 v2 = glm::vec3(modelMatrix * glm::vec4(vertices[indices[j + 2] * 3 + 0], vertices[indices[j + 2] * 3 + 1], vertices[indices[j + 2] * 3 + 2], 1.0f));

                    float t;
                    if (intersectRayTriangle(rayOrigin, rayDirection, v0, v1, v2, t)) {
                        if (t < closestObjectT) {
                            closestObjectT = t;
                        }
                    }
                }

                if (closestObjectT < closestDistance) {
                    closestDistance = closestObjectT;
                    closestObjectIndex = static_cast<int>(i);
                }
            }

            if (closestObjectIndex >= 0 && closestObjectIndex < static_cast<int>(importedObjects.size())) {
                selectedObject.type = SelectedObject::IMPORTED_OBJECT;
                selectedObject.index = closestObjectIndex;
                std::cout << "Selected Imported Object Index: " << selectedObject.index << std::endl;
            }
            else {
                selectedObject.clear();
            }
        }
    }

    if (action == GLFW_RELEASE) {
        if (button == GLFW_MOUSE_BUTTON_LEFT) {
            isRolling = false;
            isTargetMoving = false;
        }
        if (button == GLFW_MOUSE_BUTTON_RIGHT) {
            isCameraMoving = false;
        }
    }
}

void processInput(GLFWwindow* window) {
    double mouseX, mouseY;
    glfwGetCursorPos(window, &mouseX, &mouseY);
    float deltaX = static_cast<float>(mouseX - lastMouseX);
    float deltaY = static_cast<float>(mouseY - lastMouseY);
    lastMouseX = mouseX;
    lastMouseY = mouseY;

    float currentSpeed = (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) ? fastSpeed : normalSpeed;

    if (isCameraMoving) {
        const float sensitivity = 0.2f;
        cameraYaw += deltaX * sensitivity;
        cameraPitch -= deltaY * sensitivity;

        cameraPitch = glm::clamp(cameraPitch, -89.0f, 89.0f);

        cameraFront.x = cos(glm::radians(cameraYaw)) * cos(glm::radians(cameraPitch));
        cameraFront.y = sin(glm::radians(cameraPitch));
        cameraFront.z = sin(glm::radians(cameraYaw)) * cos(glm::radians(cameraPitch));
        cameraFront = glm::normalize(cameraFront);

        cameraTarget = cameraPos + cameraFront;
    }

    if (isTargetMoving) {
        const float panSpeed = 0.01f;

        glm::vec3 right = glm::normalize(glm::cross(cameraFront, cameraUp));
        glm::vec3 correctedUp = glm::normalize(glm::cross(right, cameraFront));

        glm::vec3 panOffset = (-right * deltaX + correctedUp * deltaY) * panSpeed;

        cameraPos += panOffset;
        cameraTarget += panOffset;

        cameraFront = glm::normalize(cameraTarget - cameraPos);
    }

    if (isRolling) {
        const float rollSpeed = 0.1f;

        glm::vec3 right = glm::normalize(glm::cross(cameraFront, cameraUp));
        cameraUp = glm::normalize(glm::rotate(glm::mat4(1.0f), glm::radians(deltaX * rollSpeed), cameraFront) * glm::vec4(cameraUp, 0.0f));

        glm::vec3 recalculatedRight = glm::normalize(glm::cross(cameraFront, cameraUp));
        cameraUp = glm::normalize(glm::cross(recalculatedRight, cameraFront));
    }

    glm::vec3 right = glm::normalize(glm::cross(cameraFront, cameraUp));
    glm::vec3 up = cameraUp;

    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) cameraPos += cameraFront * currentSpeed;  // Forward (W)
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) cameraPos -= cameraFront * currentSpeed;  // Backward (S)
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) cameraPos -= right * currentSpeed;        // Left (A)
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) cameraPos += right * currentSpeed;        // Right (D)
    if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS) cameraPos += up * currentSpeed;       // Up (Space)
    if (glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS) cameraPos -= up * currentSpeed; // Down (Ctrl)
    if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS) cameraPos -= up * currentSpeed;           // Up (Q)
    if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS) cameraPos += up * currentSpeed;           // Down (E)

    if (isConstrained) {
        cameraPos.x = glm::clamp(cameraPos.x, movementBoundsMin.x, movementBoundsMax.x);
        cameraPos.y = glm::clamp(cameraPos.y, movementBoundsMin.y, movementBoundsMax.y);
        cameraPos.z = glm::clamp(cameraPos.z, movementBoundsMin.z, movementBoundsMax.z);
    }

    cameraTarget = cameraPos + cameraFront;
}

void toggleConstraints(GLFWwindow* window) {
    if (glfwGetKey(window, GLFW_KEY_C) == GLFW_PRESS) {
        isConstrained = !isConstrained;
        std::cout << "Movement constraints " << (isConstrained ? "enabled" : "disabled") << std::endl;
    }
}

void scrollCallback(GLFWwindow* window, double xoffset, double yoffset) {
    ImGuiIO& io = ImGui::GetIO();
    io.AddMouseWheelEvent(static_cast<float>(xoffset), static_cast<float>(yoffset));
    const float zoomSpeed = 0.5f;
    glm::vec3 viewDirection = glm::normalize(cameraTarget - cameraPos);
    cameraPos += viewDirection * static_cast<float>(yoffset) * zoomSpeed;
    cameraTarget += viewDirection * static_cast<float>(yoffset) * zoomSpeed;
}

void setupGrid(float gridScale = 1.0f, int gridSize = 100) {
    gridVertices.clear();
    const float step = gridScale;

    for (int i = -gridSize; i <= gridSize; ++i) {
        gridVertices.insert(gridVertices.end(), { i * step, 0.0f, -gridSize * step, i * step, 0.0f, gridSize * step });
        gridVertices.insert(gridVertices.end(), { -gridSize * step, 0.0f, i * step, gridSize * step, 0.0f, i * step });
    }

    glGenVertexArrays(1, &gridVAO);
    glGenBuffers(1, &gridVBO);
    glBindVertexArray(gridVAO);
    glBindBuffer(GL_ARRAY_BUFFER, gridVBO);
    glBufferData(GL_ARRAY_BUFFER, gridVertices.size() * sizeof(float), gridVertices.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
}

void renderGrid(GLuint shaderProgram, const glm::mat4& view, const glm::mat4& projection) {
    glUseProgram(shaderProgram);

    glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "model"), 1, GL_FALSE, glm::value_ptr(glm::mat4(1.0f)));
    glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "view"), 1, GL_FALSE, glm::value_ptr(view));
    glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "projection"), 1, GL_FALSE, glm::value_ptr(projection));

    glUniform3f(glGetUniformLocation(shaderProgram, "mainLineColor"), 0.0f, 0.0f, 0.0f);
    glUniform3f(glGetUniformLocation(shaderProgram, "secondaryLineColor"), 0.5f, 0.5f, 0.5f);

    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(1.0f, 1.0f);

    glBindVertexArray(gridVAO);
    glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(gridVertices.size() / 3));

    glDisable(GL_POLYGON_OFFSET_FILL);
    glBindVertexArray(0);
}

void render(GLuint shaderProgram, const glm::mat4& view, const glm::mat4& projection) {
    glUseProgram(shaderProgram);

    for (const auto& obj : objects) {
        glm::mat4 model = glm::translate(glm::mat4(1.0f), obj.position);
        model = glm::rotate(model, glm::radians(obj.rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
        model = glm::rotate(model, glm::radians(obj.rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
        model = glm::rotate(model, glm::radians(obj.rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));
        model = glm::scale(model, obj.scale);

        glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "model"), 1, GL_FALSE, glm::value_ptr(model));
        glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "view"), 1, GL_FALSE, glm::value_ptr(view));
        glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "projection"), 1, GL_FALSE, glm::value_ptr(projection));

        glBindVertexArray(VAO);
        glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, nullptr);
        glBindVertexArray(0);
    }
}

void renderImportedObjects(GLuint shaderProgram, const glm::mat4& view, const glm::mat4& projection) {
    glUseProgram(shaderProgram);

    for (size_t i = 0; i < importedObjects.size(); ++i) {
        auto& obj = importedObjects[i];
        if (obj.indexCount == 0) continue;

        glm::mat4 model = glm::translate(glm::mat4(1.0f), obj.position);
        model = glm::rotate(model, glm::radians(obj.rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
        model = glm::rotate(model, glm::radians(obj.rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
        model = glm::rotate(model, glm::radians(obj.rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));
        model = glm::scale(model, obj.scale);

        glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "model"), 1, GL_FALSE, glm::value_ptr(model));
        glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "view"), 1, GL_FALSE, glm::value_ptr(view));
        glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "projection"), 1, GL_FALSE, glm::value_ptr(projection));

        glBindVertexArray(obj.VAO);
        glDrawElements(GL_TRIANGLES, obj.indexCount, GL_UNSIGNED_INT, nullptr);
    }

    glBindVertexArray(0);
}

const char* vertexShaderSource = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;

uniform mat4 model, view, projection;

out vec3 Normal, FragPos;

void main() {
    FragPos = vec3(model * vec4(aPos, 1.0));
    Normal = mat3(transpose(inverse(model))) * aNormal;
    gl_Position = projection * view * model * vec4(aPos, 1.0);
}
)";

const char* fragmentShaderSource = R"(
#version 330 core
out vec4 FragColor;

in vec3 Normal, FragPos;

uniform vec3 lightPos, viewPos, lightColor, objectColor;

void main() {
    vec3 norm = normalize(Normal);
    vec3 lightDir = normalize(lightPos - FragPos);

    vec3 ambient = 0.1 * lightColor;
    vec3 diffuse = max(dot(norm, lightDir), 0.0) * lightColor;

    vec3 viewDir = normalize(viewPos - FragPos);
    vec3 reflectDir = reflect(-lightDir, norm);
    float spec = pow(max(dot(viewDir, reflectDir), 0.0), 32);
    vec3 specular = 0.5 * spec * lightColor;

    vec3 result = (ambient + diffuse + specular) * objectColor;
    FragColor = vec4(result, 1.0);
}
)";

const char* gridVertexShaderSource = R"(
#version 330 core
layout (location = 0) in vec3 aPos;

uniform mat4 model, view, projection;

out vec3 fragPosition;

void main() {
    fragPosition = aPos;
    gl_Position = projection * view * model * vec4(aPos, 1.0);
}
)";

const char* gridFragmentShaderSource = R"(
#version 330 core
out vec4 FragColor;

in vec3 fragPosition;

uniform float gridScale;
uniform vec3 mainLineColor;
uniform vec3 secondaryLineColor;

void main() {
    float nearX = abs(fragPosition.x) < 0.01 ? 1.0 : 0.0;
    float nearZ = abs(fragPosition.z) < 0.01 ? 1.0 : 0.0;

    vec3 gridColor = mix(secondaryLineColor, mainLineColor, nearX + nearZ);

    FragColor = vec4(gridColor, 1.0);
}
)";

const char* outlineFragmentShaderSource = R"(
#version 330 core
out vec4 FragColor;

uniform vec3 outlineColor;

void main() {
    FragColor = vec4(outlineColor, 1.0);
}
)";

const char* outlineVertexShaderSource = R"(
#version 330 core
layout (location = 0) in vec3 aPos;

uniform mat4 model, view, projection;

void main() {
    float outlineScale = 1.02;
    gl_Position = projection * view * model * vec4(aPos * outlineScale, 1.0);
}
)";

GLuint createShaderProgram(const char* vShaderSrc, const char* fShaderSrc) {
    auto compileShader = [](GLenum type, const char* src) {
        GLuint shader = glCreateShader(type);
        glShaderSource(shader, 1, &src, nullptr);
        glCompileShader(shader);
        return shader;
        };

    GLuint vertexShader = compileShader(GL_VERTEX_SHADER, vShaderSrc);
    GLuint fragmentShader = compileShader(GL_FRAGMENT_SHADER, fShaderSrc);

    GLuint shaderProgram = glCreateProgram();
    glAttachShader(shaderProgram, vertexShader);
    glAttachShader(shaderProgram, fragmentShader);
    glLinkProgram(shaderProgram);

    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);
    return shaderProgram;
}

void importObject(const std::string& filePath) {
    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(filePath, aiProcess_Triangulate | aiProcess_GenNormals | aiProcess_FlipUVs);

    if (!scene || !scene->mRootNode || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE)) {
        std::cerr << "Error loading model: " << importer.GetErrorString() << std::endl;
        return;
    }

    for (unsigned int meshIndex = 0; meshIndex < scene->mNumMeshes; ++meshIndex) {
        aiMesh* mesh = scene->mMeshes[meshIndex];
        std::vector<float> vertices(mesh->mNumVertices * 6);
        std::vector<unsigned int> indices;

        for (unsigned int i = 0; i < mesh->mNumVertices; ++i) {
            vertices[i * 6 + 0] = mesh->mVertices[i].x;
            vertices[i * 6 + 1] = mesh->mVertices[i].y;
            vertices[i * 6 + 2] = mesh->mVertices[i].z;

            if (mesh->HasNormals()) {
                vertices[i * 6 + 3] = mesh->mNormals[i].x;
                vertices[i * 6 + 4] = mesh->mNormals[i].y;
                vertices[i * 6 + 5] = mesh->mNormals[i].z;
            }
            else {
                vertices[i * 6 + 3] = vertices[i * 6 + 4] = vertices[i * 6 + 5] = 0.0f;
            }
        }

        for (unsigned int i = 0; i < mesh->mNumFaces; ++i) {
            aiFace face = mesh->mFaces[i];
            for (unsigned int j = 0; j < face.mNumIndices; ++j) {
                indices.push_back(face.mIndices[j]);
            }
        }

        ImportedObject newObject;
        glGenVertexArrays(1, &newObject.VAO);
        glGenBuffers(1, &newObject.VBO);
        glGenBuffers(1, &newObject.EBO);

        glBindVertexArray(newObject.VAO);
        glBindBuffer(GL_ARRAY_BUFFER, newObject.VBO);
        glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_STATIC_DRAW);

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, newObject.EBO);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), indices.data(), GL_STATIC_DRAW);

        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
        glEnableVertexAttribArray(1);

        newObject.indexCount = static_cast<int>(indices.size());
        importedObjects.push_back(newObject);
    }

    std::cout << "Imported " << scene->mNumMeshes << " mesh(es) from " << filePath << std::endl;
}

void openImportDialog() {
    const char* filters[] = { "*.obj", "*.fbx", "*.gltf", "*.dae" };
    const char* filePath = tinyfd_openFileDialog(
        "Select Model File",
        "",
        4, filters,
        "Supported Model Files",
        0
    );

    if (filePath) {
        importObject(filePath);
    }
}

void exportSelectedObject() {
    if (selectedObject.isSelected()) {
        const auto& obj = importedObjects[selectedObject.index];
        const char* savePath = tinyfd_saveFileDialog(
            "Export Selected Object",
            "",
            0, nullptr,
            "Save as"
        );

        if (savePath) {
            std::cout << "Exporting object to " << savePath << std::endl;
        }
    }
    else {
        std::cout << "No object selected for export." << std::endl;
    }
}

void renderOutline(const ImportedObject& obj, GLuint outlineShader, const glm::mat4& view, const glm::mat4& projection) {
    glEnable(GL_STENCIL_TEST);
    glStencilMask(0x00);
    glStencilFunc(GL_NOTEQUAL, 1, 0xFF);
    glDisable(GL_DEPTH_TEST);

    glUseProgram(outlineShader);

    glm::mat4 model = glm::translate(glm::mat4(1.0f), obj.position);
    model = glm::rotate(model, glm::radians(obj.rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
    model = glm::rotate(model, glm::radians(obj.rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
    model = glm::rotate(model, glm::radians(obj.rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));
    model = glm::scale(model, obj.scale * 0.985f);

    glUniformMatrix4fv(glGetUniformLocation(outlineShader, "model"), 1, GL_FALSE, glm::value_ptr(model));
    glUniformMatrix4fv(glGetUniformLocation(outlineShader, "view"), 1, GL_FALSE, glm::value_ptr(view));
    glUniformMatrix4fv(glGetUniformLocation(outlineShader, "projection"), 1, GL_FALSE, glm::value_ptr(projection));
    glUniform3f(glGetUniformLocation(outlineShader, "outlineColor"), 1.0f, 1.0f, 0.0f);

    glBindVertexArray(obj.VAO);
    glDrawElements(GL_TRIANGLES, obj.indexCount, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);

    glEnable(GL_DEPTH_TEST);
    glStencilMask(0xFF);
    glDisable(GL_STENCIL_TEST);
}

float calculateLOD(const glm::vec3& cameraPos, float baseScale = 1.0f, float maxScale = 10.0f) {
    float distance = glm::length(cameraPos);
    return glm::clamp(baseScale * (distance / 10.0f), baseScale, maxScale);
}

bool snapToGrid = false;

void renderSelectedObjectPanel() {
    ImGui::SetNextWindowPos({ 0, 0 });
    ImGui::SetNextWindowSize({ OBJECT_PROPERTIES_PANEL_WIDTH, WINDOW_HEIGHT });
    ImGui::Begin("Object Properties", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);

    if (selectedObject.isSelected() && selectedObject.type == SelectedObject::IMPORTED_OBJECT &&
        selectedObject.index >= 0 && selectedObject.index < static_cast<int>(importedObjects.size())) {

        auto& obj = importedObjects[selectedObject.index];

        if (ImGui::CollapsingHeader("Position")) {
            ImGui::DragFloat3("Position", &obj.position.x, 0.1f, -100.0f, 100.0f);
            if (ImGui::Button("Reset Position")) {
                obj.position = glm::vec3(0.0f);
            }
        }

        if (ImGui::CollapsingHeader("Rotation")) {
            ImGui::DragFloat("Rotate X", &obj.rotation.x, 0.1f, -FLT_MAX, FLT_MAX);
            ImGui::DragFloat("Rotate Y", &obj.rotation.y, 0.1f, -FLT_MAX, FLT_MAX);
            ImGui::DragFloat("Rotate Z", &obj.rotation.z, 0.1f, -FLT_MAX, FLT_MAX);
            if (ImGui::Button("Reset Rotation")) {
                obj.rotation = glm::vec3(0.0f);
            }
        }

        if (ImGui::CollapsingHeader("Scale")) {
            ImGui::DragFloat3("Scale", &obj.scale.x, 0.1f, 0.1f, 100.0f);
            if (ImGui::Button("Reset Scale")) {
                obj.scale = glm::vec3(1.0f);
            }
        }
    }
    else {
        ImGui::Text("No object selected.");
    }

    ImGui::End();
}

void renderObjectListPanel() {
    ImGui::SetNextWindowPos({ OBJECT_PROPERTIES_PANEL_WIDTH + VIEWPORT_WIDTH, 0 });
    ImGui::SetNextWindowSize({ OBJECT_LIST_PANEL_WIDTH, WINDOW_HEIGHT });
    ImGui::Begin("Objects in Scene", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);

    if (ImGui::Button("Import")) {
        openImportDialog();
    }
    ImGui::SameLine();
    if (ImGui::Button("Export")) {
        exportSelectedObject();
    }

    static char searchFilter[64] = "";
    ImGui::InputText("Search", searchFilter, sizeof(searchFilter));
    ImGui::Separator();

    ImGui::Text("Scene Objects:");
    for (size_t i = 0; i < importedObjects.size(); ++i) {
        if (strstr(("Imported Object " + std::to_string(i)).c_str(), searchFilter)) {
            bool isSelected = (selectedObject.type == SelectedObject::IMPORTED_OBJECT && selectedObject.index == static_cast<int>(i));
            if (ImGui::Selectable(("Imported Object " + std::to_string(i)).c_str(), isSelected)) {
                if (selectedObject.index != static_cast<int>(i) || selectedObject.type != SelectedObject::IMPORTED_OBJECT) {
                    selectedObject.type = SelectedObject::IMPORTED_OBJECT;
                    selectedObject.index = static_cast<int>(i);
                    std::cout << "Selected Index: " << selectedObject.index << std::endl;
                }
            }
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                cameraTarget = importedObjects[i].position;
            }
        }
    }

    ImGui::End();
}

int main() {
    GLFWwindow* window = initGLFW();
    if (!window) return -1;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    GLuint cubeShader = createShaderProgram(vertexShaderSource, fragmentShaderSource);
    GLuint gridShader = createShaderProgram(gridVertexShaderSource, gridFragmentShaderSource);
    GLuint outlineShader = createShaderProgram(outlineVertexShaderSource, outlineFragmentShaderSource);

    glfwSetMouseButtonCallback(window, mouseButtonCallback);
    glfwSetScrollCallback(window, scrollCallback);

    float gridScale = calculateLOD(cameraPos);
    setupGrid(gridScale);

    glEnable(GL_DEPTH_TEST);
    glClearColor(0.25f, 0.25f, 0.25f, 1.0f);

    glEnable(GL_STENCIL_TEST);
    glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

    projection = glm::perspective(glm::radians(45.0f), 1.0f, 0.1f, 1000.0f);

    Renderer renderer;

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        renderSelectedObjectPanel();
        renderObjectListPanel();

        toggleConstraints(window);
        if (!ImGui::GetIO().WantCaptureMouse) {
            processInput(window);
        }

        glm::mat4 view = glm::lookAt(cameraPos, cameraTarget, cameraUp);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
        glViewport(OBJECT_PROPERTIES_PANEL_WIDTH, 0, VIEWPORT_WIDTH, VIEWPORT_HEIGHT);

        glUseProgram(gridShader);
        renderGrid(gridShader, view, projection);

        if (selectedObject.isSelected() && selectedObject.type == SelectedObject::IMPORTED_OBJECT) {
            const auto& obj = importedObjects[selectedObject.index];

            glEnable(GL_STENCIL_TEST);
            glStencilFunc(GL_ALWAYS, 1, 0xFF);
            glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
            glStencilMask(0xFF);
            glClear(GL_STENCIL_BUFFER_BIT);

            renderer.render({ obj }, cubeShader, view, projection);

            renderOutline(obj, outlineShader, view, projection);

            glDisable(GL_STENCIL_TEST);
        }

        glUseProgram(cubeShader);
        renderer.render(importedObjects, cubeShader, view, projection);

        if (selectedObject.isSelected() && selectedObject.type == SelectedObject::IMPORTED_OBJECT) {
            const auto& obj = importedObjects[selectedObject.index];
            renderOutline(obj, outlineShader, view, projection);
        }

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}