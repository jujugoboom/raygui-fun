/*
Raylib example file.
This is an example main file for a simple raylib project.
Use this as a starting point or replace it with your code.

For a C++ project simply rename the file to .cpp and re-run the build script

-- Copyright (c) 2020-2024 Jeffery Myers
--
--This software is provided "as-is", without any express or implied warranty. In no event
--will the authors be held liable for any damages arising from the use of this software.

--Permission is granted to anyone to use this software for any purpose, including commercial
--applications, and to alter it and redistribute it freely, subject to the following restrictions:

--  1. The origin of this software must not be misrepresented; you must not claim that you
--  wrote the original software. If you use this software in a product, an acknowledgment
--  in the product documentation would be appreciated but is not required.
--
--  2. Altered source versions must be plainly marked as such, and must not be misrepresented
--  as being the original software.
--
--  3. This notice may not be removed or altered from any source distribution.

I've changed the original file - jujugogoom 2024-12-01

*/

#include "raylib.h"

#define RAYGUI_IMPLEMENTATION
#include "raygui.h"
#include "style_jungle.h"

#include "resource_dir.h" // utility header for SearchAndSetResourceDir

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/types.h>
#include <pthread.h>
#include <math.h>
#include <limits.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#endif

#define MAX_CHAR 127 // Assuming the alphabet size is at most 127
#define MARKER ")))"

// Image hashes are 64 bit unsigned ints
#define HASH_SIZE 65
// Node structure for the BK-Tree
typedef struct Node
{
	char *word;
	struct Node *children[MAX_CHAR];
} Node;

typedef struct ImageNode
{
	unsigned long long int hash;
	char *path;
	struct ImageNode *children[HASH_SIZE];
} ImageNode;

typedef struct NodeStack
{
	struct Node *head;
	struct NodeStack *next;
} NodeStack;

typedef struct ImageNodeStack
{
	struct ImageNode *head;
	struct ImageNodeStack *next;
} ImageNodeStack;

typedef struct CharStack
{
	char *word;
	int len;
	struct CharStack *next;
} CharStack;

typedef struct IndexingArguments
{
	Node **root;
	size_t *total;
	size_t *completed;
	bool *done;
	bool *kill;
} IndexingArguments;

typedef struct LoadingArguments
{
	Node **root;
	size_t *total;
	size_t *completed;
	bool *done;
	bool *kill;
} LoadingArguments;

typedef struct ImageIndexArguments
{
	ImageNode **root;
	size_t *total;
	size_t *completed;
	bool *done;
	bool *kill;
} ImageIndexArguments;

unsigned long long int dctTransform(Image image);

// Function to create a new node
Node *createNode(char *word)
{
	Node *newNode = (Node *)malloc(sizeof(Node));
	newNode->word = strdup(word); // Allocate memory for the word
	Node *children[MAX_CHAR] = {0};
	memcpy(newNode->children, children, sizeof(children));
	return newNode;
}

ImageNode *createImageNode(Image image, char *path)
{
	ImageNode *newNode = malloc(sizeof(ImageNode));
	newNode->hash = dctTransform(image);
	newNode->path = strdup(path);
	printf("Inserting %s with hash %llx\n", newNode->path, newNode->hash);
	ImageNode *children[HASH_SIZE] = {0};
	memcpy(newNode->children, children, sizeof(children));
	return newNode;
}

// Frees malloc'd data as well as all children, can be used to destruct whole tree
void freeNode(Node *node)
{
	if (node == NULL)
	{
		return;
	}
	free(node->word);
	for (int i = 0; i < MAX_CHAR; i++)
	{
		freeNode(node->children[i]);
		node->children[i] = NULL;
	}
	free(node);
}

void freeImageNode(ImageNode *node)
{
	if (node == NULL)
	{
		return;
	}
	free(node->path);
	for (int i = 0; i < HASH_SIZE; i++)
	{
		freeImageNode(node->children[i]);
		node->children[i] = NULL;
	}
	free(node);
}

NodeStack *push_node(NodeStack *stack, Node *node)
{
	NodeStack *new = (NodeStack *)malloc(sizeof(NodeStack));
	new->head = node;
	new->next = stack;
	return new;
}

Node *pop_node(NodeStack **stack)
{
	if (stack == NULL)
	{
		return NULL;
	}
	Node *ret = (*stack)->head;
	NodeStack *last = *stack;
	*stack = (*stack)->next;
	free(last);
	return ret;
}

ImageNodeStack *push_image_node(ImageNodeStack *stack, ImageNode *node)
{
	ImageNodeStack *new = malloc(sizeof(ImageNodeStack));
	new->head = node;
	new->next = stack;
	return new;
}

ImageNode *pop_image_node(ImageNodeStack **stack)
{
	if (stack == NULL)
	{
		return NULL;
	}
	ImageNode *ret = (*stack)->head;
	ImageNodeStack *last = *stack;
	*stack = (*stack)->next;
	free(last);
	return ret;
}

CharStack *push_char(CharStack *stack, char *word)
{
	CharStack *new = (CharStack *)malloc(sizeof(CharStack));
	new->word = word;
	new->next = stack;
	new->len = stack == NULL ? 1 : stack->len + 1;
	return new;
}

CharStack *push_back_char(CharStack *stack, char *word)
{
	if (stack == NULL)
	{
		return push_char(stack, word);
	}
	CharStack *curr = stack;
	while (1)
	{
		CharStack *next = curr->next;
		if (next == NULL)
		{
			CharStack *new = (CharStack *)malloc(sizeof(CharStack));
			new->word = word;
			new->len = 1;
			new->next = NULL;
			curr->next = new;
			stack->len++;
			return stack;
		}
		curr = next;
	}
	return stack;
}

char *pop_char(CharStack **stack)
{
	if (*stack == NULL)
	{
		return NULL;
	}
	char *ret = (*stack)->word;
	CharStack *last = *stack;
	*stack = (*stack)->next;
	free(last);
	return ret;
}

int min4(int a, int b, int c, int d)
{
	int min = a;
	if (b < min)
		min = b;
	if (c < min)
		min = c;
	if (d < min)
		min = d;
	return min;
}

int damerau_levenshtein_distance(const char *a, const char *b)
{
	int len_a = strlen(a);
	int len_b = strlen(b);
	// Create da array to store the last occurrence of each character
	int *da = calloc(sizeof(int), MAX_CHAR);
	// Create d array (with extra rows and columns for initialization)
	int **d = malloc((len_a + 2) * sizeof(int *));
	for (int i = 0; i <= len_a + 1; i++)
	{
		d[i] = malloc((len_b + 2) * sizeof(int));
	}
	// Initialize d array
	int maxdist = len_a + len_b;
	d[0][0] = maxdist;
	for (int i = 0; i <= len_a; i++)
	{
		d[i + 1][0] = maxdist;
		d[i + 1][1] = i;
	}
	for (int j = 0; j <= len_b; j++)
	{
		d[0][j + 1] = maxdist;
		d[1][j + 1] = j;
	}
	// Calculate Damerau-Levenshtein distance
	for (int i = 1; i <= len_a; i++)
	{
		int db = 0;
		for (int j = 1; j <= len_b; j++)
		{
			int k = da[b[j - 1]];
			int l = db;
			int cost = (a[i - 1] == b[j - 1]) ? 0 : 1;
			if (cost == 0)
				db = j;
			d[i + 1][j + 1] =
				min4(d[i][j] + cost,						   // Substitution
					 d[i + 1][j] + 1,						   // Insertion
					 d[i][j + 1] + 1,						   // Deletion
					 d[k][l] + (i - k - 1) + 1 + (j - l - 1)); // Transposition
		}
		da[a[i - 1]] = i;
	}
	int res = d[len_a + 1][len_b + 1];
	// Free allocated memory
	free(da);
	for (int i = 0; i <= len_a + 1; i++)
	{
		free(d[i]);
	}
	free(d);

	return res;
}

void insert(Node *root, char *word)
{
	if (word == NULL || strlen(word) == 0)
	{
		return;
	}

	Node *curr = root;
	while (curr != NULL)
	{
		int distance = damerau_levenshtein_distance(curr->word, word);
		if (distance == 0)
		{
			return;
		}
		int index = distance % MAX_CHAR; // Hash the distance to find the child
		Node *next = curr->children[index];
		if (next == NULL)
		{
			next = createNode(word);
			curr->children[index] = next;
			return;
		}
		curr = next;
	}
}

int min(int a, int b)
{
	return a < b ? a : b;
}

int max(int a, int b)
{
	return a > b ? a : b;
}

// Function to search for words within a given radius in the BK-Tree
CharStack *search(Node *root, char *query, int radius, int max)
{
	if (root == NULL)
	{
		return NULL;
	}
	NodeStack *stack = push_node(NULL, root);
	CharStack *potential[radius + 1];
	for (int i = 0; i < radius + 1; i++)
	{
		potential[i] = NULL;
	}
	CharStack *results = NULL;
	while (stack != NULL)
	{
		Node *curr = pop_node(&stack);
		int distance = damerau_levenshtein_distance(curr->word, query);
		if (distance <= radius)
		{
			potential[distance] = push_char(potential[distance], curr->word);
		}
		int lower = fmax(distance - radius, 0);
		int upper = min(distance + radius, MAX_CHAR - 1);
		for (int i = lower; i <= upper; i++)
		{
			if (curr->children[i])
			{
				stack = push_node(stack, curr->children[i]);
			}
		}
	}
	int curr_dist = 0;
	while ((results == NULL || results->len < max) && curr_dist <= radius)
	{
		char *res = pop_char(&potential[curr_dist]);
		if (res == NULL)
		{
			curr_dist++;
		}
		else
		{
			results = push_back_char(results, res);
		}
	}
	return results;
}

char *ltrim(char *s)
{
	while (isspace(*s))
		s++;
	return s;
}

char *rtrim(char *s)
{
	char *back = s + strlen(s);
	while (isspace(*--back))
		;
	*(back + 1) = '\0';
	return s;
}

char *trim(char *s)
{
	return rtrim(ltrim(s));
}

void *create_tree(void *args)
{
	struct IndexingArguments *arguments = args;

	Node **root = arguments->root;
	size_t *completed = arguments->completed;
	FILE *fp;
	char line[128];
	size_t len = 0;

	fp = fopen("words.txt", "r");
	if (fp == NULL)
		exit(EXIT_FAILURE);
	fseek(fp, 0, SEEK_END);
	long fsize = ftell(fp);
	fseek(fp, 0, SEEK_SET); /* same as rewind(f); */
	// printf("Filesize %d", fsize);
	char *string = malloc(fsize + 1);
	fread(string, fsize, 1, fp);
	fclose(fp);

	*arguments->total = fsize;

	string[fsize] = 0;
	char *first_word = strtok(string, "\n");
	*completed = strlen(first_word) + 1;
	first_word = trim(first_word);
	*root = createNode(first_word);
	char *to_insert = strtok(NULL, "\n");
	while (to_insert && !*arguments->kill)
	{
		// printf("Inserting %s", to_insert);
		*completed += strlen(to_insert) + 1;
		insert(*root, trim(to_insert));
		to_insert = strtok(NULL, "\n");
	}
	*arguments->done = true;
	free(string);
	pthread_exit(0);
}

void serialize(Node *root, FILE *fp)
{
	// Base case
	if (root == NULL)
	{
		return;
	}

	// Else, store current node and recur for its children
	fprintf(fp, "%s :::", root->word);
	for (int i = 0; i < MAX_CHAR; i++)
		if (root->children[i])
		{
			fprintf(fp, "%d --", i);
			serialize(root->children[i], fp);
		}

	// Store marker at the end of children
	fprintf(fp, "%s :::", MARKER);
}

void write_tree(Node *tree)
{
	FILE *file = fopen("bktree.bin", "wb");
	if (file != NULL)
	{
		serialize(tree, file);
		fclose(file);
	}
}

void deSerialize(Node **root, FILE *fp, size_t *completed, bool *kill)
{
	// Read next item from file. If there are no more items or next
	// item is marker, then return 1 to indicate same
	char val[128];
	if (!fscanf(fp, "%s :::", (char *)&val) || strcmp(val, MARKER) == 0)
		return;

	// Else create node with this item and recur for children
	*root = createNode(val);
	*completed = ftell(fp);
	int idx;
	while (fscanf(fp, "%d --", &idx) && !*kill)
	{
		deSerialize(&(*root)->children[idx], fp, completed, kill);
	}
	if (*kill)
	{
		return;
	}
	fscanf(fp, "%s :::", (char *)&val);
	if (strcmp(val, MARKER) != 0)
	{
		exit(1);
	}
	// Finally return 0 for successful finish
	return;
}

void print_tree(Node *root)
{
	printf("%s -- \n", root->word);
	for (int i = 0; i < MAX_CHAR; i++)
	{
		if (root->children[i])
		{
			printf("%d. ", i);
			print_tree(root->children[i]);
		}
	}
}

void read_tree(Node **root, size_t *total, size_t *completed, bool *kill)
{
	FILE *file = fopen("bktree.bin", "r");
	if (file != NULL)
	{
		fseek(file, 0, SEEK_END);
		long fsize = ftell(file);
		fseek(file, 0, SEEK_SET); /* same as rewind(f); */
		*total = fsize;
		deSerialize(root, file, completed, kill);
		fclose(file);
		// print_tree(root);
	}
}

void *load_tree(void *args)
{
	LoadingArguments *arguments = args;
	read_tree(arguments->root, arguments->total, arguments->completed, arguments->kill);
	*arguments->done = true;
	pthread_exit(0);
}

unsigned long long int dctTransform(Image image)
{
	int n = 32, m = 32;
	Image copy = ImageCopy(image);
	ImageResize(&copy, n, m);
	ImageColorGrayscale(&copy);
	unsigned char *matrix = copy.data;
	int i, j, k, l;

	// dct will store the discrete cosine transform
	float dct[n][m];

	float ci, cj, dct1, sum;

	for (i = 0; i < m; i++)
	{
		for (j = 0; j < n; j++)
		{

			// ci and cj depends on frequency as well as
			// number of row and columns of specified matrix
			if (i == 0)
				ci = 1. / sqrt(m);
			else
				ci = sqrt(2. / m);
			if (j == 0)
				cj = 1. / sqrt(n);
			else
				cj = sqrt(2. / n);

			// sum will temporarily store the sum of
			// cosine signals
			sum = 0;
			for (k = 0; k < m; k++)
			{
				for (l = 0; l < n; l++)
				{
					dct1 = (int)matrix[(k * 32) + l] *
						   cos((PI / m) * (k + .5) * i) *
						   cos((PI / n) * (l + .5) * j);
					sum += dct1;
				}
			}
			dct[i][j] = ci * cj * sum;
		}
	}

	float reducedDct[8][8];
	float lowFreqTotal = 0.f;
	// Reduce dct to 8x8 by taking top left to get lowest frequencies
	for (i = 0; i < 8; i++)
	{
		for (j = 0; j < 8; j++)
		{
			reducedDct[i][j] = dct[i][j];

			if (i == 0 && j == 0)
				continue;
			lowFreqTotal += dct[i][j];
		}
	}

	float avg = lowFreqTotal / 63;
	unsigned long long int result = 0;
	for (i = 0; i < 8; i++)
	{
		for (j = 0; j < 8; j++)
		{
			if (reducedDct[i][j] > avg)
				result |= 1;
			result = result << 1;
		}
	}

	UnloadImage(copy);
	return result;
}

void insertImage(ImageNode *root, char *path)
{
	ImageNode *curr = root;
	Image image = LoadImage(path);

	if (!IsImageValid(image))
	{
		UnloadImage(image);
		printf("Invalid image provided");
		return;
	}

	ImageNode *newNode = createImageNode(image, path);
	while (curr != NULL)
	{
		int distance = __builtin_popcount(curr->hash ^ newNode->hash);
		int index = distance % HASH_SIZE;
		ImageNode *next = curr->children[index];
		if (next == NULL)
		{
			curr->children[index] = newNode;
			break;
		}
		curr = next;
	}
	UnloadImage(image);
}

CharStack *searchImages(ImageNode *root, Image image, int radius, int max)
{
	if (root == NULL)
	{
		return NULL;
	}
	unsigned long long int searchHash = dctTransform(image);

	ImageNodeStack *stack = push_image_node(NULL, root);
	CharStack *potential[radius + 1];
	for (int i = 0; i < radius + 1; i++)
	{
		potential[i] = NULL;
	}
	CharStack *results = NULL;
	while (stack != NULL)
	{
		ImageNode *curr = pop_image_node(&stack);
		int distance = __builtin_popcount(curr->hash ^ searchHash);
		if (distance <= radius)
		{
			potential[distance] = push_char(potential[distance], curr->path);
		}
		int lower = fmax(distance - radius, 0);
		int upper = min(distance + radius, HASH_SIZE - 1);
		for (int i = lower; i <= upper; i++)
		{
			if (curr->children[i])
			{
				stack = push_image_node(stack, curr->children[i]);
			}
		}
	}
	int curr_dist = 0;
	while ((results == NULL || results->len < max) && curr_dist <= radius)
	{
		char *res = pop_char(&potential[curr_dist]);
		if (res == NULL)
		{
			curr_dist++;
		}
		else
		{
			results = push_back_char(results, res);
		}
	}
	return results;
}

void *index_images(void *args)
{
	struct ImageIndexArguments *arguments = args;

	ImageNode **root = arguments->root;
	size_t *completed = arguments->completed;
	if (!DirectoryExists("images"))
	{
		printf("No image directory\n");
		*arguments->done = true;
		pthread_exit(0);
	}
	FilePathList imageDirFiles = LoadDirectoryFiles("images");
	if (imageDirFiles.count < 1)
	{
		printf("No images found\n");
		*arguments->done = true;
		UnloadDirectoryFiles(imageDirFiles);
		pthread_exit(0);
	}
	int i = 0;
	Image firstImage;
	for (i = 0; i < imageDirFiles.count; i++)
	{
		firstImage = LoadImage(imageDirFiles.paths[i]);
		if (IsImageValid(firstImage))
		{
			*root = createImageNode(firstImage, imageDirFiles.paths[0]);
			UnloadImage(firstImage);
			break;
		}
		UnloadImage(firstImage);
	}
	*arguments->total = imageDirFiles.count;
	if (root == NULL)
	{
		printf("No valid images found\n");
		*arguments->done = true;
		UnloadDirectoryFiles(imageDirFiles);
		pthread_exit(0);
	}
	for (; i < imageDirFiles.count && !*arguments->kill; i++)
	{
		insertImage(*root, imageDirFiles.paths[i]);
		*arguments->completed = i;
	}
	UnloadDirectoryFiles(imageDirFiles);
	*arguments->done = true;
	pthread_exit(0);
}
//------------------------------------------------------------------------------------
// Program main entry point
//------------------------------------------------------------------------------------
int main(void)
{
	// Initialization
	//---------------------------------------------------------------------------------------
	int screenWidth = 680;
	int screenHeight = 420;

	InitWindow(screenWidth, screenHeight, "Tester");

	// layout_name: controls initialization
	//----------------------------------------------------------------------------------
	bool TextBox001EditMode = false;
	char TextBox001Text[128] = "";
	bool TextBox002EditMode = false;
	char TextBox002Text[128] = "";
	bool TextBox008EditMode = false;
	char TextBox008Text[128] = "";
	bool TextBox009EditMode = false;
	char TextBox009Text[128] = "";
	int CurrMaxResultSize = 256;
	char *SearchResultText = calloc(sizeof(char), CurrMaxResultSize);
	char EditDistanceResultText[128] = "";

	int SearchResultScrollIdx = 0;
	int SearchResultScrollActive = 0;

	// Indexing thread info
	size_t IndexingCompleted = 0;
	size_t IndexingTotal = 0;
	bool IndexingDone = false;
	bool IndexingRunning = false;
	bool KillIndexing = false;
	pthread_t IndexingThread;

	// Loading thread info
	size_t LoadingCompleted = 0;
	size_t LoadingTotal = 0;
	bool LoadingDone = false;
	bool LoadingRunning = false;
	bool KillLoading = false;
	pthread_t LoadingThread;

	size_t ImagesCompleted = 0;
	size_t ImagesTotal = 0;
	bool ImagesDone = false;
	bool ImagesRunning = false;
	bool KillImages = false;
	pthread_t ImagesThread;

	Node *root = NULL;
	ImageNode *imageRoot = NULL;
	//----------------------------------------------------------------------------------

	SetTargetFPS(60);
	GuiLoadStyleJungle();
	//--------------------------------------------------------------------------------------

	// Main game loop
	while (!WindowShouldClose()) // Detect window close button or ESC key
	{
		// Update
		//----------------------------------------------------------------------------------
		// TODO: Implement required update logic
		//----------------------------------------------------------------------------------

		if (IndexingDone)
		{
			pthread_join(IndexingThread, NULL);
			IndexingRunning = false;
			IndexingCompleted = 0;
			IndexingTotal = 0;
			IndexingDone = false;
			KillIndexing = false;
		}

		if (LoadingDone)
		{
			pthread_join(LoadingThread, NULL);
			LoadingCompleted = 0;
			LoadingTotal = 0;
			LoadingDone = false;
			LoadingRunning = false;
			KillLoading = false;
		}

		if (ImagesDone)
		{
			pthread_join(ImagesThread, NULL);
			ImagesCompleted = 0;
			ImagesTotal = 0;
			ImagesDone = false;
			ImagesRunning = false;
			KillImages = false;
		}

		if (IsFileDropped())
		{
			FilePathList dropped = LoadDroppedFiles();
			if (dropped.count != 1)
			{
				printf("Only drop 1 image at a time\n");
				UnloadDroppedFiles(dropped);
			}
			else
			{
				char *path = dropped.paths[0];
				Image image = LoadImage(path);
				if (!IsImageValid(image))
				{
					printf("Only .png supported for now\n");
					UnloadDroppedFiles(dropped);
				}
				else
				{
					memset(SearchResultText, 0, strlen(SearchResultText));
					int distance = atoi(TextBox009Text);
					if (!distance)
					{
						distance = 5;
					}
					CharStack *search_result = searchImages(imageRoot, image, distance, INT_MAX);
					int result_length = 0;
					while (search_result != NULL)
					{
						char *result = pop_char(&search_result);
						if (result_length + strlen(result) > CurrMaxResultSize)
						{
							CurrMaxResultSize *= 2;
							SearchResultText = realloc(SearchResultText, CurrMaxResultSize);
						}
						result_length += snprintf(SearchResultText + result_length, CurrMaxResultSize - result_length, "%s\n", result);
					}
				}
				UnloadImage(image);
			}
			UnloadDroppedFiles(dropped);
		}
		// Draw
		//----------------------------------------------------------------------------------
		BeginDrawing();

		ClearBackground(GetColor(GuiGetStyle(DEFAULT, BACKGROUND_COLOR)));

		// raygui: controls drawing
		//----------------------------------------------------------------------------------
		if (GuiTextBox((Rectangle){8, 34, 120, 24}, TextBox001Text, 128, TextBox001EditMode))
			TextBox001EditMode = !TextBox001EditMode;
		if (GuiTextBox((Rectangle){152, 34, 120, 24}, TextBox002Text, 128, TextBox002EditMode))
			TextBox002EditMode = !TextBox002EditMode;
		GuiStatusBar((Rectangle){524, 34, 150, 24}, EditDistanceResultText);
		GuiLabel((Rectangle){8, 10, 120, 24}, "Word 1");
		GuiLabel((Rectangle){152, 10, 120, 24}, "Word 2");
		if (GuiButton((Rectangle){304, 34, 195, 24}, "Calculate Edit Distance"))
		{
			sprintf(EditDistanceResultText, "Edit distance: %d", damerau_levenshtein_distance(TextBox001Text, TextBox002Text));
		}
		if (GuiButton((Rectangle){8, 106, 120, 24}, "Build BK-Tree"))
		{
			// Free old index if exists
			freeNode(root);
			struct IndexingArguments args;
			args.root = &root;
			args.completed = &IndexingCompleted;
			args.total = &IndexingTotal;
			args.done = &IndexingDone;
			args.kill = &KillIndexing;
			pthread_create(&IndexingThread, NULL, &create_tree, (void *)&args);
			IndexingRunning = true;
		}
		if (root == NULL && GuiGetState() != STATE_DISABLED)
		{
			GuiDisable();
			GuiButton((Rectangle){152, 106, 120, 24}, "Save BK-Tree");
			GuiEnable();
		}
		else if (GuiButton((Rectangle){152, 106, 120, 24}, "Save BK-Tree"))
			write_tree(root);

		if (GuiButton((Rectangle){304, 106, 120, 24}, "Load BK-Tree"))
		{
			freeNode(root);
			root = NULL;
			struct LoadingArguments args;
			args.root = &root;
			args.completed = &LoadingCompleted;
			args.total = &LoadingTotal;
			args.done = &LoadingDone;
			args.kill = &KillLoading;
			pthread_create(&LoadingThread, NULL, &load_tree, (void *)&args);
			LoadingRunning = true;
		}
		if (GuiTextBox((Rectangle){8, 186, 120, 24}, TextBox008Text, 128, TextBox008EditMode))
			TextBox008EditMode = !TextBox008EditMode;
		if (GuiTextBox((Rectangle){152, 186, 120, 24}, TextBox009Text, 128, TextBox009EditMode))
			TextBox009EditMode = !TextBox009EditMode;
		GuiLabel((Rectangle){8, 162, 120, 24}, "Search Term");
		GuiLabel((Rectangle){152, 162, 120, 24}, "Max edit distance");

		if (root == NULL && GuiGetState() != STATE_DISABLED)
		{
			GuiDisable();
			GuiButton((Rectangle){304, 186, 120, 24}, "Search");
			GuiEnable();
		}
		else if (GuiButton((Rectangle){304, 186, 120, 24}, "Search"))
		{
			memset(SearchResultText, 0, strlen(SearchResultText));
			int distance = atoi(TextBox009Text);
			if (!distance)
			{
				distance = 2;
			}
			CharStack *search_result = search(root, TextBox008Text, distance, INT_MAX);
			int result_length = 0;
			while (search_result != NULL)
			{
				char *result = pop_char(&search_result);
				if (result_length + strlen(result) > CurrMaxResultSize)
				{
					CurrMaxResultSize *= 2;
					SearchResultText = realloc(SearchResultText, CurrMaxResultSize);
				}
				result_length += snprintf(SearchResultText + result_length, CurrMaxResultSize - result_length, "%s\n", result);
			}
		}
		if (IndexingRunning && IndexingTotal != 0)
		{
			float progress = ((float)IndexingCompleted / (float)IndexingTotal);
			GuiEnable();
			GuiProgressBar((Rectangle){450, 106, 120, 24}, NULL, TextFormat("%i%%", (int)(progress * 100)), &progress, 0.0f, 1.0f);
			GuiDisable();
		}
		if (LoadingRunning && LoadingTotal != 0)
		{
			float progress = ((float)LoadingCompleted / (float)LoadingTotal);
			GuiEnable();
			GuiProgressBar((Rectangle){450, 106, 120, 24}, NULL, TextFormat("%i%%", (int)(progress * 100)), &progress, 0.0f, 1.0f);
			GuiDisable();
		}
		if (ImagesRunning && ImagesTotal != 0)
		{
			float progress = ((float)ImagesCompleted / (float)ImagesTotal);
			GuiEnable();
			GuiProgressBar((Rectangle){450, 106, 120, 24}, NULL, TextFormat("%i%%", (int)(progress * 100)), &progress, 0.0f, 1.0f);
			GuiDisable();
		}
		// GuiSetStyle(DEFAULT, TEXT_ALIGNMENT_VERTICAL, TEXT_ALIGN_TOP); // WARNING: Word-wrap does not work as expected in case of no-top alignment
		// GuiSetStyle(DEFAULT, TEXT_WRAP_MODE, TEXT_WRAP_WORD);
		GuiLabel((Rectangle){8, 220, 120, 24}, "Search Results");
		GuiListView((Rectangle){8, 250, 416, 160}, SearchResultText, &SearchResultScrollIdx, &SearchResultScrollActive);
		// GuiSetStyle(DEFAULT, TEXT_WRAP_MODE, TEXT_WRAP_NONE);
		// GuiSetStyle(DEFAULT, TEXT_ALIGNMENT_VERTICAL, TEXT_ALIGN_MIDDLE);

		if (GuiButton((Rectangle){450, 186, 120, 24}, "Image test"))
		{
			freeImageNode(imageRoot);
			imageRoot = NULL;
			struct ImageIndexArguments args;
			args.root = &imageRoot;
			args.completed = &ImagesCompleted;
			args.total = &ImagesTotal;
			args.done = &ImagesDone;
			args.kill = &KillImages;
			pthread_create(&ImagesThread, NULL, &index_images, (void *)&args);
			ImagesRunning = true;
			// index_images();
		}

		if (IndexingRunning || LoadingRunning || ImagesRunning)
			GuiDisable();
		else
			GuiEnable();
		//----------------------------------------------------------------------------------

		EndDrawing();
		//----------------------------------------------------------------------------------
	}

	// De-Initialization
	//--------------------------------------------------------------------------------------
	if (IndexingRunning)
	{
		KillIndexing = true;
		pthread_join(IndexingThread, NULL);
	}
	if (LoadingRunning)
	{
		KillLoading = true;
		pthread_join(LoadingThread, NULL);
	}
	if (ImagesRunning)
	{
		KillImages = true;
		pthread_join(ImagesThread, NULL);
	}
	freeNode(root);
	freeImageNode(imageRoot);
	free(SearchResultText);
	CloseWindow(); // Close window and OpenGL context
	//--------------------------------------------------------------------------------------

	return 0;
}
