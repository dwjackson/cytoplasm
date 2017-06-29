/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

/*
 * Copyright (c) 2016 David Jackson
 */

/* Linux's feature-test macros can break other systems, turn on all features */
#ifdef __linux__
#define _GNU_SOURCE
#endif /* __linux__ */

#include "common.h"
#include "processing.h"
#include "cytogen_header.h"
#include "cymkd.h"
#include "string_util.h"
#include "config.h"
#include "cyto_config.h"
#include "feed.h"
#include "initialize.h"
#include "generate.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <pthread.h>
#include <ctache/ctache.h>
#include <ftw.h>
#include <libgen.h>

#define USAGE "Usage: cyto [FLAGS] [COMMAND]"

#define DEFAULT_NUM_WORKERS 4
#define SITE_DIR "_site"
#define POSTS_DIR "_posts"
#define SITE_POSTS_DIR "_site/posts"
#define MAXFDS 100

/* NOTE: This function is not thread-safe */
static int
_rename_posts(const char *path,
              const struct stat *stat_ptr,
              int flag,
              struct FTW *ftw)
{
    char *in_path;
    char *dir;
    char index_html[] = "index.html";
    char *out_path;

    if (flag == FTW_F) {
        in_path = strdup(path);
        dir = dirname(in_path);
        out_path = malloc(strlen(dir) + 1 + strlen(index_html) + 1);
        strcpy(out_path, dir);
        strcat(out_path, "/");
        strcat(out_path, index_html);
        rename(path, out_path);
        free(out_path);
        free(in_path);
    }

    return 0;
}

static void
cmd_generate(struct cyto_config *config,
             const char *curr_dir_name,
             const char *site_dir,
             int num_workers)
{
    ctache_data_t *data;
    pthread_mutex_t data_mutex;
    struct stat statbuf;
    bool has_posts;
    struct generate_arguments args;

    /* Set up the data */
    data = ctache_data_create_hash();
    pthread_mutex_init(&data_mutex, NULL);

    /* Set up the posts data */
    has_posts = stat(POSTS_DIR, &statbuf) == 0 && statbuf.st_mode & S_IFDIR;
    ctache_data_t *posts_array = NULL;
    if (has_posts) {
        posts_array = ctache_data_create_array(0);
        ctache_data_hash_table_set(data, "posts", posts_array);
    }

    /* Set up the generation arguments */
    args.curr_dir_name = curr_dir_name;
    args.site_dir = site_dir;
    args.num_workers = num_workers;
    args.data = data;
    args.data_mutex = &data_mutex;
    args.process = process_files;

    /* Perform the generation */
    if (has_posts) {
        args.curr_dir_name = POSTS_DIR;
        args.process = process_post_files;

        generate(&args);
        nftw(SITE_POSTS_DIR, _rename_posts, MAXFDS, 0);

        args.process = process_files;
        args.curr_dir_name = curr_dir_name;
    }
    generate(&args);

    /* Create the Atom/RSS feed file */
    if (config != NULL) {
        generate_feed(config, posts_array);
    }

    /* Clean up */
    pthread_mutex_destroy(&data_mutex);
    ctache_data_destroy(data);
}

int
_clean(const char *path, const struct stat *stat, int flag, struct FTW *ftw_ptr)
{
    if (flag == FTW_DP) {
        rmdir(path);
    } else if (flag == FTW_F) {
        unlink(path);
    } else {
        fprintf(stderr, "Unrecognized file type for %s\n", path);
        return -1;
    }
    return 0;
}

void
cmd_clean()
{
    nftw(SITE_DIR, _clean, 1000, FTW_DEPTH); 
}

static void
print_help()
{
    printf("%s\n", USAGE);
    printf("Flags:\n");
    printf("\t-h Print this help message\n");
    printf("\t-V Print the version number\n");
    printf("\t-j [THREADS] Set number of worker threads (default is 4)\n");
    printf("Commands:\n");
    printf("\tgenerate - Generate a site from the current directory\n");
    printf("\tinit [PROJECT_NAME] - Initialize a cytogen project\n");
    printf("\tclean - Remove generated site files\n");
}

int
main(int argc, char *argv[])
{
    int num_workers;
    char **args;
    int opt;
    extern char *optarg;
    extern int optind;

    num_workers = 0;
    while ((opt = getopt(argc, argv, "hVj:")) != -1) {
        switch (opt) {
        case 'h':
            print_help();
            exit(EXIT_SUCCESS);
            break;
        case 'V':
            printf("cyto %s\n", PACKAGE_VERSION);
            exit(EXIT_SUCCESS);
            break;
        case 'j':
            num_workers = atoi(optarg);
            break;
        default:
            exit(EXIT_FAILURE);
        }
    }
    if (optind >= argc) {
        printf("%s\n", USAGE);
        exit(EXIT_FAILURE);
    }
    args = argv + optind;

    if (num_workers < 1) {
        num_workers = DEFAULT_NUM_WORKERS;
    }

    struct cyto_config config;
    bool has_config = false;
    struct stat statbuf;
    int stat_retval = stat(CONFIG_FILE_NAME, &statbuf);
    has_config = stat_retval == 0;
    if (has_config) {
        cyto_config_read(CONFIG_FILE_NAME, &config);
    }

    char *cmd = args[0];
    if (string_matches_any(cmd, 3, "g", "gen", "generate")) {
        if (has_config) {
            cmd_generate(&config, ".", SITE_DIR, num_workers);
        } else {
            cmd_generate(NULL, ".", SITE_DIR, num_workers);
        }
    } else if (string_matches_any(cmd, 1, "i", "init")) {
        if (argc == 3) {
            char *proj_name = args[1];
            cmd_initialize(proj_name);
        } else {
            fprintf(stderr, "ERROR: No project name given\n");
            exit(EXIT_FAILURE);
        }
    } else if (string_matches_any(cmd, 2, "c", "clean")) { 
        cmd_clean();
    } else {
        fprintf(stderr, "Unrecognized command: %s\n", cmd);
        exit(EXIT_FAILURE);
    }

    if (has_config) {
        cyto_config_destroy(&config);
    }

    return 0;
}
