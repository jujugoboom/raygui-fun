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

*/

#include "raylib.h"

#define RAYGUI_IMPLEMENTATION
#include "raygui.h"

#include "resource_dir.h" // utility header for SearchAndSetResourceDir

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/types.h>
#include <pthread.h>

#define MAX_CHAR 256  // Assuming the alphabet size is at most 256
#define MAX_NODES 100 // Adjust this based on the expected number of nodes
// Node structure for the BK-Tree
typedef struct Node
{
	char *word;
	struct Node *children[MAX_CHAR];
} Node;

typedef struct NodeStack
{
	struct Node *head;
	struct NodeStack *next;
} NodeStack;

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
} IndexingArguments;

// Function to create a new node
Node *
createNode(char *word)
{
	Node *newNode = (Node *)malloc(sizeof(Node));
	newNode->word = strdup(word); // Allocate memory for the word
	for (int i = 0; i < MAX_CHAR; i++)
	{
		newNode->children[i] = NULL;
	}
	return newNode;
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
	while (to_insert)
	{
		// printf("Inserting %s", to_insert);
		*completed += strlen(to_insert) + 1;
		insert(*root, trim(to_insert));
		to_insert = strtok(NULL, "\n");
	}
	*arguments->done = true;
	pthread_exit(0);
}

//------------------------------------------------------------------------------------
// Program main entry point
//------------------------------------------------------------------------------------
int main()
{
	// Initialization
	//---------------------------------------------------------------------------------------
	int screenWidth = 800;
	int screenHeight = 450;

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
	char SearchResultText[1024] = "";
	char EditDistanceResultText[128] = "";
	size_t IndexingCompleted = 0;
	size_t IndexingTotal = 0;
	bool IndexingDone = false;
	bool IndexingRunning = false;
	pthread_t IndexingThread;

	Node *root = NULL;
	//----------------------------------------------------------------------------------

	SetTargetFPS(60);
	//--------------------------------------------------------------------------------------

	// Main game loop
	while (!WindowShouldClose()) // Detect window close button or ESC key
	{
		// Update
		//----------------------------------------------------------------------------------
		// TODO: Implement required update logic
		//----------------------------------------------------------------------------------

		// Draw
		//----------------------------------------------------------------------------------
		BeginDrawing();

		ClearBackground(GetColor(GuiGetStyle(DEFAULT, BACKGROUND_COLOR)));

		if (IndexingDone)
		{
			pthread_join(IndexingThread, NULL);
			IndexingRunning = false;
		}

		// raygui: controls drawing
		//----------------------------------------------------------------------------------
		if (GuiTextBox((Rectangle){8, 64, 120, 24}, TextBox001Text, 128, TextBox001EditMode))
			TextBox001EditMode = !TextBox001EditMode;
		if (GuiTextBox((Rectangle){152, 64, 120, 24}, TextBox002Text, 128, TextBox002EditMode))
			TextBox002EditMode = !TextBox002EditMode;
		GuiTextBox((Rectangle){478, 64, 120, 24}, EditDistanceResultText, 128, false);
		GuiLabel((Rectangle){8, 40, 120, 24}, "Word 1");
		GuiLabel((Rectangle){152, 40, 120, 24}, "Word 2");
		if (GuiButton((Rectangle){304, 64, 150, 24}, "Calculate Edit Distance"))
		{
			sprintf(EditDistanceResultText, "Edit distance: %d", damerau_levenshtein_distance(TextBox001Text, TextBox002Text));
		}
		if (GuiButton((Rectangle){8, 136, 120, 24}, "Build BK-Tree"))
		{
			struct IndexingArguments args;
			args.root = &root;
			args.completed = &IndexingCompleted;
			args.total = &IndexingTotal;
			args.done = &IndexingDone;
			pthread_create(&IndexingThread, NULL, &create_tree, (void *)&args);
			IndexingRunning = true;
		}
		if (GuiTextBox((Rectangle){8, 216, 120, 24}, TextBox008Text, 128, TextBox008EditMode))
			TextBox008EditMode = !TextBox008EditMode;
		if (GuiTextBox((Rectangle){152, 216, 120, 24}, TextBox009Text, 128, TextBox009EditMode))
			TextBox009EditMode = !TextBox009EditMode;
		GuiLabel((Rectangle){8, 192, 120, 24}, "Search Term");
		GuiLabel((Rectangle){152, 192, 120, 24}, "Max edit distance");
		if (GuiButton((Rectangle){304, 216, 120, 24}, "Search"))
		{
			memset(SearchResultText, 0, strlen(SearchResultText));

			CharStack *search_result = search(root, TextBox008Text, 2, 20);
			while (search_result != NULL)
			{
				char *result = pop_char(&search_result);
				char *withnl = malloc(sizeof(char) * (strlen(result) + 4));
				sprintf(withnl, "%s -- ", result);
				strcat(SearchResultText, withnl);
			}
		}
		if (IndexingRunning && IndexingTotal != 0)
		{
			float progress = ((float)IndexingCompleted / (float)IndexingTotal);
			GuiEnable();
			GuiProgressBar((Rectangle){450, 216, 120, 24}, NULL, TextFormat("%i%%", (int)(progress * 100)), &progress, 0.0f, 1.0f);
			GuiDisable();
		}
		GuiSetStyle(DEFAULT, TEXT_ALIGNMENT_VERTICAL, TEXT_ALIGN_TOP); // WARNING: Word-wrap does not work as expected in case of no-top alignment
		GuiSetStyle(DEFAULT, TEXT_WRAP_MODE, TEXT_WRAP_WORD);
		GuiTextBox((Rectangle){8, 272, 416, 160}, SearchResultText, 1024, false);
		GuiSetStyle(DEFAULT, TEXT_WRAP_MODE, TEXT_WRAP_NONE);
		GuiSetStyle(DEFAULT, TEXT_ALIGNMENT_VERTICAL, TEXT_ALIGN_MIDDLE);

		if (IndexingRunning)
			GuiDisable();
		else
			GuiEnable();
		//----------------------------------------------------------------------------------

		EndDrawing();
		//----------------------------------------------------------------------------------
	}

	// De-Initialization
	//--------------------------------------------------------------------------------------
	CloseWindow(); // Close window and OpenGL context
	//--------------------------------------------------------------------------------------

	return 0;
}
