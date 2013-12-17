/*
 * xlmon.h
 * 
 * Part of xlmon
 * 
 * Copyright (c) 2013 Philip Wernersbach & Jacobs Automation
 * All rights reserved.
 * 
 * This code is licensed under the 2-Clause BSD License.
 * The license text is in the LICENSE file.
 */
 
#define DEFAULT_RUN_PERIOD 5

typedef struct {
	libxl_ctx *ctx;
	xentoollog_logger_stdiostream *logger;
	libxl_dominfo vminfo;
	uint32_t vmid;
} xlmon_ctx;

typedef struct {
	unsigned int do_shutdown;
	unsigned int do_restart;
	unsigned int do_term;
	unsigned int do_destroy;
	unsigned int do_ignore_vm_shutdown;
	unsigned int do_recover_stale_vm_id;
	
	char *vm_name;
	char *vm_config_file;
} xlmon_config;

typedef struct {
	unsigned int running;
	unsigned int term_received;
	unsigned int period;
} xlmon_run_info;
