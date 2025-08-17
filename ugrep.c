#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include <ctype.h>
#include <pthread.h>
#include <stdarg.h>

#define BLK "\x1b[30m"
#define RED "\x1b[31m"
#define GRN "\x1b[32m"
#define YEL "\x1b[33m"
#define BLU "\x1b[34m"
#define MAG "\x1b[35m"
#define CYN "\x1b[36m"
#define WHT "\x1b[37m"

#define RST "\x1b[0m"

#define FILE_SAMPLE_SIZE 4096
#define BINARY_THRESHOLD 0.30
#define SPAN_SIZE 64

pthread_mutex_t threadMutex;
pthread_mutex_t printfMutex;
pthread_cond_t threadWorkerCond;
pthread_cond_t threadMainCond;

size_t numThreads = 1;
bool *busyThreads;

typedef struct ThreadContext {
    int id;
    char *term;
} ThreadContext;

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
    char *term;
    pthread_mutex_t mutex;
} Queue;

ThreadContext *createThreadContext() {
    ThreadContext *newThreadContext = malloc(sizeof(ThreadContext));
    newThreadContext->id = 0;
    newThreadContext->term = NULL;
    return newThreadContext;
}

void freeThreadContext(ThreadContext *threadContext) {
    free(threadContext->term);
    free(threadContext);
}

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
    pthread_mutex_init(&newQueue->mutex, NULL);
    return newQueue;
}

void freeQueue(Queue *queue) {
    if (queue == NULL) return;

    pthread_mutex_lock(&queue->mutex);

    while(queue->head != NULL) {
        Node *tmp = queue->head;
        queue->head = tmp->next;
        freeNode(tmp);
    }

    pthread_mutex_unlock(&queue->mutex);
    pthread_mutex_destroy(&queue->mutex);

    free(queue);
}

void enqueue(Queue *queue, const char* path) {
    pthread_mutex_lock(&queue->mutex);

    Node *newNode = createNode(path);

    if (queue->head == NULL) {
        queue->head = newNode;
        queue->tail = newNode;
        queue->size++;
        pthread_mutex_unlock(&queue->mutex);
        return;
    }

    queue->tail->next = newNode;
    queue->tail = newNode;
    queue->size++;

    pthread_mutex_unlock(&queue->mutex);
}

char* dequeue(Queue *queue) {
    pthread_mutex_lock(&queue->mutex);

    if (queue->head == NULL) {
        pthread_mutex_unlock(&queue->mutex);
        return NULL;
    }

    Node *head = queue->head;
    char *data = strdup(head->path);
    queue->head = head->next;
    queue->size--;

    freeNode(head);
    pthread_mutex_unlock(&queue->mutex);

    return data;
}

Queue *gDirQueue;
Queue *gFileQueue;

void safePrintf(const char *format, ...) {
    pthread_mutex_lock(&printfMutex);

    va_list args;
    va_start(args, format);

    vprintf(format, args);
    va_end(args);
    fflush(stdout);

    pthread_mutex_unlock(&printfMutex);
}

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


        closedir(dir);
    }
}

int isTextFile(const char *filename) {
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
        pthread_mutex_lock(&printfMutex);
        printf(BLU"%s\n"RST, path);
        for(int i = 0; i < spanPos; i++) {
            printf(YEL"%d"RST": %s\n", spans[i].line, spans[i].span);
        }
        printf("\n");
        pthread_mutex_unlock(&printfMutex);
    }

    free(spans);
    free(line);
}

int work(const char *term, int id) {
    int count = 0;

    // Process the files first
    char *filePath;
    while((filePath = dequeue(gFileQueue)) != NULL) {
        processFile(term, filePath);
        free(filePath);
        count++;
    }

    char *dirPath;
    if((dirPath = dequeue(gDirQueue)) != NULL) {
        processDir(dirPath);
        free(dirPath);
        count++;
    }

    return count;
}

void *workerFunction(void *arg) {
    ThreadContext *context = (ThreadContext*)arg;
    int totalProcessedEntriesCount = 0;

    while(1) {
        int processedEntriesCount = work(context->term, context->id);
        totalProcessedEntriesCount += processedEntriesCount;

        pthread_mutex_lock(&threadMutex);

        if (processedEntriesCount == 0) {
            busyThreads[context->id] = false;
        }

        bool hasBusyThreads = false;
        for (int i = 0; i < numThreads; i++) {
            if (busyThreads[i]) hasBusyThreads = true;
        }

        if (!hasBusyThreads) {
            pthread_mutex_unlock(&threadMutex);
            break;
        }

        pthread_mutex_unlock(&threadMutex);
    }

    freeThreadContext(context);
    return NULL;
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

    long numProcs = sysconf(_SC_NPROCESSORS_ONLN);
    if (numProcs < 1) {
        numThreads = 1;
    } else {
        numThreads = numProcs;
    }

    pthread_t threads[numThreads];

    pthread_mutex_init(&threadMutex, NULL);
    pthread_mutex_init(&printfMutex, NULL);
    pthread_cond_init(&threadWorkerCond, NULL);
    pthread_cond_init(&threadMainCond, NULL);

    busyThreads = malloc(numThreads * sizeof(bool));

    for (int i = 0; i < numThreads; i++) {
        ThreadContext *threadContext = createThreadContext();
        threadContext->id = i;
        threadContext->term = strdup(term);
        busyThreads[i] = true;

        pthread_create(&threads[i], NULL, workerFunction, threadContext);
    }

    for (int i = 0; i < numThreads; i++) {
        pthread_join(threads[i], NULL);
    }

    pthread_mutex_destroy(&threadMutex);
    pthread_cond_destroy(&threadMainCond);
    pthread_cond_destroy(&threadWorkerCond);

    free(path);
    free(term);
    free(busyThreads);
    freeQueue(gDirQueue);
    freeQueue(gFileQueue);
    return EXIT_SUCCESS;
}