
#include "GameStateLevel1.h"
#include "CDT.h"
#include <iostream>
#include <fstream>
#include <string>
#include <irrKlang.h>
#include <cmath>
using namespace irrklang;

// -------------------------------------------
// Defines
// -------------------------------------------

#define MESH_MAX					32				// The total number of Mesh (Shape)
#define TEXTURE_MAX					32				// The total number of texture
#define GAME_OBJ_INST_MAX			1024			// The total number of different game object instances
#define PLAYER_INITIAL_NUM			3				// initial number of player lives
#define FLAG_INACTIVE				0
#define FLAG_ACTIVE					1
#define COOLDOWN				    1000				
#define ANIMATION_SPEED				60				// 1 = fastest (update every frame)
// Movement flags
#define GRAVITY						-37.0f
#define JUMP_VELOCITY				18.0f
#define MOVE_VELOCITY_PLAYER		5.0f
#define MOVE_VELOCITY_ENEMY			2.0f
#define ENEMY_IDLE_TIME				2.0
#define GROUND_LEVEL				1.5f			// the y cooridnate of the ground
// Collision flags
#define	COLLISION_LEFT				1<<0       //0001 = 1
#define	COLLISION_RIGHT				1<<1      // 0001 -> 0010 = 2
#define	COLLISION_TOP				1<<2       //0001 -> 0100 = 4
#define	COLLISION_BOTTOM			1<<3      // 0001 -> 1000 = 8


enum GAMEOBJ_TYPE
{
	// list of game object types
	TYPE_PLAYER = 0,
	TYPE_ENEMY,
	TYPE_ITEM
};

//State machine states
enum STATE
{
	STATE_NONE,
	STATE_GOING_LEFT,
	STATE_GOING_RIGHT
};

/*
//State machine inner states
enum INNER_STATE
{
INNER_STATE_ON_ENTER,
INNER_STATE_ON_UPDATE,
INNER_STATE_ON_EXIT
};
*/

// -------------------------------------------
// Structure definitions
// -------------------------------------------

struct GameObj
{
	CDTMesh* mesh;
	CDTTex* tex;
	int				type;				// enum type
	int				flag;				// 0 - inactive, 1 - active
	glm::vec3		position;			// coordinates are in Map space
	glm::vec3		velocity;			// usually we will use only x and y
	glm::vec3		scale;				// usually we will use only x and y
	float			orientation;		// 0 radians is 3 o'clock, PI/2 radian is 12 o'clock
	glm::mat4		modelMatrix;		// transform from model space [-0.5,0.5] to map space [0,MAP_SIZE]
	int				mapCollsionFlag;	// for testing collision detection with map
	bool			jumping;			// Is Player jumping or on the ground

	//animation data
	bool			mortal;
	//int			lifespan;			// in frame unit
	bool			anim;				// do animation?
	int				numFrame;			// #frame in texture animation
	int				currFrame;
	float			offset;				// offset value of each frame
	float			offsetX;			// assume single row sprite sheet
	float			offsetY;			// will be set to 0 for this single row implementation		


	//state machine data
	enum STATE			state;
	/*
	enum INNER_STATE	innerState;
	double				counter;		// use in state machine
	*/

};

struct AABB {
	float x;
	float y;
	float w;
	float h;
};


// -----------------------------------------------------
// Level variable, static - visible only in this file
// -----------------------------------------------------

static CDTMesh		sMeshArray[MESH_MAX];							// Store all unique shape/mesh in your game
static int			sNumMesh;
static CDTTex		sTexArray[TEXTURE_MAX];							// Corresponding texture of the mesh
static int			sNumTex;
static GameObj		sGameObjInstArray[GAME_OBJ_INST_MAX];			// Store all game object instance
static int			sNumGameObj;

// Player data
static GameObj* sPlayer;										// Pointer to the Player game object instance
static glm::vec3	sPlayer_start_position;
static int			sPlayerLives;									// The number of lives left
static int			sScore;
static int			sRespawnCountdown;								// Respawn player waiting time (in #frame)
static int			sMortalCountdown;


//Sound
ISoundEngine* SoundEngine;

// Map data
static int** sMapData;										// sMapData[Height][Width]
static int** sMapCollisionData;
static int			MAP_WIDTH;
static int			MAP_HEIGHT;
static glm::mat4	sMapMatrix;										// Transform from map space [0,MAP_SIZE] to screen space [-width/2,width/2]
static CDTMesh* sMapMesh;										// Mesh & Tex of the level, we only need 1 of these
static CDTTex* sMapTex;
static float		sMapOffset;

// Camera
static glm::vec2	sCamPosition(0.f, 0.f);
static int			VIEW_WIDTH = 20;
static int			VIEW_HEIGHT = 20;



// -----------------------------------------------------
// Map functions
// -----------------------------------------------------


int _detectCollisionAABB(AABB a, AABB b) {
	int result = 0;

	float dx = a.x - b.x; // Calculate the distance between the centers of the two boxes along the x-axis
	float dy = a.y - b.y; // Calculate the distance between the centers of the two boxes along the y-axis

	float combinedHalfWidths = a.w / 2 + b.w / 2; // Calculate the sum of the half-widths of the two boxes
	float combinedHalfHeights = a.h / 2 + b.h / 2; // Calculate the sum of the half-heights of the two boxes

	if (std::abs(dx) < combinedHalfWidths && std::abs(dy) < combinedHalfHeights) { // Check for collision
		float overlapX = combinedHalfWidths - std::abs(dx); // Calculate the overlap along the x-axis
		float overlapY = combinedHalfHeights - std::abs(dy); // Calculate the overlap along the y-axis

		// Determine the direction of the collision based on the overlap
		result |= dx > 0 ? 1 : 2;
		result |= dy > 0 ? 8 : 4;
	}

	return result;
}

//+ This fucntion return collison flags
//	- 1-left, 2-right, 4-top, 8-bottom 
//	- each side is checked with 2 hot spots
int CheckMapCollision(float PosX, float PosY, bool& inTheAir) {
	int result = 0;
	int mapCoorX = floor(PosX), mapCoorY = floor(MAP_HEIGHT - PosY);


	// Loop checking collision in pattern of
	//   x
	// x x x
	//   x
	for (size_t y = mapCoorY - 1; y <= mapCoorY + 1; y++)
	{
		if (y < 0 || y >= MAP_HEIGHT) continue;

		for (size_t x = mapCoorX - 1; x <= mapCoorX + 1; x++)
		{
			if (y != mapCoorY && x != mapCoorX) continue;
			if (x < 0 || x >= MAP_WIDTH || !sMapCollisionData[y][x]) continue;

			int _result = _detectCollisionAABB({ PosX, PosY , 1.f, 1.f }, { (float)x + .5f, MAP_HEIGHT - (float)y - .5f, 1.f, 1.f });

			// If check bottom/top collision, ignore x axis
			if (y == mapCoorY + 1 && _result & COLLISION_BOTTOM)
				result |= 8;
			else if (y == mapCoorY - 1 && _result & COLLISION_TOP)
				result |= 4;

			// If check right/left colliosn, ignore y axis
			else if (y == mapCoorY && _result & COLLISION_RIGHT)
				result |= 2;
			else if (y == mapCoorY && _result & COLLISION_LEFT)
				result |= 1;
			else
				result |= _result;


			//printf("Coor: %d %d\n", x, y);
			//printf("Pos: %f %f\nAnd: %f %f\nCollision: %d\n", PosX, PosY, x + .5f, MAP_HEIGHT - (float)y - .5f, result);

		}
	}

	// check if there is ground under player
	if (mapCoorY + 1 >= 0 && mapCoorY + 1 < MAP_HEIGHT && mapCoorX >= 0 && mapCoorX < MAP_WIDTH && !sMapCollisionData[mapCoorY + 1][mapCoorX])
		inTheAir = true;


	return result;
}




// -----------------------------------------------------
// State machine functions
// -----------------------------------------------------

void EnemyStateMachine(GameObj* pInst) {
	bool isInAir = false;

	// Update position, velocity, jumping states when the collide with the map
	pInst->mapCollsionFlag = CheckMapCollision(pInst->position.x, pInst->position.y, isInAir);

	// Collide Left
	if (pInst->mapCollsionFlag & COLLISION_LEFT) {
		pInst->position.x = (int)pInst->position.x + 0.5f;
		pInst->state = STATE_GOING_RIGHT;
	}

	//+ Collide Right
	if (pInst->mapCollsionFlag & COLLISION_RIGHT) {
		pInst->position.x = (int)pInst->position.x + 0.5f;
		pInst->state = STATE_GOING_LEFT;
	}


	//+ Collide Top
	if (pInst->mapCollsionFlag & COLLISION_TOP) {
		pInst->velocity.y = -0.5f;
		pInst->position.y = (int)pInst->position.y + 0.5f;
	}


	//+ Is on the ground or just landed on the ground
	if (pInst->mapCollsionFlag & COLLISION_BOTTOM) {
		pInst->jumping = false;
		pInst->velocity.y = 0;
		pInst->position.y = (int)pInst->position.y + 0.5f;
	}

	//+ Is jumping/falling
	if (isInAir) {
		pInst->jumping = true;
	}

	switch (pInst->state)
	{
	case STATE_GOING_LEFT:
		pInst->velocity.x = -MOVE_VELOCITY_ENEMY;
		break;
	case STATE_GOING_RIGHT:
		pInst->velocity.x = MOVE_VELOCITY_ENEMY;
		break;
	default:
		break;
	}
}


// -------------------------------------------
// Game object instant functions
// -------------------------------------------

// functions to create/destroy a game object instance
static GameObj* gameObjInstCreate(int type, glm::vec3 pos, glm::vec3 vel, glm::vec3 scale, float orient, bool anim, int numFrame, int currFrame, float offset);
static void			gameObjInstDestroy(GameObj& pInst);


GameObj* gameObjInstCreate(int type, glm::vec3 pos, glm::vec3 vel, glm::vec3 scale, float orient, bool anim, int numFrame, int currFrame, float offset)
{
	// loop through all object instance array to find the free slot
	for (int i = 0; i < GAME_OBJ_INST_MAX; i++) {
		GameObj* pInst = sGameObjInstArray + i;
		if (pInst->flag == FLAG_INACTIVE) {

			pInst->mesh = sMeshArray + type;
			pInst->tex = sTexArray + type;
			pInst->type = type;
			pInst->flag = FLAG_ACTIVE;
			pInst->position = pos;
			pInst->velocity = vel;
			pInst->scale = scale;
			pInst->orientation = orient;
			pInst->modelMatrix = glm::mat4(1.0f);
			pInst->mapCollsionFlag = 0;
			pInst->jumping = false;
			pInst->anim = anim;
			pInst->numFrame = numFrame;
			pInst->currFrame = currFrame;
			pInst->offset = offset;
			pInst->offsetX = 0;
			pInst->offsetY = 0;

			sNumGameObj++;
			return pInst;
		}
	}

	// Cannot find empty slot => return 0
	return NULL;
}

void gameObjInstDestroy(GameObj& pInst)
{
	// Lazy deletion, not really delete the object, just set it as inactive
	if (pInst.flag == FLAG_INACTIVE)
		return;

	sNumGameObj--;
	pInst.flag = FLAG_INACTIVE;
}


// -------------------------------------------
// Game states function
// -------------------------------------------

void GameStateLevel1Load(void) {

	// clear the Mesh array
	memset(sMeshArray, 0, sizeof(CDTMesh) * MESH_MAX);
	sNumMesh = 0;

	// clear the Texture array
	memset(sTexArray, 0, sizeof(CDTTex) * TEXTURE_MAX);
	sNumTex = 0;

	// clear the game object instance array
	memset(sGameObjInstArray, 0, sizeof(GameObj) * GAME_OBJ_INST_MAX);
	sNumGameObj = 0;

	// Set the Player object instance to NULL
	sPlayer = NULL;


	// --------------------------------------------------------------------------
	// Create all of the unique meshes/textures and put them in MeshArray/TexArray
	//		- The order of mesh MUST follow enum GAMEOBJ_TYPE 
	/// --------------------------------------------------------------------------

	// Temporary variable for creating mesh
	CDTMesh* pMesh;
	CDTTex* pTex;
	std::vector<CDTVertex> vertices;
	CDTVertex v1, v2, v3, v4;

	// Create Player mesh/texture
	vertices.clear();
	v1.x = -0.5f; v1.y = -0.5f; v1.z = 0.0f; v1.r = 1.0f; v1.g = 0.0f; v1.b = 0.0f; v1.u = 0.0f; v1.v = 0.0f;
	v2.x = 0.5f; v2.y = -0.5f; v2.z = 0.0f; v2.r = 0.0f; v2.g = 1.0f; v2.b = 0.0f; v2.u = 0.25f; v2.v = 0.0f;
	v3.x = 0.5f; v3.y = 0.5f; v3.z = 0.0f; v3.r = 0.0f; v3.g = 0.0f; v3.b = 1.0f; v3.u = 0.25f; v3.v = 1.0f;
	v4.x = -0.5f; v4.y = 0.5f; v4.z = 0.0f; v4.r = 1.0f; v4.g = 1.0f; v4.b = 0.0f; v4.u = 0.0f; v4.v = 1.0f;
	vertices.push_back(v1);
	vertices.push_back(v2);
	vertices.push_back(v3);
	vertices.push_back(v1);
	vertices.push_back(v3);
	vertices.push_back(v4);

	pMesh = sMeshArray + sNumMesh++;
	pTex = sTexArray + sNumTex++;
	*pMesh = CreateMesh(vertices);
	*pTex = TextureLoad("mario.png");

	//+ Create Enemy mesh/texture
	vertices.clear();
	v1.x = -0.5f; v1.y = -0.5f; v1.z = 0.0f; v1.r = 1.0f; v1.g = 0.0f; v1.b = 0.0f; v1.u = 0.0f; v1.v = 0.0f;
	v2.x = 0.5f; v2.y = -0.5f; v2.z = 0.0f; v2.r = 0.0f; v2.g = 1.0f; v2.b = 0.0f; v2.u = 0.5f; v2.v = 0.0f;
	v3.x = 0.5f; v3.y = 0.5f; v3.z = 0.0f; v3.r = 0.0f; v3.g = 0.0f; v3.b = 1.0f; v3.u = 0.5f; v3.v = 1.0f;
	v4.x = -0.5f; v4.y = 0.5f; v4.z = 0.0f; v4.r = 1.0f; v4.g = 1.0f; v4.b = 0.0f; v4.u = 0.0f; v4.v = 1.0f;
	vertices.push_back(v1);
	vertices.push_back(v2);
	vertices.push_back(v3);
	vertices.push_back(v1);
	vertices.push_back(v3);
	vertices.push_back(v4);

	pMesh = sMeshArray + sNumMesh++;
	pTex = sTexArray + sNumTex++;
	*pMesh = CreateMesh(vertices);
	*pTex = TextureLoad("kuribo.png");


	//+ Create Item mesh/texture
	vertices.clear();
	v1.x = -0.5f; v1.y = -0.5f; v1.z = 0.0f; v1.r = 1.0f; v1.g = 0.0f; v1.b = 0.0f; v1.u = 0.0f; v1.v = 0.0f;
	v2.x = 0.5f; v2.y = -0.5f; v2.z = 0.0f; v2.r = 0.0f; v2.g = 1.0f; v2.b = 0.0f; v2.u = 0.25f; v2.v = 0.0f;
	v3.x = 0.5f; v3.y = 0.5f; v3.z = 0.0f; v3.r = 0.0f; v3.g = 0.0f; v3.b = 1.0f; v3.u = 0.25f; v3.v = 1.0f;
	v4.x = -0.5f; v4.y = 0.5f; v4.z = 0.0f; v4.r = 1.0f; v4.g = 1.0f; v4.b = 0.0f; v4.u = 0.0f; v4.v = 1.0f;
	vertices.push_back(v1);
	vertices.push_back(v2);
	vertices.push_back(v3);
	vertices.push_back(v1);
	vertices.push_back(v3);
	vertices.push_back(v4);

	pMesh = sMeshArray + sNumMesh++;
	pTex = sTexArray + sNumTex++;
	*pMesh = CreateMesh(vertices);
	*pTex = TextureLoad("coin.png");


	// Create Level mesh/texture
	vertices.clear();
	v1.x = -0.5f; v1.y = -0.5f; v1.z = 0.0f; v1.r = 1.0f; v1.g = 0.0f; v1.b = 0.0f; v1.u = 0.01f; v1.v = 0.01f;
	v2.x = 0.5f; v2.y = -0.5f; v2.z = 0.0f; v2.r = 0.0f; v2.g = 1.0f; v2.b = 0.0f; v2.u = 0.24f; v2.v = 0.01f;
	v3.x = 0.5f; v3.y = 0.5f; v3.z = 0.0f; v3.r = 0.0f; v3.g = 0.0f; v3.b = 1.0f; v3.u = 0.24f; v3.v = 1.0f;
	v4.x = -0.5f; v4.y = 0.5f; v4.z = 0.0f; v4.r = 1.0f; v4.g = 1.0f; v4.b = 0.0f; v4.u = 0.01f; v4.v = 1.0f;
	vertices.push_back(v1);
	vertices.push_back(v2);
	vertices.push_back(v3);
	vertices.push_back(v1);
	vertices.push_back(v3);
	vertices.push_back(v4);

	sMapMesh = sMeshArray + sNumMesh++;
	sMapTex = sTexArray + sNumTex++;
	*sMapMesh = CreateMesh(vertices);
	*sMapTex = TextureLoad("level.png");
	sMapOffset = 0.25f;


	//-----------------------------------------
	// Load level from txt file to sMapData, sMapCollisonData, sPlayer_start_position
	//	- 0	is an empty space
	//	- 1-4 are background tile
	//	- 5-7 are game objects location
	//-----------------------------------------

	std::ifstream myfile("map2.txt");
	if (myfile.is_open())
	{
		myfile >> MAP_HEIGHT;
		myfile >> MAP_WIDTH;
		sMapData = new int* [MAP_HEIGHT];
		sMapCollisionData = new int* [MAP_HEIGHT];
		for (int y = 0; y < MAP_HEIGHT; y++) {
			sMapData[y] = new int[MAP_WIDTH];
			sMapCollisionData[y] = new int[MAP_WIDTH];
			for (int x = 0; x < MAP_WIDTH; x++) {
				myfile >> sMapData[y][x];
			}
		}
		myfile.close();
	}

	//+ load collision data to sMapCollisionData
	//	- 0: non-blocking cell
	//	- 1: blocking cell
	//** Don't forget that sMapCollisionData index go from- 
	//**	 bottom to top not from top to bottom as in the text file
	for (int y = 0; y < MAP_HEIGHT; y++) {
		for (int x = 0; x < MAP_WIDTH; x++) {
			bool isBlocking = sMapData[y][x] > 0 && sMapData[y][x] < 5;
			sMapCollisionData[y][x] = isBlocking ? 1 : 0;
		}
	}


	//-----------------------------------------
	//+ Compute Map Transformation Matrix
	//-----------------------------------------

	float scaleX = 800.f / VIEW_WIDTH;  // Scale factor for x-axis
	float scaleY = 800.f / VIEW_HEIGHT;  // Scale factor for y-axis
	glm::mat4 scaleMatrix = glm::mat4(1.0f);  // Identity matrix
	scaleMatrix[0][0] = scaleX;
	scaleMatrix[1][1] = scaleY;

	float translateX = -400.f; // 800 divided by 2
	float translateY = -400.f; // 800 divided by 2
	glm::mat4 translateMatrix = glm::mat4(1.0f);  // Identity matrix
	translateMatrix[3][0] = translateX;
	translateMatrix[3][1] = translateY;

	sMapMatrix = translateMatrix * scaleMatrix;


	printf("Level1: Load\n");
}

void GameStateLevel1Init(void) {

	//-----------------------------------------
	// Create game object instance from Map
	//	0,1,2,3,4:	level tiles
	//  5: player, 6: enemy, 7: item
	//-----------------------------------------
	GameObj* enemy = nullptr;

	for (int y = 0; y < MAP_HEIGHT; y++) {
		for (int x = 0; x < MAP_WIDTH; x++) {

			switch (sMapData[y][x]) {
				// Player
			case 5:
				sPlayer = gameObjInstCreate(TYPE_PLAYER, glm::vec3(x + 0.5f, (MAP_HEIGHT - y) - 0.5f, 0.0f), glm::vec3(0.0f, 0.0f, 0.0f),
					glm::vec3(1.0f, 1.0f, 1.0f), 0.0f, false, 2, 0, 0.25f);
				sPlayer->scale.x = -1;
				sPlayer->mortal = false;
				sPlayer_start_position = glm::vec3(x + 0.5f, (MAP_HEIGHT - y) - 0.5f, 0.0f);
				break;

				//+ Enemy
			case 6:
				enemy = gameObjInstCreate(TYPE_ENEMY, glm::vec3(x + 0.5f, (MAP_HEIGHT - y) - 0.5f, 0.0f), glm::vec3(0.0f, 0.0f, 0.0f),
					glm::vec3(1.0f, 1.0f, 1.0f), 0.0f, true, 1, 0, 0.5f);
				enemy->state = STATE_GOING_LEFT;
				break;

				//+ Item
			case 7:
				gameObjInstCreate(TYPE_ITEM, glm::vec3(x + 0.5f, (MAP_HEIGHT - y) - 0.5f, 0.0f), glm::vec3(0.0f, 0.0f, 0.0f),
					glm::vec3(1.0f, 1.0f, 1.0f), 0.0f, true, 3, 0, 0.25f);
				break;

			default:
				break;
			}
		}
	}


	// Initalize some data. ex. score and player life
	sScore = 0;
	sPlayerLives = PLAYER_INITIAL_NUM;
	sRespawnCountdown = 0;

	// Sound
	SoundEngine = createIrrKlangDevice();
	SoundEngine->play2D("mario_level.ogg", true);		//loop or not

	printf("Level1: Init\n");
}

void GameStateLevel1Update(double dt, long frame, int& state) {

	//-----------------------------------------
	// Get user input
	//-----------------------------------------

	if (sRespawnCountdown <= 0) {

		//+ Moving the Player
		//	- W:	jumping
		//	- AD:	go left, go right
		if ((glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) && (sPlayer->jumping == false)) {
			sPlayer->jumping = true;
			sPlayer->velocity.y = JUMP_VELOCITY;
			SoundEngine->play2D("jump.wav");

		}
		if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) {
			sPlayer->scale.x = 1;
			sPlayer->velocity.x = -MOVE_VELOCITY_PLAYER;

			// play animation
			sPlayer->anim = true;
		}
		else if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) {
			sPlayer->scale.x = -1;
			sPlayer->velocity.x = MOVE_VELOCITY_PLAYER;

			// play animation
			sPlayer->anim = true;
		}
		else {
			float friction = 0.05f;
			sPlayer->velocity.x *= (1.0f - friction);

			// using Idle animation
			sPlayer->anim = false;
			sPlayer->offsetX = sPlayer->offset * 0;
		}

		if (sPlayer->jumping) {
			sPlayer->anim = false;
			sPlayer->offsetX = sPlayer->offset * 3;
		}
	}
	else {
		//+ update sRespawnCountdown
		sRespawnCountdown -= dt;
		if (sRespawnCountdown <= 0)
			state = 2;

	}


	// Cam zoom UI
	if (glfwGetKey(window, GLFW_KEY_U) == GLFW_PRESS) {
		ZoomIn(0.1f);
	}
	if (glfwGetKey(window, GLFW_KEY_I) == GLFW_PRESS) {
		ZoomOut(0.1f);
	}


	//-----------------------------------------
	// Update some game obj behavior
	//-----------------------------------------
	for (int i = 0; i < GAME_OBJ_INST_MAX; i++)
	{
		GameObj* pInst = sGameObjInstArray + i;

		// skip inactive object
		if (pInst->flag == FLAG_INACTIVE)
			continue;

		switch (pInst->type)
		{
		case TYPE_ENEMY:
			EnemyStateMachine(pInst);
			break;
		default:
			break;
		}

	}


	//---------------------------------------------------------
	// Update all game obj position using velocity 
	//---------------------------------------------------------
	for (int i = 0; i < GAME_OBJ_INST_MAX; i++) {
		GameObj* pInst = sGameObjInstArray + i;

		// skip inactive object
		if (pInst->flag == FLAG_INACTIVE)
			continue;

		if (pInst->type == TYPE_PLAYER || pInst->type == TYPE_ENEMY) {

			// Apply gravity: Velocity Y = Gravity * Frame Time + Velocity Y
			if (pInst->jumping) {
				pInst->velocity.y += GRAVITY * dt;
			}

			// Update position using Velocity
			pInst->position += pInst->velocity * glm::vec3(dt, dt, 0.0f);
		}

	}


	//--------------------------------------------------------------------
	// Update camera's position
	//--------------------------------------------------------------------
	{
		glm::mat4 matTransform = sMapMatrix * sPlayer->modelMatrix;
		float camX = matTransform[3][0] < 0.f ? 0.f : matTransform[3][0],
			camY = matTransform[3][1] < 0.f ? 0.f : matTransform[3][1];
		SetCamPosition(camX, camY);

		// update camera's position in map coordinate
		sCamPosition.x = floor(sPlayer->position.x) < floor(VIEW_WIDTH / 2.f) ? floor(VIEW_WIDTH / 2.f) : floor(sPlayer->position.x);
		sCamPosition.y = floor(sPlayer->position.y) < floor(VIEW_HEIGHT / 2.f) ? floor(VIEW_HEIGHT / 2.f) : floor(sPlayer->position.y);
	}


	//--------------------------------------------------------------------
	// Decrease object lifespan for self destroyed objects (ex. explosion)
	//--------------------------------------------------------------------


	//-----------------------------------------
	// Update animation for animated object 
	//-----------------------------------------
	if (frame % ANIMATION_SPEED == 0) {
		for (int i = 0; i < GAME_OBJ_INST_MAX; i++) {
			GameObj* pInst = sGameObjInstArray + i;

			// skip inactive object
			if (pInst->flag == FLAG_INACTIVE)
				continue;

			//+ if this is an animated object
			if (pInst->anim) {

				//+ increment pInst->currFrame
				pInst->currFrame++;

				//	- if we reach the last frame then set the current frame back to frame 0
				if (pInst->currFrame > pInst->numFrame)
					pInst->currFrame = 0;

				//+ use currFrame infomation to set pInst->offsetX
				pInst->offsetX = pInst->currFrame * pInst->offset;

			}
		}
	}


	//-----------------------------------------
	// Check for collsion with the Map
	//-----------------------------------------
	for (int i = 0; i < GAME_OBJ_INST_MAX; i++) {
		GameObj* pInst = sGameObjInstArray + i;

		// skip inactive object
		if (pInst->flag == FLAG_INACTIVE)
			continue;

		if (pInst->type == TYPE_PLAYER) {
			bool isInAir = false;

			// Update Player position, velocity, jumping states when the player collide with the map
			sPlayer->mapCollsionFlag = CheckMapCollision(sPlayer->position.x, sPlayer->position.y, isInAir);

			// Collide Left
			if (sPlayer->mapCollsionFlag & COLLISION_LEFT) {
				sPlayer->position.x = (int)sPlayer->position.x + 0.5f;      // 4.32 -> 4,   4.89 -> 4  sMapCollisionData[int][int]
			}

			//+ Collide Right
			if (sPlayer->mapCollsionFlag & COLLISION_RIGHT) {
				sPlayer->position.x = (int)sPlayer->position.x + 0.5f;      // 4.32 -> 4,   4.89 -> 4  sMapCollisionData[int][int]
			}


			//+ Collide Top
			if (sPlayer->mapCollsionFlag & COLLISION_TOP) {
				sPlayer->velocity.y = -0.5f;
				sPlayer->position.y = (int)sPlayer->position.y + 0.5f;      // 4.32 -> 4,   4.89 -> 4  sMapCollisionData[int][int]
			}


			//+ Player is on the ground or just landed on the ground
			if (sPlayer->mapCollsionFlag & COLLISION_BOTTOM) {
				sPlayer->jumping = false;
				sPlayer->velocity.y = 0;
				sPlayer->position.y = (int)sPlayer->position.y + 0.5f;
			}

			//+ Player is jumping/falling
			if (isInAir) {
				sPlayer->jumping = true;
			}


		}
	}



	//-----------------------------------------
	// Check for collsion between game objects
	//	- Player vs Enemy
	//	- Player vs Item
	//-----------------------------------------

	for (int i = 0; i < GAME_OBJ_INST_MAX; i++) {

		if (sPlayer->flag == FLAG_INACTIVE)
			break;


		GameObj* pInst = sGameObjInstArray + i;

		// skip inactive object
		if (pInst->flag == FLAG_INACTIVE)
			continue;


		//+ Player vs Enemy
		//	- if the Player die, set the sRespawnCountdown > 0	
		if (pInst->type == TYPE_ENEMY && sPlayer->mortal) {
			int result = _detectCollisionAABB({ sPlayer->position.x, sPlayer->position.y , 1.f, 1.f }, { pInst->position.x, pInst->position.y, 1.f, 1.f });
			if (result) {
				if (result & COLLISION_BOTTOM) {
					gameObjInstDestroy(*pInst);
				}
				else {
					if (--sPlayerLives <= 0) {
						sRespawnCountdown = 2000;
						gameObjInstDestroy(*sPlayer);
					}
					else {
						sPlayer->mortal = false;
						sMortalCountdown = COOLDOWN;
					}
				}
			}
		}


		//+ Player vs Item
		if (pInst->type == TYPE_ITEM) {
			int result = _detectCollisionAABB({ sPlayer->position.x, sPlayer->position.y , 1.f, 1.f }, { pInst->position.x, pInst->position.y, 1.f, 1.f });
			if (result) {
				sScore++;
				SoundEngine->play2D("coin.wav");
				gameObjInstDestroy(*pInst);
			}
		}
	}

	//-----------------------------------------
	// Update player mortal cooldown
	//-----------------------------------------

	if (sMortalCountdown > 0.f && !sPlayer->mortal) {
		sMortalCountdown -= dt;
	}
	else {
		sPlayer->mortal = true;
	}

	//-----------------------------------------
	// Update modelMatrix of all game obj
	//-----------------------------------------
	for (int i = 0; i < GAME_OBJ_INST_MAX; i++) {
		GameObj* pInst = sGameObjInstArray + i;

		// skip inactive object
		if (pInst->flag == FLAG_INACTIVE)
			continue;

		// Compute the scaling matrix
		// Compute the rotation matrix, we should rotate around z axis 
		// Compute the translation matrix
		// Concatenate the 3 matrix to from Model Matrix
		glm::mat4 rMat = glm::rotate(glm::mat4(1.0f), pInst->orientation, glm::vec3(0.0f, 0.0f, 1.0f));
		glm::mat4 sMat = glm::scale(glm::mat4(1.0f), pInst->scale);
		glm::mat4 tMat = glm::translate(glm::mat4(1.0f), pInst->position);
		pInst->modelMatrix = tMat * sMat * rMat;
	}

	double fps = 1.0 / dt;
	printf("Level1: Update @> %f fps, frame>%ld\n", fps, frame);
	printf("Life> %i\n", sPlayerLives);
	printf("Score> %i\n", sScore);
	printf("num obj> %i\n", sNumGameObj);
}

void GameStateLevel1Draw(void) {

	// Clear the screen
	glClearColor(0.0f, 0.5f, 1.0f, 0.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);


	//--------------------------------------------------------
	// Draw Level
	//--------------------------------------------------------
	glm::mat4 matTransform;
	glm::mat4 cellMatrix;

	// calculate for view culling rendering
	int minRenderCoorX = floor(sCamPosition.x - ceil(VIEW_WIDTH / 2)),
		maxRenderCoorX = ceil(sCamPosition.x + ceil(VIEW_WIDTH / 2));
	int minRenderCoorY = floor((MAP_HEIGHT - sCamPosition.y) - ceil(VIEW_HEIGHT / 2)) - 1,
		maxRenderCoorY = ceil((MAP_HEIGHT - sCamPosition.y) + ceil(VIEW_HEIGHT / 2));

	for (int y = minRenderCoorY; y <= maxRenderCoorY; y++) {

		// prevent out of bound
		if (y < 0 || y >= MAP_HEIGHT) continue;

		for (int x = minRenderCoorX; x <= maxRenderCoorX; x++) {

			// prevent out of bound
			if (x < 0 || x >= MAP_WIDTH) continue;

			//+ Only draw non-background cell
			if (sMapData[y][x] > 0 && sMapData[y][x] < 5) {

				glm::mat4 tMat = glm::translate(glm::mat4(1.0f), glm::vec3(x + 0.5f, (MAP_HEIGHT - y) - 0.5f, 0.0f));
				glm::mat4 sMat = glm::scale(glm::mat4(1.0f), glm::vec3(1.0f, 1.0f, 1.0f));

				//+ Find transformation matrix of each cell
				cellMatrix = tMat * sMat;

				// Transform cell from map space [0,MAP_SIZE] to screen space [-width/2,width/2]
				matTransform = sMapMatrix * cellMatrix;

				// Render each cell
				SetRenderMode(CDT_TEXTURE, 1.0f);
				SetTexture(*sMapTex, sMapOffset * (sMapData[y][x] - 1), 0.0f);
				SetTransform(matTransform);
				DrawMesh(*sMapMesh);
			}
		}
	}


	//--------------------------------------------------------
	// Draw all game object instance in the sGameObjInstArray
	//--------------------------------------------------------

	for (int i = 0; i < GAME_OBJ_INST_MAX; i++) {
		GameObj* pInst = sGameObjInstArray + i;

		// skip inactive object
		if (pInst->flag == FLAG_INACTIVE)
			continue;

		int objCoorX = floor(pInst->position.x),
			objCoorY = floor(MAP_HEIGHT - pInst->position.y);

		// skip if out of view
		if (objCoorX < minRenderCoorX || objCoorX > maxRenderCoorX ||
			objCoorY < minRenderCoorY || objCoorY > maxRenderCoorY)
			continue;


		// Transform cell from map space [0,MAP_SIZE] to screen space [-width/2,width/2]
		matTransform = sMapMatrix * pInst->modelMatrix;

		int alpha = 1.0f;

		if (pInst->type == TYPE_PLAYER && !pInst->mortal)
			alpha = sMortalCountdown % 2;


		SetRenderMode(CDT_TEXTURE, alpha);
		SetTexture(*pInst->tex, pInst->offsetX, pInst->offsetY);
		SetTransform(matTransform);
		DrawMesh(*pInst->mesh);
	}


	// Swap the buffer, to present the drawing
	glfwSwapBuffers(window);
}

void GameStateLevel1Free(void) {

	// call gameObjInstDestroy for all object instances in the sGameObjInstArray
	for (int i = 0; i < GAME_OBJ_INST_MAX; i++) {
		gameObjInstDestroy(sGameObjInstArray[i]);
	}

	// reset camera
	ResetCam();

	// Free sound
	SoundEngine->drop();

	printf("Level1: Free\n");
}

void GameStateLevel1Unload(void) {

	// Unload all meshes in MeshArray
	for (int i = 0; i < sNumMesh; i++) {
		UnloadMesh(sMeshArray[i]);
	}

	// Unload all textures in TexArray
	for (int i = 0; i < sNumTex; i++) {
		TextureUnload(sTexArray[i]);
	}

	// Unload Level
	for (int i = 0; i < MAP_HEIGHT; ++i) {
		delete[] sMapData[i];
		delete[] sMapCollisionData[i];
	}
	delete[] sMapData;
	delete[] sMapCollisionData;


	printf("Level1: Unload\n");
}
