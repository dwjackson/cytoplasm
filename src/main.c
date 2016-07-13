/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

/*
 * Copyright (c) 2016 David Jackson
 */

#include "layout.h"
#include "cytoplasm_header.h"
#include "string_util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <pthread.h>
#include <ctache/ctache.h>
#include <libgen.h>

#define USAGE "Usage: cyto [COMMAND]"

#define NUM_WORKERS 4
#define SITE_DIR "_site"
#define DEFAULT_CONTENT_LENGTH 1024
#define LAYOUT "layout"

static void
get_file_list(const char *dir_name,
              char ***file_names_ptr,
              int *num_files_ptr, 
              char ***directories_ptr,
              int *num_directories_ptr)
{
    DIR *dir = opendir(dir_name);
    if (dir == NULL) {
        fprintf(stderr, "ERROR: Could not open current directory\n");
        exit(EXIT_FAILURE);
    }

    int num_files = 0;
    int num_directories = 0;
    struct dirent *de;
    struct stat statbuf;
    while ((de = readdir(dir)) != NULL) {
        char *file_path = malloc(strlen(dir_name) + 1 + de->d_namlen + 1);
        strcpy(file_path, dir_name);
        strcat(file_path, "/");
        strcat(file_path, de->d_name);
        char *file_name = de->d_name;
        if (stat(file_path, &statbuf) == -1) {
            fprintf(stderr, "ERROR: Could not stat %s\n", file_name);
            exit(EXIT_FAILURE);
        }
        if (!S_ISDIR(statbuf.st_mode)
            && file_name[0] != '_'
            && file_name[0] != '.') {
            num_files++;
        } else if (S_ISDIR(statbuf.st_mode)
                   && file_name[0] != '_'
                   && file_name[0] != '.') {
            num_directories++;
        }
        free(file_path);
    }
    *num_files_ptr = num_files;
    *num_directories_ptr = num_directories;
    rewinddir(dir);

    char **file_names = malloc(sizeof(char*) * num_files);
    char **directory_names = malloc(sizeof(char*) * num_directories);
    int index = 0;
    int dir_index = 0;
    while ((de = readdir(dir)) != NULL) {
        char *file_path = malloc(strlen(dir_name) + 1 + de->d_namlen + 1);
        strcpy(file_path, dir_name);
        strcat(file_path, "/");
        strcat(file_path, de->d_name);
        char *file_name = de->d_name;
        if (stat(file_path, &statbuf) == -1) {
            fprintf(stderr, "ERROR: Could not stat %s\n", file_name);
            exit(EXIT_FAILURE);
        }
        if (!S_ISDIR(statbuf.st_mode)
            && file_name[0] != '_'
            && file_name[0] != '.') {
            file_names[index] = strdup(file_path);
            index++;
        } else if (S_ISDIR(statbuf.st_mode)
                   && file_name[0] != '_'
                   && file_name[0] != '.') {
            directory_names[dir_index] = strdup(file_name);
            dir_index++;
        }
        free(file_path);
    }

    closedir(dir);
    *file_names_ptr = file_names;
    *directories_ptr = directory_names;
}

static char
*read_file_contents(FILE *fp)
{
    char *content;
    size_t content_length;
    size_t content_bufsize;
    int ch;

    content_bufsize = DEFAULT_CONTENT_LENGTH ;
    content = malloc(content_bufsize);
    content_length = 0;
    while ((ch = fgetc(fp)) != EOF) {
        if (content_length + 1 >= content_bufsize) {
            content_bufsize *= 2;
            content = realloc(content, content_bufsize);
        }
        content[content_length] = ch;
        content_length++;
    }
    content[content_length] = '\0';
    return content;
}

static char
*file_extension(const char *file_name)
{
    size_t file_name_len;
    int i;
    char ch;
    size_t extension_len;
    char *extension;
    int start_index;

    file_name_len = strlen(file_name);
    extension_len = 0;
    for (i = file_name_len - 1; i >= 0; i--) {
        ch = file_name[i];
        if (ch != '.') {
            extension_len++;
        } else {
            start_index = i;
            break;
        }
    }

    extension = malloc(extension_len + 1);
    memset(extension, 0, extension_len + 1);
    for (i = start_index; i < file_name_len; i++) {
        ch = file_name[i];
        extension[i - start_index] = ch;
    }

    return extension;
}

struct process_file_args {
    int start_index;
    int end_index;
    char **file_names;
    ctache_data_t *data;
    pthread_mutex_t *data_mutex;
    struct layout *layouts;
    int num_layouts;
    const char *site_dir;
};

static void
*process_file(void *args_ptr)
{
    struct process_file_args *args = (struct process_file_args *)args_ptr;
    int i;
    const char *site_dir = args->site_dir;
    char *in_file_name;
    char *in_file_extension;
    char *out_file_name;
    for (i = args->start_index; i < args->end_index; i++) {
        in_file_name = (args->file_names)[i];
        in_file_extension = file_extension(in_file_name);

        char *in_file_name_dup = strdup(in_file_name);
        char *in_file_base_name = basename(in_file_name_dup);
        size_t out_file_name_len;
        out_file_name_len = strlen(site_dir) + 1 + strlen(in_file_base_name);
        out_file_name = malloc(out_file_name_len + 1);
        strcpy(out_file_name, site_dir);
        strcat(out_file_name, "/");
        strcat(out_file_name, in_file_base_name);
        if (out_file_name == NULL) {
            fprintf(stderr, "ERROR: Could not malloc()\n");
            break;
        }

        FILE *in_fp = fopen(in_file_name, "r");
        if (in_fp != NULL) {
            ctache_data_t *file_data = ctache_data_create_hash();
            cytoplasm_header_read(in_fp, file_data);
            FILE *out_fp = fopen(out_file_name, "w");
            if (out_fp != NULL) {
                if (!ctache_data_hash_table_has_key(file_data, LAYOUT)) {
                    /* Render the file as a ctache template */
                    ctache_render_file(in_fp, out_fp, file_data, ESCAPE_HTML);
                } else {
                    /*
                     * Render the layout with the file content passed as a
                     * partial with the key "content"
                     */
                    char *content = read_file_contents(in_fp);
                    size_t content_len = strlen(content);
                    ctache_data_t *content_data;
                    content_data = ctache_data_create_string(content,
                                                             content_len);
                    ctache_data_hash_table_set(file_data,
                                               "content",
                                               content_data);
                    ctache_data_t *layout_data;
                    layout_data = ctache_data_hash_table_get(file_data, LAYOUT);
                    char *layout_name = string_trim(layout_data->data.string);
                    char *layout = get_layout_content(args->layouts,
                                                      args->num_layouts,
                                                      layout_name);
                    if (layout == NULL) {
                        char *err_fmt = "ERROR: Layout not found: \"%s\"\n";
                        fprintf(stderr, err_fmt, layout_name);
                        abort();
                    }
                    size_t layout_len = strlen(layout);
                    ctache_render_string(layout,
                                         layout_len,
                                         out_fp,
                                         file_data,
                                         ESCAPE_HTML);
                    free(layout_name);
                }

                /* Clean up the file pointer */
                fclose(out_fp);
                out_fp = NULL;
            } else {
                char *err_fmt = "ERROR: Could not open output file: %s\n";
                fprintf(stderr, err_fmt, out_file_name);
            }
            fclose(in_fp);
            pthread_mutex_lock(args->data_mutex);
            ctache_data_hash_table_set(args->data, in_file_name, file_data);
            pthread_mutex_unlock(args->data_mutex);
        } else {
            char *err_fmt = "ERROR: Could not open input file %s\n";
            fprintf(stderr, err_fmt, in_file_name);
        }
        free(out_file_name);
        free(in_file_extension);
        free(in_file_name_dup);
    }

    return NULL;
}

static void
_generate(const char *curr_dir_name, const char *site_dir)
{
    mkdir(site_dir, 0770);

    char **file_names;
    int num_files;
    char **directories;
    int num_directories;
    get_file_list(curr_dir_name,
                  &file_names,
                  &num_files,
                  &directories,
                  &num_directories);
    int num_layouts;
    struct layout *layouts = get_layouts(&num_layouts);

    /* Set up the data */
    ctache_data_t *data = ctache_data_create_hash();
    pthread_mutex_t data_mutex;
    pthread_mutex_init(&data_mutex, NULL);

    int files_per_worker = num_files / NUM_WORKERS;
    pthread_t thr_pool[NUM_WORKERS];
    struct process_file_args threads_args[NUM_WORKERS];

    /* Create workers */
    int i;
    for (i = 0; i < NUM_WORKERS; i++) {
        threads_args[i].start_index = i * files_per_worker;
        if (i + 1 < NUM_WORKERS) {
            threads_args[i].end_index = threads_args[i].start_index
                + files_per_worker;
        } else {
            threads_args[i].end_index = num_files;
        }
        threads_args[i].file_names = file_names;
        threads_args[i].data = data;
        threads_args[i].data_mutex = &data_mutex;
        threads_args[i].layouts = layouts;
        threads_args[i].num_layouts = num_layouts;
        threads_args[i].site_dir = site_dir;
        pthread_create(&(thr_pool[i]), NULL, process_file, &(threads_args[i]));
    }

    /* Wait for workers to finish */
    for (i = 0; i < NUM_WORKERS; i++) {
        pthread_join(thr_pool[i], NULL);
    }

    /* Process the subdirectories, recursively */
    for (i = 0; i < num_directories; i++) {
        char *directory = directories[i];

        size_t subdir_len = strlen(curr_dir_name) + 1 + strlen(directory);
        char *subdir = malloc(subdir_len + 1);
        strcpy(subdir, curr_dir_name);
        strcat(subdir, "/");
        strcat(subdir, directory);

        size_t site_subdir_len = strlen(site_dir) + 1 + strlen(directory);
        char *site_subdir = malloc(site_subdir_len + 1);
        strcpy(site_subdir, site_dir);
        strcat(site_subdir, "/");
        strcat(site_subdir, directory);
        _generate(subdir, site_subdir);

        free(site_subdir);
        free(subdir);
    }

    /* Cleanup */
    pthread_mutex_destroy(&data_mutex);
    ctache_data_destroy(data);
    layouts_destroy(layouts, num_layouts);
    for (i = 0; i < num_files; i++) {
        free(file_names[i]);
    }
    free(file_names);
    free(directories);
}

static void
cmd_generate(const char *curr_dir_name, const char *site_dir)
{
    _generate(curr_dir_name, site_dir);
}

static void
cmd_initialize(const char *project_name)
{
    if (mkdir(project_name, 0770) != 0) {
        char *err_fmt = "ERROR: Could not create project directory: %s\n";
        fprintf(stderr, err_fmt, project_name);
    }
}
    
int
main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "%s\n", USAGE);
        exit(EXIT_FAILURE);
    }

    char *cmd = argv[1];
    if (strcmp(cmd, "g") == 0
        || strcmp(cmd, "gen") == 0
        || strcmp(cmd, "generate") == 0) {
        cmd_generate(".", SITE_DIR);
    } else if (strcmp(cmd, "init") == 0) {
        if (argc == 3) {
            char *proj_name = argv[2];
            cmd_initialize(proj_name);
        } else {
            fprintf(stderr, "ERROR: No project name given\n");
            exit(EXIT_FAILURE);
        }
    } else {
        fprintf(stderr, "Unrecognized command: %s\n", cmd);
        exit(EXIT_FAILURE);
    }

    return 0;
}
