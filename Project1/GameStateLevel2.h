#ifndef GAME_STATE_LEVEL2
#define GAME_STATE_LEVEL2

#include <stdio.h>
#include <stdlib.h>

// Include GLEW
#include <GL/glew.h>

// Include GLFW
#include <glfw3.h>
extern GLFWwindow* window;

// Include GLM
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/random.hpp>

// ---------------------------------------------------------------------------

void GameStateLevel2Load(void);
void GameStateLevel2Init(void);
void GameStateLevel2Update(double dt, long frame, int &state);
void GameStateLevel2Draw(void);
void GameStateLevel2Free(void);
void GameStateLevel2Unload(void);

// ---------------------------------------------------------------------------

#endif // GAME_STATE_LEVEL2