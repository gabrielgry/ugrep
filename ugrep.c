#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include <ctype.h>

#define FILE_SAMPLE_SIZE 4096
#define BINARY_THRESHOLD 0.30

#define SPAN_SIZE 64

typedef struct Span {
    char span[SPAN_SIZE];
    int line;
} Span;

typedef struct Node {
    char *path;
    struct Node *next;
} Node;

typedef struct Queue {
    Node *head;
    Node *tail;
    int size;
} Queue;

Node *createNode(const char *path) {
    Node *newNode = malloc(sizeof(Node));
    newNode->path = strdup(path);
    newNode->next = NULL;
    return newNode;
}

void freeNode(Node *node) {
    if (node == NULL) return;
    free(node->path);
    free(node);
}

Queue *createQueue() {
    Queue *newQueue = malloc(sizeof(Queue));
    newQueue->head = NULL;
    newQueue->tail = NULL;
    newQueue->size = 0;
    return newQueue;
}

void freeQueue(Queue *queue) {
    if (queue == NULL) return;

    while(queue->head != NULL) {
        Node *tmp = queue->head;
        queue->head = tmp->next;
        free(tmp);
    }

    free(queue);
}

void enqueue(Queue *queue, const char* path) {
    Node *newNode = createNode(path);

    if (queue->head == NULL) {
        queue->head = newNode;
        queue->tail = newNode;
        queue->size++;
        return;
    }

    queue->tail->next = newNode;
    queue->tail = newNode;
    queue->size++;
}

char* dequeue(Queue *queue) {
    if (queue->head == NULL) return NULL;

    Node *head = queue->head;
    char *data = strdup(head->path);
    queue->head = head->next;
    queue->size--;

    freeNode(head);

    return data;
}

bool isEmpty(Queue *queue) {
    return queue->size == 0;
}

Queue *gDirQueue;
Queue *gFileQueue;

void dispatchPath(const char *path) {
    struct stat fileStat;

    if (lstat(path, &fileStat) == -1) {
        perror("stat");
    }

    if (S_ISDIR(fileStat.st_mode)) {
        enqueue(gDirQueue, path);
    } else if (S_ISREG(fileStat.st_mode)) {
        enqueue(gFileQueue, path);
    }
}

void processDir(const char *path) {
    struct dirent *dirEntry;
    DIR *dir = opendir(path);

    if (dir) {
        while((dirEntry = readdir(dir)) != NULL) {
            if (strcmp(dirEntry->d_name, ".") == 0 || strcmp(dirEntry->d_name, "..") == 0)
                continue;

            char entryPath[PATH_MAX];

            if (strcmp(path, "/") == 0)
                snprintf(entryPath, sizeof(entryPath), "%s%s", path, dirEntry->d_name);
            else
                snprintf(entryPath, sizeof(entryPath), "%s/%s", path, dirEntry->d_name);

            dispatchPath(entryPath);
        }
    }
    closedir(dir);
}

int isTextFile(const char *filename) {
    struct stat fileStat;

    FILE *f = fopen(filename, "rb");
    if (!f) return -1;

    unsigned char buf[FILE_SAMPLE_SIZE];
    size_t n = fread(buf, 1, FILE_SAMPLE_SIZE, f);
    fclose(f);

    if (n == 0) return -1;

    int suspicious = 0;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = buf[i];

        if (c == 0) {
            suspicious++;
            continue;
        }

        if (!(isprint(c) || isspace(c))) {
            suspicious++;
        }
    }

    double ratio = (double)suspicious / n;
    return ratio < BINARY_THRESHOLD;
}

void processFile(const char *term, const char *path) {
    if (isTextFile(path) <= 0) return;

    FILE *file = fopen(path, "rb");
    if (!file) return;

    size_t maxSpanCount = 256;
    size_t spanPos = 0;
    Span *spans = malloc(maxSpanCount * sizeof(Span));

    char *line = NULL;
    size_t len = 0;
    int lineCount = 0;

    while(getline(&line, &len, file) != -1) {
        lineCount++;

        char *newLine = strchr(line, '\n');
        if (newLine) {
            *newLine = '\0';
        }

        char *foundTerm = strstr(line, term);

        if (foundTerm != NULL) {
            if (spanPos >= maxSpanCount) {
                maxSpanCount *= 2;
                spans = realloc(spans, maxSpanCount * sizeof(Span));
            }

            strncpy(spans[spanPos].span, foundTerm, SPAN_SIZE - 1);
            spans[spanPos].span[SPAN_SIZE - 1] = '\0';
            spans[spanPos].line = lineCount;
            spanPos++;
        }
    }
    fclose(file);

    if (spanPos) {
        printf("%s\n", path);
        for(int i = 0; i < spanPos; i++) {
            printf("%d: %s\n", spans[i].line, spans[i].span);
        }
        printf("\n");
    }

    free(spans);
    free(line);
}

void work(const char *term) {
    // Process the files first
    while(!isEmpty(gFileQueue) || !isEmpty(gDirQueue)) {
        while(!isEmpty(gFileQueue)) {
            char *filePath = dequeue(gFileQueue);
            processFile(term, filePath);
            free(filePath);
        }

        if(!isEmpty(gDirQueue)) {
            char *dirPath = dequeue(gDirQueue);
            processDir(dirPath);
            free(dirPath);
        }
    }
}

void help() {
    puts("usage: ugrep term path/to/target");
}

void parseInput(int argc, char **argv, char **term, char **path) {
    if (argc != 3) {
        help();
        exit(EXIT_SUCCESS);
    }

    *term = strdup(argv[1]);

    if ((*path = realpath(argv[2], NULL)) == NULL) {
        perror("realpath");
        exit(EXIT_FAILURE);
    };
}

int main(int argc, char** argv) {
    gDirQueue = createQueue();
    gFileQueue = createQueue();

    char *path;
    char *term;

    parseInput(argc, argv, &term, &path);

    dispatchPath(path);
    work(term);

    free(path);
    free(term);
    freeQueue(gDirQueue);
    freeQueue(gFileQueue);
    return EXIT_SUCCESS;
}
