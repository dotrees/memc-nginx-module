#ifndef DDEBUG
#define DDEBUG 0
#endif
#include "ddebug.h"

/*
 * Copyright (C) Igor Sysoev
 */

/*
 * Copyright (C) Zhang "agentzh" Yichun
 */

#include "ngx_http_memc_module.h"
#include "ngx_http_memc_handler.h"
#include "ngx_http_memc_util.h"

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
#include <nginx.h>


#define SAFE_LENGTH     224
#define DECRMENT_STEP   8

static ngx_int_t ngx_http_safe_http_referer_variable(ngx_http_request_t *r,
        ngx_http_variable_value_t *v, uintptr_t data);

static ngx_int_t ngx_http_memc_add_variables(ngx_conf_t *cf);

static void *ngx_http_memc_create_loc_conf(ngx_conf_t *cf);
static char *ngx_http_memc_merge_loc_conf(ngx_conf_t *cf,
    void *parent, void *child);

static char *ngx_http_memc_cmds_allowed(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);

static char *ngx_http_memc_pass(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);

static char *ngx_http_memc_upstream_max_fails_unsupported(ngx_conf_t *cf,
    ngx_command_t *cmd, void *conf);
static char *ngx_http_memc_upstream_fail_timeout_unsupported(ngx_conf_t *cf,
    ngx_command_t *cmd, void *conf);


static ngx_conf_bitmask_t  ngx_http_memc_next_upstream_masks[] = {
    { ngx_string("error"), NGX_HTTP_UPSTREAM_FT_ERROR },
    { ngx_string("timeout"), NGX_HTTP_UPSTREAM_FT_TIMEOUT },
    { ngx_string("invalid_response"), NGX_HTTP_UPSTREAM_FT_INVALID_HEADER },
    { ngx_string("not_found"), NGX_HTTP_UPSTREAM_FT_HTTP_404 },
    { ngx_string("off"), NGX_HTTP_UPSTREAM_FT_OFF },
    { ngx_null_string, 0 }
};


static ngx_command_t  ngx_http_memc_commands[] = {

    { ngx_string("memc_cmds_allowed"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF
          |NGX_HTTP_LIF_CONF|NGX_CONF_1MORE,
      ngx_http_memc_cmds_allowed,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      NULL },

    { ngx_string("memc_pass"),
      NGX_HTTP_LOC_CONF|NGX_HTTP_LIF_CONF|NGX_CONF_TAKE1,
      ngx_http_memc_pass,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      NULL },

    { ngx_string("memc_connect_timeout"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_msec_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_memc_loc_conf_t, upstream.connect_timeout),
      NULL },

    { ngx_string("memc_send_timeout"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_msec_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_memc_loc_conf_t, upstream.send_timeout),
      NULL },

    { ngx_string("memc_read_timeout"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_msec_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_memc_loc_conf_t, upstream.read_timeout),
      NULL },

    { ngx_string("memc_buffer_size"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_size_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_memc_loc_conf_t, upstream.buffer_size),
      NULL },

    { ngx_string("memc_next_upstream"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_1MORE,
      ngx_conf_set_bitmask_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_memc_loc_conf_t, upstream.next_upstream),
      &ngx_http_memc_next_upstream_masks },

    { ngx_string("memc_upstream_max_fails"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_http_memc_upstream_max_fails_unsupported,
      0,
      0,
      NULL },

    { ngx_string("memc_upstream_fail_timeout"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_http_memc_upstream_fail_timeout_unsupported,
      0,
      0,
      NULL },

    { ngx_string("memc_flags_to_last_modified"),
      NGX_HTTP_LOC_CONF|NGX_HTTP_LIF_CONF|NGX_CONF_FLAG,
      ngx_conf_set_flag_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_memc_loc_conf_t, flags_to_last_modified),
      NULL },

      ngx_null_command
};


static ngx_http_module_t  ngx_http_memc_module_ctx = {
    ngx_http_memc_add_variables,           /* preconfiguration */
    ngx_http_memc_init,                    /* postconfiguration */

    NULL,                                  /* create main configuration */
    NULL,                                  /* init main configuration */

    NULL,                                  /* create server configuration */
    NULL,                                  /* merge server configuration */

    ngx_http_memc_create_loc_conf,    /* create location configration */
    ngx_http_memc_merge_loc_conf      /* merge location configration */
};


ngx_module_t  ngx_http_memc_module = {
    NGX_MODULE_V1,
    &ngx_http_memc_module_ctx,        /* module context */
    ngx_http_memc_commands,           /* module directives */
    NGX_HTTP_MODULE,                       /* module type */
    NULL,                                  /* init master */
    NULL,                                  /* init module */
    NULL,                                  /* init process */
    NULL,                                  /* init thread */
    NULL,                                  /* exit thread */
    NULL,                                  /* exit process */
    NULL,                                  /* exit master */
    NGX_MODULE_V1_PADDING
};


static ngx_http_variable_t ngx_http_imgoutlink_vars[] = {

	{ ngx_string("safe_http_referer"), NULL,
	  ngx_http_safe_http_referer_variable, 0,
	  0, 0 },

	{ ngx_null_string, NULL, NULL, 0, 0, 0 }
};


static uintptr_t
ngx_encode_uri(u_char *dst, u_char *src, size_t size)
{
    ngx_uint_t      n;
    static u_char   hex[] = "0123456789ABCDEF";

                    /* " ", "#", "%", "?", %00-%1F, %7F-%FF */

    static uint32_t   escape[] = {
        0xffffffff, /* 1111 1111 1111 1111  1111 1111 1111 1111 */

                    /* ?>=< ;:98 7654 3210  /.-, +*)( '&%$ #"!  */
        0xfc009bfe, /* 1111 1100 0000 0000  1001 1011 1111 1110 */

                    /* _^]\ [ZYX WVUT SRQP  ONML KJIH GFED CBA@ */
        0x78000001, /* 0111 1000 0000 0000  0000 0000 0000 0001 */

                    /*  ~}| {zyx wvut srqp  onml kjih gfed cba` */
        0xf8000001, /* 1111 1000 0000 0000  0000 0000 0000 0001 */

        0xffffffff, /* 1111 1111 1111 1111  1111 1111 1111 1111 */
        0xffffffff, /* 1111 1111 1111 1111  1111 1111 1111 1111 */
        0xffffffff, /* 1111 1111 1111 1111  1111 1111 1111 1111 */
        0xffffffff  /* 1111 1111 1111 1111  1111 1111 1111 1111 */
    };

    if (dst == NULL) {

        /* find the number of the characters to be escaped */

        n = 0;

        while (size) {
            if (escape[*src >> 5] & (1 << (*src & 0x1f))) {
                n++;
            }
            src++;
            size--;
        }

        return (uintptr_t) n;
    }

    while (size) {
        if (escape[*src >> 5] & (1 << (*src & 0x1f))) {
            *dst++ = '%';
            *dst++ = hex[*src >> 4];
            *dst++ = hex[*src & 0xf];
            src++;

        } else if (*src == ' ') {
            *dst++ = '+';
        } else {
            *dst++ = *src++;
        }

        size--;
    }

    return (uintptr_t) dst;
}


static size_t  
make_memcached_safe_variable_length(u_char *data, size_t len)
{
    size_t            l;
    uintptr_t         escape;

    if (len > SAFE_LENGTH) {
        len = SAFE_LENGTH;
    }

    while (1) {

        escape = 2 * ngx_encode_uri(NULL, data, len);

        l = len + escape;

        if (l < SAFE_LENGTH) {
            break;
        }

        len -= DECRMENT_STEP;
    }

    return len;
}


static ngx_int_t 
ngx_http_safe_http_referer_variable (ngx_http_request_t *r,
        ngx_http_variable_value_t *v, uintptr_t data) 
{
    ngx_table_elt_t  *h;

    h =  r->headers_in.referer;

    if (h) {
        v->len = h->value.len;
        v->valid = 1;
        v->no_cacheable = 0;
        v->not_found = 0;
        v->data = h->value.data;

        v->len = make_memcached_safe_variable_length(v->data, v->len);
    } else {
        v->not_found = 1;
    }

    return NGX_OK;
}


static ngx_int_t 
ngx_http_memc_add_variables(ngx_conf_t *cf)
{
	ngx_http_variable_t *var, *v;

	for (v = ngx_http_imgoutlink_vars; v->name.len; v++) {
		var = ngx_http_add_variable(cf, &v->name, v->flags);
		if (var == NULL) {
		    return NGX_ERROR;
		}

		var->get_handler = v->get_handler;
		var->data = v->data;
	}

	return NGX_OK;
}


static void *
ngx_http_memc_create_loc_conf(ngx_conf_t *cf)
{
    ngx_http_memc_loc_conf_t  *conf;

    conf = ngx_pcalloc(cf->pool, sizeof(ngx_http_memc_loc_conf_t));
    if (conf == NULL) {
        return NULL;
    }

    /*
     * set by ngx_pcalloc():
     *
     *     conf->cmds_allowed = NULL;
     *     conf->upstream.bufs.num = 0;
     *     conf->upstream.next_upstream = 0;
     *     conf->upstream.temp_path = NULL;
     *     conf->upstream.uri = { 0, NULL };
     *     conf->upstream.location = NULL;
     */

    conf->flags_to_last_modified = NGX_CONF_UNSET;

    conf->upstream.connect_timeout = NGX_CONF_UNSET_MSEC;
    conf->upstream.send_timeout = NGX_CONF_UNSET_MSEC;
    conf->upstream.read_timeout = NGX_CONF_UNSET_MSEC;

    conf->upstream.buffer_size = NGX_CONF_UNSET_SIZE;

    /* the hardcoded values */
    conf->upstream.cyclic_temp_file = 0;
    conf->upstream.buffering = 0;
    conf->upstream.ignore_client_abort = 0;
    conf->upstream.send_lowat = 0;
    conf->upstream.bufs.num = 0;
    conf->upstream.busy_buffers_size = 0;
    conf->upstream.max_temp_file_size = 0;
    conf->upstream.temp_file_write_size = 0;
    conf->upstream.intercept_errors = 1;
    conf->upstream.intercept_404 = 1;
    conf->upstream.pass_request_headers = 0;
    conf->upstream.pass_request_body = 0;

    return conf;
}


static char *
ngx_http_memc_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child)
{
    ngx_http_memc_loc_conf_t *prev = parent;
    ngx_http_memc_loc_conf_t *conf = child;

    ngx_conf_merge_value(conf->flags_to_last_modified,
            prev->flags_to_last_modified, 0);

    ngx_conf_merge_msec_value(conf->upstream.connect_timeout,
                              prev->upstream.connect_timeout, 60000);

    ngx_conf_merge_msec_value(conf->upstream.send_timeout,
                              prev->upstream.send_timeout, 60000);

    ngx_conf_merge_msec_value(conf->upstream.read_timeout,
                              prev->upstream.read_timeout, 60000);

    ngx_conf_merge_size_value(conf->upstream.buffer_size,
                              prev->upstream.buffer_size,
                              (size_t) ngx_pagesize);

    ngx_conf_merge_bitmask_value(conf->upstream.next_upstream,
                              prev->upstream.next_upstream,
                              (NGX_CONF_BITMASK_SET
                               |NGX_HTTP_UPSTREAM_FT_ERROR
                               |NGX_HTTP_UPSTREAM_FT_TIMEOUT));

    if (conf->upstream.next_upstream & NGX_HTTP_UPSTREAM_FT_OFF) {
        conf->upstream.next_upstream = NGX_CONF_BITMASK_SET
                                       |NGX_HTTP_UPSTREAM_FT_OFF;
    }

    if (conf->upstream.upstream == NULL) {
        conf->upstream.upstream = prev->upstream.upstream;
    }

    if (conf->cmds_allowed == NULL) {
        conf->cmds_allowed = prev->cmds_allowed;
    }

    return NGX_CONF_OK;
}


static char *
ngx_http_memc_pass(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_memc_loc_conf_t  *mlcf = conf;

    ngx_str_t                 *value;
    ngx_url_t                  url;
    ngx_http_core_loc_conf_t  *clcf;
    ngx_uint_t                 n;

    ngx_http_compile_complex_value_t         ccv;

    if (mlcf->upstream.upstream) {
        return "is duplicate";
    }

    clcf = ngx_http_conf_get_module_loc_conf(cf, ngx_http_core_module);

    clcf->handler = ngx_http_memc_handler;

    if (clcf->name.data[clcf->name.len - 1] == '/') {
        clcf->auto_redirect = 1;
    }

    ngx_http_memc_set_module_enabled();

    value = cf->args->elts;

    n = ngx_http_script_variables_count(&value[1]);
    if (n) {
        mlcf->complex_target = ngx_palloc(cf->pool,
                sizeof(ngx_http_complex_value_t));
        if (mlcf->complex_target == NULL) {
            return NGX_CONF_ERROR;
        }

        ngx_memzero(&ccv, sizeof(ngx_http_compile_complex_value_t));
        ccv.cf = cf;
        ccv.value = &value[1];
        ccv.complex_value = mlcf->complex_target;

        if (ngx_http_compile_complex_value(&ccv) != NGX_OK) {
            return NGX_CONF_ERROR;
        }

        return NGX_CONF_OK;
    }

    mlcf->complex_target = NULL;

    ngx_memzero(&url, sizeof(ngx_url_t));

    url.url = value[1];
    url.no_resolve = 1;

    mlcf->upstream.upstream = ngx_http_upstream_add(cf, &url, 0);
    if (mlcf->upstream.upstream == NULL) {
        return NGX_CONF_ERROR;
    }

    return NGX_CONF_OK;
}


static char *
ngx_http_memc_upstream_max_fails_unsupported(ngx_conf_t *cf,
    ngx_command_t *cmd, void *conf)
{
    ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
         "\"memc_upstream_max_fails\" is not supported, "
         "use the \"max_fails\" parameter of the \"server\" directive ",
         "inside the \"upstream\" block");

    return NGX_CONF_ERROR;
}


static char *
ngx_http_memc_upstream_fail_timeout_unsupported(ngx_conf_t *cf,
    ngx_command_t *cmd, void *conf)
{
    ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
         "\"memc_upstream_fail_timeout\" is not supported, "
         "use the \"fail_timeout\" parameter of the \"server\" directive ",
         "inside the \"upstream\" block");

    return NGX_CONF_ERROR;
}

static char *
ngx_http_memc_cmds_allowed(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_memc_loc_conf_t *mlcf = conf;

    ngx_uint_t                 i;
    ngx_str_t                 *value;
    ngx_http_memc_cmd_t        memc_cmd;
    ngx_http_memc_cmd_t        *c;
    ngx_flag_t                 is_storage_cmd;

    value = cf->args->elts;

    mlcf->cmds_allowed = ngx_array_create(cf->pool, cf->args->nelts - 1,
            sizeof(ngx_http_memc_cmd_t));

    if (mlcf->cmds_allowed == NULL) {
        return NGX_CONF_ERROR;
    }

    for (i = 1; i < cf->args->nelts; i++) {
        memc_cmd = ngx_http_memc_parse_cmd(value[i].data, value[i].len,
                &is_storage_cmd);

        if (memc_cmd == ngx_http_memc_cmd_unknown) {
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                 "Unknown memcached command \"%V\" used in "
                 "\"memc_cmds_allowed\"", &value[i]);

            return NGX_CONF_ERROR;
        }

        c = ngx_array_push(mlcf->cmds_allowed);
        if (c == NULL) {
            return NGX_CONF_ERROR;
        }

        *c = memc_cmd;
    }

    return NGX_CONF_OK;
}

