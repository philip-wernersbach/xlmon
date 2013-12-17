/*
 * xlmon.c
 * 
 * Part of xlmon
 * 
 * Copyright (c) 2013 Philip Wernersbach & Jacobs Automation
 * All rights reserved.
 * 
 * This code is licensed under the 2-Clause BSD License.
 * The license text is in the LICENSE file.
 */

/* System includes */
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <inttypes.h>
#include <signal.h>

/* Library includes */
#include <libxl_utils.h>

/* Local includes */
#include <xlmon.h>

/* For getopt */
extern char *optarg;
extern int optind, opterr, optopt;

xlmon_run_info run_info;

static void usage(char *program_name) {
	fprintf(stderr, "Usage: %s [options]\n\n", program_name);
	fprintf(stderr, "Required Options:\n");
	fprintf(stderr, "\t-n [DomU name] -> The name of the DomU xlmon will monitor.\n");
	fprintf(stderr, "\t-c [DomU config] -> The path of the config file for the DomU.\n");
	
	fprintf(stderr, "\nOptional Options:\n");
	fprintf(stderr, "\t-S -> xlmon will not shut down DomU.\n");
	fprintf(stderr, "\t-R -> xlmon will not restart DomU.\n");
	fprintf(stderr, "\t-T -> xlmon will not shut down DomU when receiving a TERM signal.\n");
	fprintf(stderr, "\t-D -> xlmon will not destroy the DomU resources after shutting down\n");
	fprintf(stderr, "\t      a DomU.\n");
	fprintf(stderr, "\t-I -> xlmon will ignore a DomU shut down and will continue running\n");
	fprintf(stderr, "\t      after the DomU has shut down.\n");
	fprintf(stderr, "\t-A -> xlmon will attempt to recover from a stale DomU ID.\n");
	fprintf(stderr, "\t-p [time] -> The frequency (in seconds) that the monitoring loop\n");
	fprintf(stderr, "\t             will run at. (Default: %d)\n", DEFAULT_RUN_PERIOD);
	exit(1);
}

static void signal_stop_running(int signum) {
	if (signum != 0)
		fprintf(stderr, "INFO: Received signal, stopping monitoring loop.\n");
	else
		printf("INFO: Program signaled shutdown, stopping monitoring loop.\n");
	
	run_info.running = 0;
}

static void signal_shutdown(int signum) {
	(void)(signum);
	run_info.term_received = 1;
}

/* Do a graceful shutdown, unallocate monitoring resources. */
static void program_shutdown(xlmon_ctx *ctx, xlmon_config *config)
{
	printf("INFO: Gracefully shutting down.\n");
	
	/* Unallocate our libxl context. */
	if (ctx->ctx != NULL)
		libxl_ctx_free(ctx->ctx);
	
	/* Unallocate our Xen logger. */
	if (ctx->logger != NULL)
		xtl_logger_destroy((xentoollog_logger*)ctx->logger);
	
	/* The strings we malloc'd. */
	if (config->vm_name == NULL)
		free(config->vm_name);
		
	if (config->vm_config_file)
		free(config->vm_config_file);
}

/* If our VM ID goes stale and we can't recover, shut down. */
static void program_shutdown_due_to_stale_vmid(xlmon_ctx *ctx, xlmon_config *config)
{
	fprintf(stderr, "ERROR: Could not get info for DomU %s.\n", config->vm_name);
	program_shutdown(ctx, config);
	exit(4);
}

/* Take the DomU name from the xlmon_config struct, get a DomU ID, and store it in xlmon_ctx. */
static void vm_name_to_vmid(xlmon_ctx *ctx, xlmon_config *config)
{
	/* Get the DomU ID from the DomU name. */
	if (libxl_name_to_domid(ctx->ctx, config->vm_name, &ctx->vmid))
	{
		fprintf(stderr, "ERROR: The DomU %s does not exist, so we cannot monitor it.\n", config->vm_name);
        program_shutdown(ctx, config);
        exit(3);
	}
	
	/* Print informative messages in a cross-platform manner. */
	printf("DEBUG: DomU Name: %s\n", config->vm_name);
	printf("DEBUG: New DomU ID: %"PRIu32"\n", ctx->vmid);
}

/* Destroy the DomU. */
static void vm_destroy(xlmon_ctx *ctx)
{		
	printf("INFO: Destroying DomU...\n");
	libxl_domain_destroy(ctx->ctx, ctx->vmid, NULL);
	printf("INFO: DomU destroyed.\n");
}

/* Signal a Dom U shutdown, but don't destroy it. */
static void vm_shutdown(xlmon_ctx *ctx, xlmon_config *config)
{
	/* Does the user want us to shut down the DomU? */
	if (config->do_shutdown == 1) {
		/* Try sending a PV shutdown command first. */
		printf("INFO: Gracefully shutting down DomU...\n");
		if (libxl_domain_shutdown(ctx->ctx, ctx->vmid))
		{
			/* ...if that doesn't work, send an ACPI power button press event. */
			printf("INFO: DomU doesn't support PV shutdown, shutting down with ACPI power button event.\n");
			if (libxl_send_trigger(ctx->ctx, ctx->vmid, LIBXL_TRIGGER_POWER, 0))
			{
				/* ...if that doesn't work, we're up the creek without a paddle, there's
				 * nothing else we can do to gracefully shut down the DomU. */
				fprintf(stderr, "ERROR: DomU failed to accept ACPI power button event!\n");
			}
		}
		printf("INFO: DomU shut down.\n");
	} else {
		printf("INFO: not shutting down DomU because of configuration.\n");
	}
}

/*
 * Not the nicest way to spawn a new DomU, but spawning a DomU is very complex,
 * so it's best left to the upstream tools.
 */
static void vm_create_with_xl_command(xlmon_ctx *ctx, xlmon_config *config, xlmon_run_info *runinfo)
{
	pid_t pid;		
	
	printf("INFO: Creating DomU...\n");
	
	/* Fork a new process. */
	pid = fork();
	
	if (pid < 0)
	{
		fprintf(stderr, "ERROR: Failed to fork xl process to create DomU.\n");
	} else if (pid == 0) {
		/* We're the child process, create the DomU by executing the xl command. */
		execlp("xl", "xl", "create", config->vm_config_file, NULL);
	} else {
		/* We're the parent process, wait for the DomU to init, and then get the new
		 * DomU ID. */
		printf("INFO: xl process spawned to create DomU, waiting before continuing...\n");
		sleep(runinfo->period * 2);
		
		vm_name_to_vmid(ctx, config);
	}
}

/* Parse the command line options and change the appropriate fields in xlmon_config, and our xlmon_run_info structs. */
static void xlmon_init_config(int argc, char *argv[], char *program_name, xlmon_config *config, xlmon_run_info *runinfo)
{
	int c;
	
	while ( (c = getopt(argc, argv, "SRTDIAc:n:p:")) != -1) {
		switch(c) {
			/* Don't shut down VM. */
			case 'S':
				config->do_shutdown = 0;
				break;
				
			/* Don't restart VM. */
			case 'R':
				config->do_restart = 0;
				break;
			
			/* Don't shut down VM when we receive a TERM signal. */
			case 'T':
				config->do_term = 0;
				break;
			
			/* Don't destroy VM resources after we shut it down. */
			case 'D':
				config->do_destroy = 0;
				break;
			
			/* Ignore a VM shut down and continue running after
			 * the VM shuts down. */
			case 'I':
				config->do_ignore_vm_shutdown = 1;
				break;
				
			/* Attempt to recover from a stale VM ID. */
			case 'A':
				config->do_recover_stale_vm_id = 1;
				break;
			
			/* The name of the VM to monitor. */
			case 'n':
				if (optarg != NULL) {
					config->vm_name = malloc((strlen(optarg) + 1) * sizeof(char));
					strcpy(config->vm_name, optarg);
					
					break;
				}
			
			/* The path to the config file. */
			case 'c':
				if (optarg != NULL) {
					config->vm_config_file = malloc((strlen(optarg) + 1) * sizeof(char));
					strcpy(config->vm_config_file, optarg);
					
					break;
				}
			
			/* A custom period (how frequently the monitoring loop runs). */
			case 'p':
				if (optarg != NULL) {
					if ((runinfo->period =  (unsigned int)strtoul(optarg, NULL, 10)) != 0)
						break;
				}
			
			/* Invalid arguments, show usage. */
			default:
				usage(program_name);
			
		}
	}
	
	/* Make sure that we have a VM name, and have a VM config file path if we can restart the VM. */				
	if ((config->vm_name == NULL) || ((config->do_restart == 1) && (config->vm_config_file == NULL)))
	{
		/* We don't, deinit and show usage. */
		if (config->vm_name != NULL)
			free(config->vm_name);
		
		if (config->vm_config_file != NULL)
			free(config->vm_config_file);
		
		usage(program_name);
	}
}

/* Init the monitoring context. */
static void xlmon_init_ctx(xlmon_ctx *ctx, xlmon_config *config)
{
	/* Create the Xen logger. We really don't need a logger since we do our own error
	 * checking, but libxl requires one. */
	if ((ctx->logger = xtl_createlogger_stdiostream(stderr, XTL_PROGRESS,  0)) == NULL)
	{
		fprintf(stderr, "ERROR: Cannot create Xen logger!.\n");
		program_shutdown(ctx, config);
        exit(1);
	}
	
	/* Initialize the libxl context. */
	if (libxl_ctx_alloc(&ctx->ctx, LIBXL_VERSION, 0, (xentoollog_logger*)ctx->logger))
	{
		fprintf(stderr, "ERROR: Cannot initialize Xen/XL context.\n");
		program_shutdown(ctx, config);
        exit(2);
	}
	
	/* Get a DomU ID from our DomU name. */
	vm_name_to_vmid(ctx, config);
}

int main(int argc, char *argv[]) {
	xlmon_ctx ctx;
	xlmon_config config;
	
	/* We zero out our structs just to be safe. */
	memset(&ctx, 0, sizeof(xlmon_ctx));
	memset(&config, 0, sizeof(xlmon_config));
	memset(&run_info, 0, sizeof(xlmon_run_info));
	
	/* Set the default run info. */
	run_info.running = 1;
	run_info.term_received = 0;
	run_info.period = DEFAULT_RUN_PERIOD;
	
	/* Enable all xlmon actions by default. */
	config.do_shutdown = 1;
	config.do_restart = 1;
	config.do_term = 1;
	config.do_destroy = 1;
	
	/* Disable ignore and stale recovery by default. */
	config.do_ignore_vm_shutdown = 0;
	config.do_recover_stale_vm_id = 0;
	
	/* Process the command line arguments and modify our xlmon_config. */
	xlmon_init_config(argc, argv, argv[0], &config, &run_info);
	
	
	/* Work around a minor bug in libxl. The libxl functions will segfault if we don't
	 * have root permissions, so make sure we're root.
	 * 
	 * I personally don't like UID-based checks, but libxl expects the UID to be zero,
	 * so we have to check for root this way. */
	if (geteuid() != 0)
	{
		fprintf(stderr, "ERROR: Xen access requires root, please run this as root.\n");
		program_shutdown(&ctx, &config);
        exit(1);
	}
	
	/* Init our monitoring context. */
	xlmon_init_ctx(&ctx, &config);
	
	/* Register a signal handler. Our signal handler will signal the monitoring
	 * loop to stop running. */
	signal(SIGINT, signal_stop_running);
	
	/* If we want to shut down the DomU after getting a TERM signal, register
	 * a different signal handler. */
	if (config.do_term == 1)
		signal(SIGTERM, signal_shutdown);
	else
		signal(SIGTERM, signal_stop_running);
	
	/* Start the monitoring loop! It will run until xlmon_run_info.running is
	 * set to zero. */
	printf("INFO: DomU monitoring loop has started.\n");
	while (run_info.running == 1) {
		/* Get updated info for our DomU. */
		if (libxl_domain_info(ctx.ctx, &ctx.vminfo, ctx.vmid)) {
			if (config.do_recover_stale_vm_id == 0) {
				program_shutdown_due_to_stale_vmid(&ctx, &config);
			} else {
				
				/* The program is configured to recover, so try to refresh our VM ID. */
				fprintf(stderr, "INFO: DomU ID is stale, but attempting to recover because of configuration.\n");
				vm_name_to_vmid(&ctx, &config);
				
				/* We refreshed the VM ID. If getting the domain info fails again, 
				 * then we have to abort because we can't recover. */
				if (libxl_domain_info(ctx.ctx, &ctx.vminfo, ctx.vmid))
					program_shutdown_due_to_stale_vmid(&ctx, &config);
			}
		}
		
		/* If we got a TERM signal, shut down the DomU on this run. */
		if (run_info.term_received == 1) {					
			
			fprintf(stderr, "INFO: Received signal, shutting down DomU.\n");
			vm_shutdown(&ctx, &config);
			run_info.term_received = 0;
		
		} else if (ctx.vminfo.shutdown) {
			
			/* If the DomU has restarted, destroy the DomU and create it again on this run. */
			if (ctx.vminfo.shutdown_reason == LIBXL_SHUTDOWN_REASON_REBOOT) {
				
				/* Does the user want us to automatically restart the DomU? */
				if (config.do_restart == 1) {
					printf("INFO: DomU is rebooting, waiting before destroying and recreating...\n");
					sleep(run_info.period * 2);
					
					/* Does the user want us to automatically destroy the DomU? */
					if (config.do_destroy == 1)
						vm_destroy(&ctx);
					else
						printf("INFO: DomU is rebooting, but not destroying because of configuration.\n");
						
					vm_create_with_xl_command(&ctx, &config, &run_info);
				} else {
					printf("INFO: DomU is rebooting, but not destroying and recreating because of configuration.\n");
					sleep(run_info.period);
				}
				
			} else {
				
				/* If the DomU has restarted, destroy the DomU on this run. */
				/* Does the user want us to automatically destroy the DomU? */
				if (config.do_destroy == 1) {
					printf("INFO: DomU is shutting down, waiting before destroying...\n");
					sleep(run_info.period * 2);
						
					vm_destroy(&ctx);
				} else {
					printf("INFO: DomU is shutting down, but not destroying because of configuration.\n");
				}
				
				if (config.do_ignore_vm_shutdown == 0) {
					signal_stop_running(0);
				} else {
					printf("INFO: DomU is shutting down, but ignoring and continuing because of configuration.\n");
					sleep(run_info.period);
				}
			}
		} else {
			/* Nothing happened on this run, sleep our period. */
			sleep(run_info.period);
		}
	}
	printf("INFO: DomU monitoring loop has ended.\n");
	
	/* Unallocate our resources and exit. */
	program_shutdown(&ctx, &config);
		
	return 0;
}
