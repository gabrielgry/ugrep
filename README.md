# ugrep - Concurrent Search Utility

`ugrep` is a command-line tool, similar to `grep`, for concurrently searching for a string in files. It is written in C and uses threads to optimize searching on multi-processor systems.

## Features

* **Concurrent Search**: Utilizes a thread pool, sized according to the number of available processor cores, to process files and directories in parallel.

* **Recursive Search**: Recursively navigates through directories from the specified path.

* **Binary Detection**: Attempts to identify and ignore binary files, focusing the search on text files only.

* **Colored Output**: Formats the output with colors to make the results easier to read.

## Compilation

The project uses the `pthread` library, so it's necessary to link it during compilation.

```
gcc -o ugrep main.c -lpthread -std=c99

```

## Usage

Run the program by providing the search term and the path (file or directory) where the search should be performed.

```
./ugrep <term> <path>

```

### Arguments

* `<term>`: The string you want to find.

* `<path>`: The initial directory or file for the search.

### Example

```
./ugrep "main" .

```

This command will search for the string "main" in all text files in the current directory and its subdirectories.

## How It Works

The application uses a producer-consumer model with two main queues: one for directories and one for files.

1. The initial path is added to the appropriate queue.

2. The worker threads are started.

3. Each thread attempts to consume an item from one of the queues:

   * If it's a **file**, the thread scans it for the term. If found, it prints the path, line number, and a snippet of the content.

   * If it's a **directory**, the thread lists its contents and adds the new files and subdirectories to the corresponding queues.

4. The process ends when all queues are empty and all threads are idle. Mutexes are used to ensure safe access to the queues and standard output (`stdout`).
