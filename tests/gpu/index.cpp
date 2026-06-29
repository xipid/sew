#include <Languages/JS/WASI/WASI.hpp>
#include <stdio.h>

static GLFWwindow* g_window = nullptr;
static double last_x = -1.0;
static double last_y = -1.0;

void update_loop() {
    if (glfwWindowShouldClose(g_window)) {
        printf("Window close requested. Terminating GLFW...\n");
        fflush(stdout);
        glfwDestroyWindow(g_window);
        glfwTerminate();
        return;
    }

    // Process pending events
    glfwPollEvents();

    // Query cursor coordinates
    double xpos = 0.0, ypos = 0.0;
    glfwGetCursorPos(g_window, &xpos, &ypos);

    // Print to console only when the coordinates change
    if (xpos != last_x || ypos != last_y) {
        printf("Mouse Position: x = %.1f, y = %.1f\n", xpos, ypos);
        fflush(stdout); // Force immediate print to browser console
        last_x = xpos;
        last_y = ypos;
    }

    // Request next frame
    jsRequestAnimationFrame(update_loop);
}

// GLFW key callback
static void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    printf("C++ Key Event Received: key = %d, action = %d\n", key, action);
    fflush(stdout); // Force immediate print to browser console
    
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
        glfwSetWindowShouldClose(window, GLFW_TRUE);
    }
}


int main() {
    // Disable stdout buffering completely for WASI-libc
    setvbuf(stdout, NULL, _IONBF, 0);

    printf("Initializing GLFW...\n");
    fflush(stdout);

    if (!glfwInit()) {
        fprintf(stderr, "Failed to initialize GLFW\n");
        fflush(stderr);
        return -1;
    }

    printf("Creating GLFW window mapping to target canvas...\n");
    fflush(stdout);

    g_window = glfwCreateWindow(640, 480, "GLFW WASM Test Window", nullptr, nullptr);
    if (!g_window) {
        fprintf(stderr, "Failed to create GLFW window\n");
        fflush(stderr);
        glfwTerminate();
        return -1;
    }

    glfwSetKeyCallback(g_window, key_callback);

    printf("GLFW window set up. Starting frame loop...\n");
    fflush(stdout);

    update_loop();
    
    return 0;
}