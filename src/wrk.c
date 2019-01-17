// Copyright (C) 2012 - Will Glozer.  All rights reserved.

#include "wrk.h"
#include "script.h"
#include "server_perf.h"
#include "main.h"

#define MAX_URL_LENGTH 2048
#define JSON_FILE_DIR "report"

static uint64_t start_thread_time = 0;
static bool thread_concurrency;

static struct config {
    uint64_t connections;
    uint64_t duration;
    uint64_t interval;
    uint64_t threads;
    uint64_t timeout;
    uint64_t pipeline;
    bool     delay;
    bool     dynamic;
    bool     latency;
    char    *host;
    char    *script;
    char    *json_template_file;
    char    json_file[256];
    SSL_CTX *ctx;
} cfg;

static struct {
    stats *latency;
    stats *requests;
} statistics;

static struct resultForm {
    uint64_t complete;
    char *connections;
    char *time;
    char *bytes;
    char *latency_max;
    char *latency_mean;
    char *latency_stdev;
    char *latency_PorNstdev;
    char *rps_max;
    char *rps_mean;
    char *rps_stdev;
    char *rps_PorNstdev;
    char *req_per_s;
    char *bytes_per_s;
 } result = {0, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL};

static struct sock sock = {
    .connect  = sock_connect,
    .close    = sock_close,
    .read     = sock_read,
    .write    = sock_write,
    .readable = sock_readable
};

static struct http_parser_settings parser_settings = {
    .on_message_complete = response_complete
};

static volatile sig_atomic_t stop = 0;

static void handler(int sig) {
    stop = 1;
}

static char *g_html_template;

static void usage() {
    printf("Usage: wrk <options> <url>                            \n"
           "  Options:                                            \n"
           "    -c, --connections <N>  Connections to keep open   \n"
           "    -d, --duration    <T>  Duration of test           \n"
           "    -i, --interval    <T>  Request sampling interval  \n"
           "    -t, --threads     <N>  Number of threads to use   \n"
           "                                                      \n"
           "    -s, --script      <S>  Load Lua script file       \n"
           "    -j, --json        <S>  Load json data for script  \n"
           "    -H, --header      <H>  Add header to request      \n"
           "        --latency          Print latency statistics   \n"
           "    -v, --version          Print version details      \n"
           "                                                      \n"
           "  Numeric arguments may include a SI unit (1k, 1M, 1G)\n"
           "  Time arguments may include a time unit (2s, 2m, 2h)\n");
}

static char *get_template(const char *template_name) {
    struct stat s;
    if (stat(template_name, &s) == -1 || s.st_size == 0)
        return NULL;

    FILE *fp = fopen(template_name, "r");
    if (fp == NULL)
        return NULL;

    char *template = (char *)malloc(sizeof(char) * s.st_size + 1);
    fread(template, s.st_size, sizeof(char), fp);
    fclose(fp);

    template[s.st_size * sizeof(char)] = 0;

    return template;
}

static bool save_template(const char *dst_path, const char *buffer, int buffer_size) {
    FILE *fd = fopen(dst_path, "w");
    if (fd == NULL) {          
            fprintf(stderr, "fopen %s failed:get last error:%d\n", dst_path, errno);
            return false;          
        }

    bool result = true;        
    int written_size = fwrite(buffer, sizeof(char), buffer_size, fd); 
    if (written_size != buffer_size) { 
            fprintf(stderr, "fwrite %s failed:get last error:%d\n", dst_path, errno);
            result = false;        
        }                          
    fclose(fd);                

    return result;             
} 

static void decide_thread_num(struct config *cfg) {
    if (cfg->connections < 500)
        cfg->threads = 1;
    else {
        int cpu_num = get_nprocs();
        cfg->threads = cfg->connections/500 < cpu_num ? cfg->connections/500 : cpu_num;
    }
}

static bool build_test_file(const char *template_path, const char *url) {
    bool result = false;
    json_error_t error;
    json_t *template_json = json_load_file(template_path, 0, &error);
    if (template_json == NULL)
        goto END;

    if (url != NULL && strlen(url) > 0)
        json_object_set(template_json, "url", json_string(url));

	const char *p = strrchr(template_path, '/');
	if (p == NULL || p + 1 == 0)
		p = template_path;
	else
		p++;

    char dst_file[256];
    sprintf(dst_file, "%s/%s", JSON_FILE_DIR, p);
    if (json_dump_file(template_json, dst_file, JSON_INDENT(4)) == 0)
        result = true;
END:
    json_decref(template_json);
    return result;
}

static bool build_mixed_file(const char *url, char **file_list_link) {
    bool result = false;
    json_error_t error;
    json_t *template_json = json_load_file(cfg.json_template_file, 0, &error);
    if (template_json == NULL) {
        fprintf(stderr, "load json file (%s) failed\n", cfg.json_template_file);
        goto END;
    }

    json_t *mixed_test_json = json_object_get(template_json, "mixed_test");
    if (mixed_test_json == NULL) {
        fprintf(stderr, "cannot get mixed_test field in %s\n", cfg.json_template_file);
        goto END;
    }

    int file_num = json_array_size(mixed_test_json);
    for (int i = 0; i < file_num; i++) {
        json_t *test_json = json_array_get(mixed_test_json, i);
        if (test_json == NULL) {
            fprintf(stderr, "get array from %s failed\n", cfg.json_template_file);
            goto END;
        }

        const char *file_path = json_string_value_of_name(test_json, "file");
        if (file_path == NULL) {
            fprintf(stderr, "cannot get file field in %s\n", cfg.json_template_file);
            goto END;
        }

        if (!build_test_file(file_path, url)) {
            fprintf(stderr, "build json file failed %s\n", file_path);
            goto END;
        }

		const char *p = strrchr(file_path, '/');
		if (p == NULL || p + 1 == 0)
			p = file_path;
		else
			p++;

		char dst_file[256];
		sprintf(dst_file, "%s/%s", JSON_FILE_DIR, p);
		aprintf(file_list_link, "<div><a href=\"%s\">%s</a></div>", p, p);
		json_object_set(test_json, "file", json_string(dst_file));
    }

    if (url != NULL && strlen(url) != 0) {
        if (json_object_set_new_nocheck(template_json, "url", json_string(url)) == -1)
            goto END;
    }

    if (json_dump_file(template_json, cfg.json_file, JSON_INDENT(4)) == 0)
        result = true;

END:
    json_decref(template_json);
    return result;
}

static bool build_test_data(char *url) {
    char *p = strrchr(cfg.json_template_file, '/');
    if (p == NULL)
        return false;

    char *file_list_link = NULL;
    aprintf(&file_list_link, "<div><a href=\"%s\">%s</a></div>", p, p);

    bool result = false;
    if (strncmp(p + 1, "mixed_", strlen("mixed_")) == 0)
        result = build_mixed_file(url, &file_list_link);
    else
        result = build_test_file(cfg.json_template_file, url);

    print_test_parameter(url, file_list_link);
    free(file_list_link);
    return result;
}

int main(int argc, char **argv) {
    thread_concurrency = false;
    char url[MAX_URL_LENGTH] = {0};
    char **headers = zmalloc(argc * sizeof(char *));
    struct http_parser_url parts = {};

    if (access("report", F_OK) != 0 && mkdir("report", 0775) != 0)
        goto FAILED;

    system("cp template/* report -rf");

    g_html_template = get_template("report/template.html");
    if (g_html_template == NULL) {
        fprintf(stderr, "Cannot open HTML template");
        free(g_html_template);
        goto FAILED;
    }

    if (parse_args(&cfg, url, headers, argc, argv)) {
        usage();
        goto FAILED;
    }

    if (strlen(url) == 0) {
        const char *wrk_url = getenv("WRK_URL");
        if (wrk_url != NULL)
            strncpy(url, wrk_url, strlen(wrk_url));
    }

    if (!build_test_data(url))
        goto FAILED;

    decide_thread_num(&cfg);

    lua_State *L = script_create(cfg.script, cfg.json_file, url, headers);
    if (strlen(url) == 0 || !script_parse_url(url, &parts)) {
        fprintf(stderr, "invalid URL: %s\n", url);
        goto FAILED;
    }
    char *schema  = copy_url_part(url, &parts, UF_SCHEMA);
    char *host    = copy_url_part(url, &parts, UF_HOST);
    char *port    = copy_url_part(url, &parts, UF_PORT);
    char *service = port ? port : schema;

    if (!script_resolve(L, host, service)) {
        char *msg = strerror(errno);
        fprintf(stderr, "unable to connect to %s:%s %s\n", host, service, msg);
        goto FAILED;
    }

    cfg.host = host;

    CollectConfig collectCfg;
    collectCfg.result  = startCollecting(cfg.host, cfg.duration, cfg.interval, NULL, collectCfg.hash_string);
    if (!collectCfg.result)
        fprintf(stderr, "start collecting failed");
    collectCfg.start_time = time(NULL);

    if (!strncmp("https", schema, 5)) {
        if ((cfg.ctx = ssl_init()) == NULL) {
            fprintf(stderr, "unable to initialize SSL\n");
            ERR_print_errors_fp(stderr);
            goto FAILED;
        }
        sock.connect  = ssl_connect;
        sock.close    = ssl_close;
        sock.read     = ssl_read;
        sock.write    = ssl_write;
        sock.readable = ssl_readable;
    }

    signal(SIGPIPE, SIG_IGN);
    signal(SIGINT,  SIG_IGN);

    statistics.latency  = stats_alloc(cfg.timeout * 1000);
    statistics.requests = stats_alloc(MAX_THREAD_RATE_S);
    thread *threads     = zcalloc(cfg.threads * sizeof(thread));

    for (uint64_t i = 0; i < cfg.threads; i++) {
        thread *t      = &threads[i];
        t->loop        = aeCreateEventLoop(10 + cfg.connections * 3);
        t->connections = cfg.connections / cfg.threads;
        memset(t->errors.code, 0, sizeof(t->errors.code));

        t->L = script_create(cfg.script, cfg.json_file, url, headers);
        script_init(L, t, argc - optind, &argv[optind]);

        if (i == 0) {
            cfg.pipeline = script_verify_request(t->L);
            cfg.dynamic  = !script_is_static(t->L);
            cfg.delay    = script_has_delay(t->L);
            if (script_want_response(t->L)) {
                parser_settings.on_header_field = header_field;
                parser_settings.on_header_value = header_value;
                parser_settings.on_body         = response_body;
            }
        }

        if (!t->loop || pthread_create(&t->thread, NULL, &thread_main, t)) {
            char *msg = strerror(errno);
            fprintf(stderr, "unable to create thread %"PRIu64": %s\n", i, msg);
            goto FAILED;
        }
    }

    struct sigaction sa = {
        .sa_handler = handler,
        .sa_flags   = 0,
    };
    sigfillset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);

    start_thread_time = time_us();
    uint64_t complete = 0;
    uint64_t bytes    = 0;
    errors errors     = { 0 };
    thread_concurrency = true;

    sleep(cfg.duration);
    stop = 1;

    for (uint64_t i = 0; i < cfg.threads; i++) {
        thread *t = &threads[i];
        pthread_join(t->thread, NULL);

        complete += t->complete;
        bytes    += t->bytes;

        errors.connect += t->errors.connect;
        errors.read    += t->errors.read;
        errors.write   += t->errors.write;
        errors.timeout += t->errors.timeout;
        errors.status  += t->errors.status;

        int count = sizeof(errors.code)/sizeof(errors.code[0]);
        for (int j = 0; j < count; j++) {
            if (t->errors.code != 0)
                errors.code[j] += t->errors.code[j];
        }
    }

    uint64_t runtime_us = time_us() - start_thread_time;
    long double runtime_s   = runtime_us / 1000000.0;
    long double req_per_s   = complete   / runtime_s;
    long double bytes_per_s = bytes      / runtime_s;

    result.complete = complete;
    result.time = format_time_us(runtime_us);
    result.bytes = format_binary(bytes);
    result.req_per_s = format_metric(req_per_s);
    result.bytes_per_s = format_binary(bytes_per_s);
    result.connections = format_metric(cfg.connections);
    statistics_rps(statistics.requests);

    print_test_result(&result, &errors);
    print_result_details(&result, &errors);

    print_stats_error_code(&errors);
    print_stats_requests(statistics.requests);
    print_stats_latency_map(statistics.latency);

    if (complete / cfg.connections > 0) {
        int64_t interval = runtime_us / (complete / cfg.connections);
        stats_correct(statistics.latency, interval);
    }

    print_stats("Latency", statistics.latency, format_time_us);
    print_stats("Req/Sec", statistics.requests, format_metric);
    print_stats_latency(statistics.latency);
    
    print_result_form();

    if (collectCfg.result) {
        json_t* responseJson = stopCollecting(cfg.host, collectCfg.hash_string);
        if (responseJson != NULL)
            print_dstServerPerformance(&collectCfg, responseJson);
        else
            printf("stopCollecting failed\n");

        json_decref(responseJson);
    }

    if (script_has_done(L)) {
        script_summary(L, runtime_us, complete, bytes);
        script_errors(L, &errors);
        script_done(L, statistics.latency, statistics.requests);
    }


    clear_unused_variable();
    save_template("report/log.html", g_html_template, strlen(g_html_template));
    free(g_html_template);

    return 0;

FAILED:
    zfree(headers);
    return 1;
}

void *thread_main(void *arg) {
    thread *thread = arg;

    char *request = NULL;
    size_t length = 0;

    if (!cfg.dynamic) {
        script_request(thread->L, &request, &length);
    }

    thread->cs = zcalloc(thread->connections * sizeof(connection));
    connection *c = thread->cs;
    for (uint64_t i = 0; i < thread->connections; i++, c++) {
        c->thread = thread;
        c->ssl     = cfg.ctx ? SSL_new(cfg.ctx) : NULL;
        c->request = request;
        c->length  = length;
        c->delayed = cfg.delay;
        connect_socket(thread, c);
    }

    aeEventLoop *loop = thread->loop;

    while(!loop->stop) {
        if (thread_concurrency) {
            aeCreateTimeEvent(loop, RECORD_INTERVAL_MS, record_rate, thread, NULL);
            thread->start = time_us();
            aeMain(loop);
        }else
            usleep(100);
    }

    aeDeleteEventLoop(loop);
    zfree(thread->cs);

    return NULL;
}

static int connect_socket(thread *thread, connection *c) {
    struct addrinfo *addr = thread->addr;
    struct aeEventLoop *loop = thread->loop;
    int fd, flags;

    fd = socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol);

    flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    if (connect(fd, addr->ai_addr, addr->ai_addrlen) == -1) {
        if (errno != EINPROGRESS) goto error;
    }

    flags = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &flags, sizeof(flags));

    flags = AE_READABLE | AE_WRITABLE;
    if (aeCreateFileEvent(loop, fd, flags, socket_connected, c) == AE_OK) {
        c->parser.data = c;
        c->fd = fd;
        return fd;
    }

  error:
    thread->errors.connect++;
    close(fd);
    return -1;
}

static int reconnect_socket(thread *thread, connection *c) {
    aeDeleteFileEvent(thread->loop, c->fd, AE_WRITABLE | AE_READABLE);
    sock.close(c);
    close(c->fd);
    return connect_socket(thread, c);
}

static int record_rate(aeEventLoop *loop, long long id, void *data) {
    thread *thread = data;
    if (thread->succ > 0) {
        uint64_t time_interval = (time_us() - start_thread_time) / 1000 / 1000;
        stats_record_requests_per_sec(statistics.requests, false, thread->succ, time_interval);
        thread->succ = 0;
    }

    if (thread->requests > 0) {
        uint64_t time_interval = (time_us() - start_thread_time) / 1000 / 1000;
        stats_record_requests_per_sec(statistics.requests, true, thread->requests, time_interval);

        thread->requests = 0;
        thread->start    = time_us();
    }

    if (stop) aeStop(loop);

    return RECORD_INTERVAL_MS;
}

static int delay_request(aeEventLoop *loop, long long id, void *data) {
    connection *c = data;
    c->delayed = false;
    aeCreateFileEvent(loop, c->fd, AE_WRITABLE, socket_writeable, c);
    return AE_NOMORE;
}

static int header_field(http_parser *parser, const char *at, size_t len) {
    connection *c = parser->data;
    if (c->state == VALUE) {
        *c->headers.cursor++ = '\0';
        c->state = FIELD;
    }
    buffer_append(&c->headers, at, len);
    return 0;
}

static int header_value(http_parser *parser, const char *at, size_t len) {
    connection *c = parser->data;
    if (c->state == FIELD) {
        *c->headers.cursor++ = '\0';
        c->state = VALUE;
    }
    buffer_append(&c->headers, at, len);
    return 0;
}

static int response_body(http_parser *parser, const char *at, size_t len) {
    connection *c = parser->data;
    buffer_append(&c->body, at, len);
    return 0;
}

static int response_complete(http_parser *parser) {
    connection *c = parser->data;
    thread *thread = c->thread;
    uint64_t now = time_us();
    int status = parser->status_code;

    thread->complete++;
    thread->requests++;

    if (status > 399) {
        thread->errors.code[status] += 1;
        thread->errors.status++;
    }else {
        thread->succ++;
    }

    if (c->headers.buffer) {
        *c->headers.cursor++ = '\0';
        script_response(thread->L, status, &c->headers, &c->body);
        c->state = FIELD;
    }

    if (--c->pending == 0) {
        if (!stats_record(statistics.latency, now - c->start)) {
            thread->errors.timeout++;
        }
        c->delayed = cfg.delay;
        aeCreateFileEvent(thread->loop, c->fd, AE_WRITABLE, socket_writeable, c);
    }

    if (!http_should_keep_alive(parser)) {
        reconnect_socket(thread, c);
        goto done;
    }

    http_parser_init(parser, HTTP_RESPONSE);

  done:
    return 0;
}

static void socket_connected(aeEventLoop *loop, int fd, void *data, int mask) {
    connection *c = data;

    switch (sock.connect(c, cfg.host)) {
        case OK:    break;
        case ERROR: goto error;
        case RETRY: return;
    }

    http_parser_init(&c->parser, HTTP_RESPONSE);
    c->written = 0;

    aeCreateFileEvent(c->thread->loop, fd, AE_READABLE, socket_readable, c);
    aeCreateFileEvent(c->thread->loop, fd, AE_WRITABLE, socket_writeable, c);

    return;

  error:
    c->thread->errors.connect++;
    reconnect_socket(c->thread, c);
}

static void socket_writeable(aeEventLoop *loop, int fd, void *data, int mask) {
    connection *c = data;
    thread *thread = c->thread;

    if (c->delayed) {
        uint64_t delay = script_delay(thread->L);
        aeDeleteFileEvent(loop, fd, AE_WRITABLE);
        aeCreateTimeEvent(loop, delay, delay_request, c, NULL);
        return;
    }

    if (!c->written) {
        if (cfg.dynamic) {
            script_request(thread->L, &c->request, &c->length);
        }
        c->start   = time_us();
        c->pending = cfg.pipeline;
    }

    char  *buf = c->request + c->written;
    size_t len = c->length  - c->written;
    size_t n;

    status r = sock.write(c, buf, len, &n);
    if (cfg.dynamic) {
        free(c->request);
        c->request = NULL;
    }

    switch (r) {
        case OK:    break;
        case ERROR: goto error;
        case RETRY: return;
    }

    c->written += n;
    if (c->written == c->length) {
        c->written = 0;
        aeDeleteFileEvent(loop, fd, AE_WRITABLE);
    }

    return;

  error:
    thread->errors.write++;
    reconnect_socket(thread, c);
}

static void socket_readable(aeEventLoop *loop, int fd, void *data, int mask) {
    connection *c = data;
    size_t n;

    do {
        switch (sock.read(c, &n)) {
            case OK:    break;
            case ERROR: goto error;
            case RETRY: return;
        }

        if (http_parser_execute(&c->parser, &parser_settings, c->buf, n) != n) goto error;
        if (n == 0 && !http_body_is_final(&c->parser)) goto error;

        c->thread->bytes += n;
    } while (n == RECVBUF && sock.readable(c) > 0);

    return;

  error:
    c->thread->errors.read++;
    reconnect_socket(c->thread, c);
}

static uint64_t time_us() {
    struct timeval t;
    gettimeofday(&t, NULL);
    return (t.tv_sec * 1000000) + t.tv_usec;
}

static char *copy_url_part(char *url, struct http_parser_url *parts, enum http_parser_url_fields field) {
    char *part = NULL;

    if (parts->field_set & (1 << field)) {
        uint16_t off = parts->field_data[field].off;
        uint16_t len = parts->field_data[field].len;
        part = zcalloc(len + 1 * sizeof(char));
        memcpy(part, &url[off], len);
    }

    return part;
}

static struct option longopts[] = {
    { "connections", required_argument, NULL, 'c' },
    { "duration",    required_argument, NULL, 'd' },
    { "json",        required_argument, NULL, 'j' },
    { "script",      required_argument, NULL, 's' },
    { "header",      required_argument, NULL, 'H' },
    { "latency",     no_argument,       NULL, 'L' },
    { "timeout",     required_argument, NULL, 'T' },
    { "help",        no_argument,       NULL, 'h' },
    { "version",     no_argument,       NULL, 'v' },
    { NULL,          0,                 NULL,  0  }
};

static int parse_args(struct config *cfg, char *url, char **headers, int argc, char **argv) {
    char **header = headers;
    int c;

    memset(cfg, 0, sizeof(struct config));
    cfg->threads     = 1;
    cfg->interval    = 1;
    cfg->connections = 10;
    cfg->duration    = 10;
    cfg->timeout     = SOCKET_TIMEOUT_MS;

    while ((c = getopt_long(argc, argv, "c:i:d:j:l:s:H:T:Lrv?", longopts, NULL)) != -1) {
        switch (c) {
            case 'c':
                if (scan_metric(optarg, &cfg->connections)) return -1;
                break;
            case 'i':
                if (scan_metric(optarg, &cfg->interval)) return -1;
                break;
            case 'd':
                if (scan_time(optarg, &cfg->duration)) return -1;
                break;
            case 's':
                cfg->script = optarg;
                break;
            case 'j':
                cfg->json_template_file = optarg;
                char *p = strrchr(optarg, '/');
                sprintf(cfg->json_file, "%s/%s", JSON_FILE_DIR, p + 1);
                break;
            case 'H':
                *header++ = optarg;
                break;
            case 'L':
                cfg->latency = true;
                break;
            case 'T':
                if (scan_time(optarg, &cfg->timeout)) return -1;
                cfg->timeout *= 1000;
                break;
            case 'v':
                printf("wrk %s [%s] ", VERSION, aeGetApiName());
                printf("Copyright (C) 2012 Will Glozer\n");
                break;
            case 'h':
            case '?':
            case ':':
            default:
                return -1;
        }
    }

    if (!cfg->threads || !cfg->duration) return -1;

    if (cfg->script == NULL && cfg->json_template_file != NULL)
        cfg->script = "/usr/local/lib/visual_wrk/multi_requests.lua";

    if (!cfg->connections || cfg->connections < cfg->threads) {
        fprintf(stderr, "number of connections must be >= threads\n");
        return -1;
    }

    if (optind != argc) {
        if (MAX_URL_LENGTH < strlen(argv[optind])) {
            fprintf(stderr, "Url length exceeds %d!\n", MAX_URL_LENGTH);
            return -1;
        }

        memcpy(url, argv[optind], strlen(argv[optind]));
    }

    *header = NULL;

    return 0;
}

char *str_replace(char *orig, const char *rep, const char *with) {
    char *result; // the return string
    char *ins;    // the next insert point
    char *tmp;    // varies
    int len_rep;  // length of rep (the string to remove)
    int len_with; // length of with (the string to replace rep with)
    int len_front; // distance between rep and end of last rep
    int count;    // number of replacements

    //sanity checks and initialization
    if (!orig || !rep)
        return NULL;

    len_rep = strlen(rep);
    if (len_rep == 0)
        return NULL; // empty rep causes infinite loop during count

    if (!with)
        with = "";

    len_with = strlen(with);

    // count the number of replacements needed
    ins = orig;
    for (count = 0; (tmp = strstr(ins, rep)); ++count) {
        ins = tmp + len_rep;
    }

    tmp = result = malloc(strlen(orig) + (len_with - len_rep) * count + 1);

    if (!result)
        return NULL;

    while (count--) {
        ins = strstr(orig, rep);
        len_front = ins - orig;
        tmp = strncpy(tmp, orig, len_front) + len_front;
        tmp = strcpy(tmp, with) + len_with;
        orig += len_front + len_rep;
    }
    strcpy(tmp, orig);

    return result;
}

static void record_html_log(char *key, char *value) {
    char *p = str_replace(g_html_template, key, value);
    free(g_html_template);
    g_html_template = p;
}

static bool get_unused_variable(char *variable) {
    char *start = strstr(g_html_template, "${");
    if (start == NULL)
        return false;

    char *end = strstr(start, "}");
    if (end == NULL)
        return false;

    int variable_size = end - start + 1;
    strncpy(variable, start, variable_size);
    variable[variable_size] = 0;
    return true;
}

static void clear_unused_variable() {
    char unused_variable[256];
    record_html_log("${cpu_num}", "0");
    while(get_unused_variable(unused_variable)) {
        record_html_log(unused_variable, " ");
    }
}

static void print_stats_error_code(errors *errors) {
    char buff[1024];
    if (errors->status == 0) {
        snprintf(buff, sizeof(buff), "Error code: none\n");
        goto END;
    }

    int offset = 0;
    snprintf(buff, sizeof(buff), "Error code: \n");
    int count = sizeof(errors->code)/sizeof(errors->code[0]);
    for (int i = 0; i < count; i++) {
        if (errors->code[i] != 0) {
            offset = strlen(buff);
            snprintf(buff + offset, sizeof(buff), "  %u:%u\n", i, errors->code[i]);
        }
    }

END:
    record_html_log("${error_codes}", buff);
}

uint64_t digit_ceil(uint64_t x) {  
    int len=0;  
    while(x) {  
        x/=10;  
        len++;  
        if (x < 10)
            break;
    }  
    return (x + 1) * pow(10, len);
}  

static void print_stats_latency_map(stats *stats) {
    uint64_t max = digit_ceil(stats->max);
    uint64_t interval = max/20;
    if (interval == 0)
        return ;

    int latency_array[21];
    memset(latency_array, 0, sizeof(latency_array));
    char *x_coordinate = NULL;
    char *y_coordinate = NULL;
    aprintf(&x_coordinate, "labels: [");
    aprintf(&y_coordinate, "data: [");

    for (uint64_t num = 0; num < max; num++) {
        int pos = num/interval;
        if (pos < 20 && pos >= 0)
            latency_array[pos] += stats->data[num];
        else
            fprintf(stderr, "lantency position cross the border!\n");
    }

    char *time;
    for (int i = 0; i <= 20; i++) {
        time = format_time_us(interval * i);
        aprintf(&x_coordinate, "'%s', ", time);
        aprintf(&y_coordinate, "%d, ", latency_array[i]);
        free(time);
    }

    aprintf(&x_coordinate, "]");
    aprintf(&y_coordinate, "]");
    record_html_log("${latency_chart_label}", x_coordinate);
    record_html_log("${latency_chart_data}", y_coordinate);
    free(x_coordinate);
    free(y_coordinate);
}

static void statistics_rps(stats *stats) {
    uint64_t requests_num = 0;
    uint64_t requests = 0;

    for (uint64_t i = 0; i <= stats->max_location; i++) {
        requests += stats->requests[i];
        requests_num++;

        if (requests_num == cfg.interval) {
            stats_record(statistics.requests, (double)requests/cfg.interval);
            requests = 0;
            requests_num = 0;
        }
    }

    if (requests_num != 0 && requests_num != cfg.interval)
        stats_record(statistics.requests, (double)requests/requests_num);
}

static void print_stats_requests(stats *stats) {
    char buff[1024];
    char *units = cfg.interval == 1 ? "second" : "seconds";
    snprintf(buff, sizeof(buff), "requests / %lu %s\n", cfg.interval, units);

    uint64_t requests = 0;
    uint64_t success = 0;
    uint64_t requests_num = 0;

    char *rps_data = NULL;
    char timeArray[40];
    for (uint64_t i = 0; i <= stats->max_location; i++) {
        requests += stats->requests[i];
        success += stats->success[i];
        requests_num++;

        if (requests_num == cfg.interval) {
            time_t time = start_thread_time/1000/1000 + i;
            strftime(timeArray, sizeof(timeArray) - 1, "%F %T", localtime(&time));
            aprintf(&rps_data, "\n{\"date\":\"%s\", \"requests\":%Lf, \"success\":%Lf},", 
                    timeArray, (long double)requests/cfg.interval, (long double)success/cfg.interval);
            success = 0;
            requests = 0;
            requests_num = 0;
        }
    }

    record_html_log("${requests_frequency}", buff);
    record_html_log("${rps_chart_data}", rps_data);
    record_html_log("${rps_chart_div}", "<div id=\"rps_chart\"></div>");
    free(rps_data);
}

static void print_stats(char *name, stats *stats, char *(*fmt)(long double)) {
    uint64_t max = stats->max;
    long double mean  = stats_mean(stats);
    long double stdev = stats_stdev(stats, mean);
    long double PorNstdev = stats_within_stdev(stats, mean, stdev, 1);

    if (strcmp("Latency", name) == 0) {
        result.latency_max = fmt(max);
        result.latency_mean  = fmt(mean);
        result.latency_stdev = fmt(stdev);
        result.latency_PorNstdev = format_metric(PorNstdev/100);
    }else if (strcmp("Req/Sec", name) == 0) {
        result.rps_max = fmt(max);
        result.rps_mean  = fmt(mean);
        result.rps_stdev = fmt(stdev);
        result.rps_PorNstdev = format_metric(PorNstdev/100);
    }
}

static void print_stats_latency(stats *stats) {
    if (!cfg.latency) 
        record_html_log("${latency_distribution}", "Latency Distribution\n");

    long double percentiles[] = { 50.0, 66.0, 75.0, 80.0, 90.0, 95.0, 98.0, 99.0, 100.0 };
    char buff[1024];
    int offset = 0;
    snprintf(buff, sizeof(buff), "Latency Distribution\n");
    offset = strlen(buff);
    for (size_t i = 0; i < sizeof(percentiles) / sizeof(long double); i++) {
        long double p = percentiles[i];
        uint64_t n = stats_percentile(stats, p);
        char *time_us = format_time_us(n);
        snprintf(buff + offset, sizeof(buff), "%5.0Lf%% %s\n", p, time_us);
        offset = strlen(buff);
        free(time_us);
    }
    record_html_log("${latency_distribution}", buff);
}

static void print_result_form() {
    struct resultForm *o = &result;
    record_html_log("${concurrency}", o->connections);
    record_html_log("${duration}", o->time);
    record_html_log("${tps}", o->bytes_per_s);
    record_html_log("${rps}", o->req_per_s);

    record_html_log("${latency_avg}", o->latency_mean);
    record_html_log("${latency_max}", o->latency_max);
    record_html_log("${latency_stdev}", o->latency_stdev);
    record_html_log("${latency_PorNstdev}", o->latency_PorNstdev);

    record_html_log("${rps_avg}", o->rps_mean);
    record_html_log("${rps_max}", o->rps_max);
    record_html_log("${rps_stdev}", o->rps_stdev);
    record_html_log("${rps_PorNstdev}", o->rps_PorNstdev);
}

static void print_test_parameter(const char *url, char *file_list_link) {
    record_html_log("${file_list_link}", file_list_link);

    char buff[1024];
    char *time = format_time_s(cfg.duration);
    format_time_str(&time);
    snprintf(buff, sizeof(buff), "run at %s\n%s %"PRIu64" concurrency", url, time,  cfg.connections);
    record_html_log("${test_content}", buff);
    free(time);
}

static void print_test_result(struct resultForm *o, errors *errors) {
    char buff[1024];
    int offset = 0;
    char *time = strdup(o->time);
    format_time_str(&time);
    snprintf(buff, sizeof(buff), "%"PRIu64" requests sent in %s, %sB recieved\n", o->complete, time, o->bytes);
    free(time);
    if (errors->connect || errors->read || errors->write) {
        offset = strlen(buff);
        snprintf(buff + offset, sizeof(buff), "Socket errors: connect %d, read %d, write %d\n",
                errors->connect, errors->read, errors->write);
    }
    record_html_log("${test_result}", buff);
}

static void print_result_details(struct resultForm *o, errors *errors) {
    char buff[1024];
    int offset = 0;

    snprintf(buff, sizeof(buff), "Completed Requests: %lu\n", o->complete);
    offset = strlen(buff);
    snprintf(buff + offset, sizeof(buff), "Non-2xx or 3xx responses: %d\n", errors->status);

    offset = strlen(buff);
    snprintf(buff + offset, sizeof(buff), "Average RPS: %s requests/s\n", o->req_per_s);
    offset = strlen(buff);
    snprintf(buff + offset, sizeof(buff), "Average Transfer Rate: %sB/s\n", o->bytes_per_s);
    record_html_log("${result_details}", buff);
}

static void print_cpu_percent(json_t* json, uint64_t start_time) {
    CpuPerformance* cpu_performance = getCpuPerformance(json);
    if (cpu_performance == NULL)
        return;

    char *performance_data = NULL;
    char timeArray[40];
    int count = cpu_performance->percent.count;
    for (uint64_t i = 0; i < count; i++) {
        time_t time = start_time + i * cfg.interval;
        strftime(timeArray, sizeof(timeArray) - 1, "%F %T", localtime(&time));

        char format_string[1024];
        sprintf(format_string, "\n{\"date\":\"%s\", \"cpu\":%lf", timeArray, cpu_performance->percent.array[i]);
        for (int j = 0; j < cpu_performance->coreNum; j++) {
            int offset = strlen(format_string);
            int index = i * cpu_performance->coreNum + j;
            sprintf(format_string + offset, ", \"cpu%d\":%lf", j, cpu_performance->corePercent.array[index]);
        }
        strcat(format_string, "},");

        aprintf(&performance_data, format_string);
    }

    char *general_info_data = NULL;
    aprintf(&general_info_data, "CPU Model:%s\nCPU Architecture:%s\nCPU MHz:%s\nCPU(s):%d\n", cpu_performance->model,
            cpu_performance->architecture, cpu_performance->MHz, cpu_performance->coreNum);

    char cpu_num[4];
    snprintf(cpu_num, sizeof(cpu_num), "%d", cpu_performance->coreNum);
    record_html_log("${cpu_num}", cpu_num);
    record_html_log("${cpu_chart_div}", "<div id=\"cpu_chart\"></div>");
    record_html_log("${cpu_chart_data}", performance_data);
    record_html_log("${general_info_data}", general_info_data);
    releaseCpuPerformance(cpu_performance);
    free(performance_data);
    free(general_info_data);
}

static void print_mem_percent(json_t* json, uint64_t start_time) {
    MemPerformance* memPerformance = getMemPerformance(json);
    if (memPerformance == NULL)
        return;

    char *performance_data = NULL;
    char timeArray[40];
    int count = memPerformance->percent.count;
    for (uint64_t i = 0; i < count; i++) {
        time_t time = start_time + i * cfg.interval;
        strftime(timeArray, sizeof(timeArray) - 1, "%F %T", localtime(&time));
        aprintf(&performance_data, "\n{\"date\":\"%s\", \"mem\":%lf},",
                timeArray, memPerformance->percent.array[i]);
    }

    record_html_log("${mem_chart_div}", "<div id=\"mem_chart\"></div>");
    record_html_log("${mem_chart_data}", performance_data);
    releaseMemPerformance(memPerformance);
    free(performance_data);
}

static void print_io_percent(json_t* json, uint64_t start_time) {
    IoPerformance* ioPerformance = getIoPerformance(json);
    if (ioPerformance == NULL)
        return;

    char *performance_data = NULL;
    char timeArray[40];
    int count = ioPerformance->readSize.count;
    for (int i = 0; i < count - 1; i++) {
        time_t time = start_time + i * cfg.interval;
        strftime(timeArray, sizeof(timeArray) - 1, "%F %T", localtime(&time));
        double readSize = i != 0 ? ioPerformance->readSize.array[i] - ioPerformance->readSize.array[i - 1] : 0;
        double writeSize = i != 0 ? ioPerformance->writeSize.array[i] - ioPerformance->writeSize.array[i - 1] : 0;
        int readCount = i != 0 ? ioPerformance->readCount.array[i] - ioPerformance->readCount.array[i - 1] : 0;
        int writeCount = i != 0 ? ioPerformance->writeCount.array[i] - ioPerformance->writeCount.array[i - 1] : 0;
        aprintf(&performance_data, "\n{\"date\":\"%s\", \"readSize\":%lf, \"writeSize\":%lf, \"readCount\":%d, \"writeCount\":%d},", 
                timeArray, readSize, writeSize, readCount, writeCount);
    }

    record_html_log("${io_chart_div}", "<div id=\"io_chart\"></div>");
    record_html_log("${io_chart_data}", performance_data);
    releaseIoPerformance(ioPerformance);
    free(performance_data);
}

static void print_platform_info(json_t* json) {
    PlatformInfo* platform_info = getPlatformInfo(json);
    if (platform_info == NULL)
        return;

    char *platform_info_data = NULL;
    aprintf(&platform_info_data, "hostname:%s\nsystem:%s\nrelease:%s\ndistribution:%s\n", platform_info->hostname,
            platform_info->system, platform_info->release, platform_info->distribution);
    record_html_log("${platform_info_data}", platform_info_data);
    releasePlatformInfo(platform_info);
    free(platform_info_data);
}

static void print_disk_info(json_t* json) {
    int disk_num = 0;
    DiskInfo** disk_info = getDiskInfo(json, &disk_num);
    if (disk_info == NULL)
        return;

    char* disk_info_data = NULL;
    for (int i = 0; i < disk_num; i++)
    {
        aprintf(&disk_info_data, "Usage of %s : %.1f%% of %.1f GB\n", disk_info[i]->mountPoint, 
                disk_info[i]->percent, disk_info[i]->total);
    }
    record_html_log("${disk_info_data}", disk_info_data);
    releaseDiskInfo(disk_info, disk_num);
    free(disk_info_data);
}

static void print_dstServerPerformance(CollectConfig* collectCfg, json_t* bufferJson) {
    print_cpu_percent(bufferJson, collectCfg->start_time);
    print_mem_percent(bufferJson, collectCfg->start_time);
    print_io_percent(bufferJson, collectCfg->start_time);
    print_disk_info(bufferJson);
    print_platform_info(bufferJson);
}

