// Copyright (C) 2012 - Will Glozer.  All rights reserved.

#include "wrk.h"
#include "script.h"
#include "main.h"

#define MAX_URL_LENGTH 2048

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
    char    *json_file;
    char    *log_file;
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

static FILE *g_log;
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
           "        --timeout     <T>  Socket/request timeout     \n"
           "    -l  --log              Output report in rst format\n"
           "    -v, --version          Print version details      \n"
           "                                                      \n"
           "  Numeric arguments may include a SI unit (1k, 1M, 1G)\n"
           "  Time arguments may include a time unit (2s, 2m, 2h)\n");
}

static bool html_get_template() {
    char *template_name = "template/template1.html";
    struct stat s;
    if (stat(template_name, &s) == -1 || s.st_size == 0)
        return false;

    FILE *fp = fopen(template_name, "r");
    if (fp == NULL)
        return false;

    g_html_template = (char *)malloc(sizeof(char) * s.st_size + 1);
    fread(g_html_template, s.st_size, sizeof(char), fp);

    fclose(fp);

    return true;
}

static void decide_thread_num(struct config *cfg) {
    if (cfg->connections < 500)
        cfg->threads = 1;
    else {
        int cpu_num = get_nprocs();
        cfg->threads = cfg->connections/500 < cpu_num ? cfg->connections/500 : cpu_num;
    }
}

int main(int argc, char **argv) {
    thread_concurrency = false;
    char url[MAX_URL_LENGTH] = {0};
    char **headers = zmalloc(argc * sizeof(char *));
    struct http_parser_url parts = {};

    if (parse_args(&cfg, url, headers, argc, argv)) {
        usage();
        exit(1);
    }

    decide_thread_num(&cfg);

    lua_State *L = script_create(cfg.script, cfg.json_file, url, headers);
    if (strlen(url) == 0 || !script_parse_url(url, &parts)) {
        fprintf(stderr, "invalid URL: %s\n", url);
        exit(1);
    }

    g_log = fopen(cfg.log_file, "w");
    if (g_log == NULL) {
        g_log = stderr;
        fprintf(stderr, "get last error:%d\n", errno);
    }

    char *schema  = copy_url_part(url, &parts, UF_SCHEMA);
    char *host    = copy_url_part(url, &parts, UF_HOST);
    char *port    = copy_url_part(url, &parts, UF_PORT);
    char *service = port ? port : schema;

    if (!script_resolve(L, host, service)) {
        char *msg = strerror(errno);
        fprintf(stderr, "unable to connect to %s:%s %s\n", host, service, msg);
        exit(1);
    }

    cfg.host = host;

    if (!strncmp("https", schema, 5)) {
        if ((cfg.ctx = ssl_init()) == NULL) {
            fprintf(stderr, "unable to initialize SSL\n");
            ERR_print_errors_fp(stderr);
            exit(1);
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
            exit(2);
        }
    }

    struct sigaction sa = {
        .sa_handler = handler,
        .sa_flags   = 0,
    };
    sigfillset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);

    if (!html_get_template()) {
        fprintf(stderr, "Cannot open HTML template");
        exit(1);
    }

    print_test_parameter(url);
    
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

    print_test_result(&result, &errors);
    print_result_details(&result, &errors);

    print_stats_error_code(&errors);
    print_stats_requests(statistics.requests);
    print_stats_latency_map(statistics.latency);

    if (complete / cfg.connections > 0) {
        int64_t interval = runtime_us / (complete / cfg.connections);
        stats_correct(statistics.latency, interval);
    }

   // print_stats_header();
    print_stats("Latency", statistics.latency, format_time_us);
    print_stats("Req/Sec", statistics.requests, format_metric);
    print_stats_latency(statistics.latency);
    
    print_result_form();

    if (script_has_done(L)) {
        script_summary(L, runtime_us, complete, bytes);
        script_errors(L, &errors);
        script_done(L, statistics.latency, statistics.requests);
    }

    fwrite(g_html_template, strlen(g_html_template), sizeof(char), g_log);
    fclose(g_log);
    free(g_html_template);

    return 0;
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

    if (thread->requests > 0) {
        uint64_t elapsed_ms = (time_us() - thread->start) / 1000;
        uint64_t requests = (thread->requests / (double) elapsed_ms) * 1000;

        stats_record(statistics.requests, requests);

        uint64_t time_interval = (time_us() - start_thread_time) / 1000 / 1000;
        stats_record_requests_per_sec(statistics.requests, thread->requests, time_interval);

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
            //printf("buf:%s\n", c->request);
            //printf("offset:%s\n", c->request+c->length);
        }
        c->start   = time_us();
        c->pending = cfg.pipeline;
    }

    char  *buf = c->request + c->written;
    size_t len = c->length  - c->written;
    size_t n;

    switch (sock.write(c, buf, len, &n)) {
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
    printf("error number:%d\n", errno);
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
    { "log",         required_argument, NULL, 'l' },
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
                cfg->json_file = optarg;
                break;
            case 'H':
                *header++ = optarg;
                break;
            case 'l':
                cfg->log_file = optarg;
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

char *str_replace(char *orig, char *rep, char *with) {
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

    // first time through the loop, all the variable are set correctly
    // from here on,
    //    tmp points to the end of the result string
    //    ins points to the next occurrence of rep in orig
    //    orig points to the remainder of orig after "end of rep"
    while (count--) {
        ins = strstr(orig, rep);
        len_front = ins - orig;
        tmp = strncpy(tmp, orig, len_front) + len_front;
        tmp = strcpy(tmp, with) + len_with;
        orig += len_front + len_rep; // move to next "end of rep"
    }
    strcpy(tmp, orig);

    return result;
}

static void record_html_log(char *key, char *value) {
    char *p = str_replace(g_html_template, key, value);
    free(g_html_template);
    g_html_template = p;
}

//static void print_stats_header() {
//    fprintf(g_log, "\n::\n\n\tThread Stats%6s%11s%8s%12s\n", "Avg", "Stdev", "Max", "+/- Stdev");
//}

//static void print_units(long double n, char *(*fmt)(long double), int width) {
//    char *msg = fmt(n);
//    int len = strlen(msg), pad = 2;
//
//    if (isalpha(msg[len-1])) pad--;
//    if (isalpha(msg[len-2])) pad--;
//    width -= pad;
//
//    fprintf(stderr, "%*.*s%.*s", width, width, msg, pad, "  ");
//
//    free(msg);
//}

static void print_stats_error_code(errors *errors) {
    char buff[1024];
    int offset = 0;
    if (errors->status == 0) {
        record_html_log("${error_codes}", NULL);
        return ;
    }

    snprintf(buff, sizeof(buff), "error code :\n");
    int count = sizeof(errors->code)/sizeof(errors->code[0]);
    for (int i = 0; i < count; i++) {
        if (errors->code[i] != 0) {
            offset = strlen(buff);
            snprintf(buff + offset, sizeof(buff), "  %u:%u\n", i, errors->code[i]);
        }
    }
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
    record_html_log("${latency_chat_label}", x_coordinate);
    record_html_log("${latency_chat_data}", y_coordinate);
    free(x_coordinate);
    free(y_coordinate);
}

static void print_stats_requests(stats *stats) {
    char buff[1024];
    //int offset = 0;
    snprintf(buff, sizeof(buff), "Frequency of requests Per %lus\n", cfg.interval);
    char *x_coordinate = NULL;
    char *y_coordinate = NULL;
    aprintf(&x_coordinate, "labels: ['0', ");
    aprintf(&y_coordinate, "data: [");

    uint64_t requests = 0;
    uint64_t requests_num = 0;
    uint64_t  i = 0;
    if (stats->max_location == 0) {
        aprintf(&x_coordinate, "'%d', ", cfg.interval);
        aprintf(&y_coordinate, "%Lf, ", (long double)0);
        goto END;
    }

    for (i = 0; i <= stats->max_location; i++) {
        requests += stats->requests[i];
        requests_num++;

        if (requests_num == cfg.interval) {
            uint64_t indexInterval = i/cfg.interval;
            aprintf(&x_coordinate, "'%d', ", (indexInterval + 1) * cfg.interval);
            aprintf(&y_coordinate, "%Lf, ", (long double)requests/cfg.interval);
            requests = 0;
            requests_num = 0;
        }
    }

END:
    record_html_log("${requests_frequency}", buff);
    aprintf(&x_coordinate, "]");
    aprintf(&y_coordinate, "]");
    record_html_log("${rps_chat_label}", x_coordinate);
    record_html_log("${rps_chat_data}", y_coordinate);
    free(x_coordinate);
    free(y_coordinate);
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
        record_html_log("${latency_distribution}", NULL);

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

static void print_test_parameter(const char *url) {
    record_html_log("${json_file}", cfg.json_file);

    char buff[1024];
    char *time = format_time_s(cfg.duration);
    snprintf(buff, sizeof(buff), "Running %s test @ %s\n%"PRIu64" concurrency",
            time, url, cfg.connections);
    record_html_log("${test_content}", buff);
}

static void print_test_result(struct resultForm *o, errors *errors) {
    char buff[1024];
    int offset = 0;
    snprintf(buff, sizeof(buff), "%"PRIu64" requests in %s, %sB read\n", o->complete, o->time, o->bytes);
    if (errors->connect || errors->read || errors->write || errors->timeout) {
        offset = strlen(buff);
        snprintf(buff + offset, sizeof(buff), "Socket errors: connect %d, read %d, write %d, timeout %d\n",
                errors->connect, errors->read, errors->write, errors->timeout);
    }
    record_html_log("${test_result}", buff);
}

static void print_result_details(struct resultForm *o, errors *errors) {
    char buff[1024];
    int offset = 0;

    snprintf(buff, sizeof(buff), "Complete responses: %lu\n", o->complete);
    offset = strlen(buff);
    snprintf(buff + offset, sizeof(buff), "Non-2xx or 3xx responses: %d\n", errors->status);

    offset = strlen(buff);
    snprintf(buff + offset, sizeof(buff), "Requests/sec: %10s\n", o->req_per_s);
    offset = strlen(buff);
    snprintf(buff + offset, sizeof(buff), "Transfer/sec: %10sB\n", o->bytes_per_s);
    record_html_log("${result_details}", buff);
}

