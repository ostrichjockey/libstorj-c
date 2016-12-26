#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <getopt.h>

#include "storj.h"

extern int errno;

#define HELP_TEXT "usage: storj [<options>] <command> [<args>]\n\n"     \
    "These are common Storj commands for various situations:\n\n"       \
    "working with buckets and files\n"                                  \
    "  get-info\n"                                                      \
    "  list-buckets\n"                                                  \
    "  list-files <bucket-id>\n"                                        \
    "  add-bucket <name> \n\n"                                          \
    "downloading and uploading files\n"                                 \
    "  upload-file <bucket-id> <path>\n"                                \
    "  download-file <bucket-id> <file-id> <path>\n\n"                  \
    "options:\n\n"                                                      \
    "  -h, --help                output usage information\n"            \
    "  -V, --version             output the version number\n"           \
    "  -u, --url <url>           set the base url for the api\n\n"      \

void upload_file_progress(double progress)
{
    // TODO assersions
}

void upload_file_complete(int status)
{
    if (status != 0) {
        printf("Upload failure: %s\n", storj_strerror(status));
        exit(status);
    }

    printf("Upload Success!\n");
    exit(0);
}

static int upload_file(storj_env_t *env, char *bucket_id, char *file_path)
{
    char *mnemonic = getenv("STORJ_CLI_MNEMONIC");
    if (!mnemonic) {
        printf("Set your STORJ_CLI_MNEMONIC\n");
        exit(1);
        // "abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon about"
    }

    storj_upload_opts_t upload_opts = {
        .file_concurrency = 1,
        .shard_concurrency = 3,
        .bucket_id = bucket_id,
        .file_path = file_path,
        .key_pass = "password",
        .mnemonic = mnemonic
    };

    int status = storj_bridge_store_file(env, &upload_opts,
                                     upload_file_progress,
                                     upload_file_complete);

    return status;
}


static void download_file_progress(double progress)
{
    // TODO
}

static void download_file_complete(int status, FILE *fd)
{
    fclose(fd);
    if (status) {
        printf("Download failure: %s\n", storj_strerror(status));
        exit(status);
    }
    exit(0);
}

static int download_file(storj_env_t *env, char *bucket_id,
                         char *file_id, char *path)
{
    FILE *fd = fopen(path, "w+");

    if (fd == NULL) {
        printf("Unable to open %s: %s\n", path, strerror(errno));
        return 1;
    }

    int status = storj_bridge_resolve_file(env, bucket_id, file_id, fd,
                                           download_file_progress,
                                           download_file_complete);

    return status;

}

void get_info_callback(uv_work_t *work_req, int status)
{
    assert(status == 0);
    json_request_t *req = work_req->data;

    struct json_object *info;
    json_object_object_get_ex(req->response, "info", &info);

    struct json_object *title;
    json_object_object_get_ex(info, "title", &title);
    struct json_object *description;
    json_object_object_get_ex(info, "description", &description);
    struct json_object *version;
    json_object_object_get_ex(info, "version", &version);
    struct json_object *host;
    json_object_object_get_ex(req->response, "host", &host);

    printf("Title:       %s\n", json_object_to_json_string(title));
    printf("Description: %s\n", json_object_to_json_string(description));
    printf("Version:     %s\n", json_object_to_json_string(version));
    printf("Host:        %s\n", json_object_to_json_string(host));

    json_object_put(req->response);
    free(req);
    free(work_req);
}

int main(int argc, char **argv)
{
    int status = 0;

    static struct option cmd_options[] = {
        {"url", required_argument,  0, 'u'},
        {"version", no_argument,  0, 'V'},
        {"help", no_argument,  0, 'h'},
        {0, 0, 0, 0}
    };

    int index = 0;

    char *storj_bridge = getenv("STORJ_BRIDGE");
    int c;

    while ((c = getopt_long(argc, argv, "hVu:", cmd_options, &index)) != -1) {
        switch (c) {
            case 'u':
                storj_bridge = optarg;
                break;
            case 'V':
                printf("libstorj 1.0.0-alpha\n");
                exit(0);
                break;
            case 'h':
                printf(HELP_TEXT);
                exit(0);
                break;
        }
    }

    int command_index = optind;

    char *command = argv[command_index];
    if (!command) {
        printf(HELP_TEXT);
        status = 0;
        goto end_program;
    }

    if (!storj_bridge) {
        storj_bridge = "https://api.storj.io:443/";
    }

    printf("Using Storj bridge: %s\n\n", storj_bridge);

    // Parse the host, part and proto from the storj bridge url
    char proto[6];
    char host[100];
    int port = 443;
    sscanf(storj_bridge, "%5[^://]://%99[^:/]:%99d", proto, host, &port);

    // Get the bridge user
    char *user = getenv("STORJ_BRIDGE_USER");
    if (!user) {
        char *user_input;
        size_t user_input_size = 1024;
        size_t num_chars;
        user_input = calloc(user_input_size, sizeof(char));
        if (user_input == NULL) {
            printf("Unable to allocate buffer");
            exit(1);
        }
        printf("Username (email): ");
        num_chars = getline(&user_input, &user_input_size, stdin);
        user = calloc(num_chars, sizeof(char));
        memcpy(user, user_input, num_chars * sizeof(char));
    }

    // Get the bridge password
    char *pass = getenv("STORJ_BRIDGE_PASS");
    if (!pass) {
        char *pass_input = getpass("Password: ");

        // TODO hash password

        pass = pass_input;
    }

    storj_bridge_options_t options = {
        .proto = proto,
        .host  = host,
        .port  = port,
        .user  = user,
        .pass  = pass
    };

    // initialize event loop and environment
    storj_env_t *env = storj_init_env(&options, NULL);
    if (!env) {
        status = 1;
        goto end_program;
    }

    if (strcmp(command, "download-file") == 0) {
        char *bucket_id = argv[command_index + 1];
        char *file_id = argv[command_index + 2];
        char *path = argv[command_index + 3];

        if (!bucket_id || !file_id || !path) {
            printf(HELP_TEXT);
            status = 1;
            goto end_program;
        }

        if (download_file(env, bucket_id, file_id, path)) {
            status = 1;
            goto end_program;
        }
    } else if (strcmp(command, "upload-file") == 0) {
        char *bucket_id = argv[command_index + 1];
        char *path = argv[command_index + 2];

        if (!bucket_id || !path) {
            printf(HELP_TEXT);
            status = 1;
            goto end_program;
        }

        if (upload_file(env, bucket_id, path)) {
            status = 1;
            goto end_program;
        }

    } else if (strcmp(command, "get-info") == 0) {
        storj_bridge_get_info(env, get_info_callback);
    }  else {
        printf(HELP_TEXT);
        status = 1;
        goto end_program;
    }

    // run all queued events
    if (uv_run(env->loop, UV_RUN_DEFAULT)) {
        status = 1;
        goto end_program;
    }

    // shutdown
    int uv_status = uv_loop_close(env->loop);
    if (uv_status == UV_EBUSY) {
        status = 1;
        goto end_program;
    }

end_program:
    if (env) {
        free(env->loop);
        free(env);
    }
    return status;
}