#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <libvirt/libvirt.h>
#include "mem_coordinator.h"


void printinfo(domain_memstat_t *domain)
{
	printf("In domain %d:\n", domain->id);
	printf("max physical mem: %lu KB\n", domain->maxmem);
	printf("memory unused: %llu KB\n", domain->unused);
	printf("total memory available to domain: %llu KB\n", domain->available);
	//printf("memory used: %llu KB\n", domain->used);
	printf("free memory ratio: %.2f %%\n", domain->free_ratio);
	printf("memory needed to reach threshold: %.2f KB\n", domain->needed);
	printf("current balloon value: %llu KB\n", domain->balloon);
	printf("balloon avail overhead: %llu KB\n", domain->overhead);
	if (domain->needed > 0 && domain->balloon + domain->needed >= domain->maxmem) {
		printf("[WARNING] adding more needed memory would exceed max physical memory!!\n");
	}
	printf("\n");	
}

void get_domainstats(domain_memstat_t *domain)
{
	int j;
	
	virDomainMemoryStats(domain->dom,
			     domain->stats,
			     VIR_DOMAIN_MEMORY_STAT_NR,
			     FLAGS_UNUSED);
	for (j = 0; j < VIR_DOMAIN_MEMORY_STAT_NR; j++) {
		switch (domain->stats[j].tag) {
		case MEM_UNUSED_TAG:
			domain->unused = domain->stats[j].val;
			break;
		case MEM_AVAIL_TAG:
			domain->available = domain->stats[j].val;
			break;
		case BALLOON_TAG:
			domain->balloon = domain->stats[j].val;
			break;
		}
	}
	domain->used = domain->available - domain->unused;
	domain->free_ratio = (double) calc_freeratio(domain->unused, domain->available);
	if ((FREE_RATIO_THRESH / 100) * domain->available > domain->unused) {
		domain->needed = (FREE_RATIO_THRESH / 100) * domain->available - domain->unused;
		domain->spare = 0;
	} else {
		domain->spare = domain->unused - (FREE_RATIO_THRESH / 100) * domain->available;
		domain->needed = 0;
	}
	domain->overhead = domain->balloon - domain->available;
}


int main(int argc, char *argv[])
{
	char *sec = argv[1];
	int interval;
	
	interval = atoi(sec);
	
	virConnectPtr conn;
	
	if ((conn = virConnectOpen("qemu:///system")) == NULL ) {
		fprintf(stderr, "Failed to open connection to qemu:///system\n");
		return -1;
	}

	int i;
	int nparams;
	virNodeMemoryStatsPtr params;
	host_memstat_t pmem_stat;

	nparams = 0;
	gethostmem_nparams(conn, nparams);
	params = calloc(nparams, sizeof(virNodeMemoryStats));
	virNodeGetMemoryStats(conn, VIR_NODE_MEMORY_STATS_ALL_CELLS, params, &nparams, FLAGS_UNUSED);
	printf("host node memory:\n");
	for (i = 0; i < nparams; i++) {
		if (strcmp(params[i].field, "free") == 0) {
			pmem_stat.bank = params[i].value;
			//printf("%s: %llu KB\n", params[i].field, params[i].value);
		}
		printf("%s: %llu KB\n", params[i].field, params[i].value);
	}
	printf("\n");


	int num_domains;
	int *active_domains;
	
	num_domains = virConnectNumOfDomains(conn);
	active_domains = malloc(sizeof(int) * num_domains);
	num_domains = virConnectListDomains(conn, active_domains, num_domains);

	domain_memstat_t vmem_stats[num_domains];
	domain_memstat_t *domain;
	
	for (i = 0; i < num_domains; i++) {
		domain = &vmem_stats[i];
		domain->id = active_domains[i];
		domain->dom = virDomainLookupByID(conn, domain->id);
		/* Apparently this function has to be run first... */
		if ((virDomainSetMemoryStatsPeriod(domain->dom,
						  interval,
						   VIR_DOMAIN_AFFECT_LIVE)) == -1) {
			fprintf(stderr, "virDomainSetMemoryStatsPeriod failed.");
			return -1;
		}
	}
	
	for (i = 0; i < num_domains; i++) {
		domain = &vmem_stats[i];
		domain->maxmem = virDomainGetMaxMemory(domain->dom);
		domain->stats = calloc(VIR_DOMAIN_MEMORY_STAT_NR,
				       sizeof(virDomainMemoryStatStruct));
		get_domainstats(domain);
		// testing
		if (virDomainSetMemory(domain->dom, set_initmem(domain->balloon, FREE_RATIO_THRESH)) == -1) {
			fprintf(stderr, "[ERROR] set memory failed!");
			return -1;
		}
		get_domainstats(domain);
		/*
		for (j = 0; j < VIR_DOMAIN_MEMORY_STAT_NR; j++) {
			printf("tag %d: %llu\n", domain->stats[j].tag, domain->stats[j].val);
		}
		*/
	}

	for (i = 0; i < num_domains; i++) {
		domain = &vmem_stats[i];
		//get_domainstats(domain);
		printinfo(domain);
	}
	printf("\n");

	int count;
	domain_memstatptr_ndoe_t *domains_needmore;
	domain_memstatptr_ndoe_t *domains_withspare;
	domain_memstatptr_ndoe_t *curr;
	
	count = 0;
	while (1) {
		sleep(interval);
		
		printf("*** ROUND %d ***\n\n", count++);

		/* Build linked lists of domains that need more memory and domains that don't */
		domains_needmore = NULL;
		domains_withspare = NULL;
		for (i = 0; i < num_domains; i++) {
			domain = &vmem_stats[i];
			get_domainstats(domain);
			if (domain->needed > 0) {
				if (domains_needmore == NULL) {
					domains_needmore = malloc(sizeof(domain_memstatptr_ndoe_t));
					domains_needmore->d = domain;
					domains_needmore->next = NULL;
				} else {
					curr = domains_needmore;
					while (curr->next != NULL)
						curr = curr->next;
					curr->next = malloc(sizeof(domain_memstatptr_ndoe_t));
					curr->next->d = domain;
					curr->next->next = NULL;
				}
			} else {
				if (domains_withspare == NULL) {
					domains_withspare = malloc(sizeof(domain_memstatptr_ndoe_t));
					domains_withspare->d = domain;
					domains_withspare->next = NULL;
				} else {
					curr = domains_withspare;
					while (curr->next != NULL)
						curr = curr->next;
					curr->next = malloc(sizeof(domain_memstatptr_ndoe_t));
					curr->next->d = domain;
					curr->next->next = NULL;
				}
			}
		}

		/* Testing if the linked lists are correctly built */
		curr = domains_needmore;
		if (curr != NULL) {
			printf("domain id %d needs more memory: %.2f KB\n", curr->d->id, curr->d->needed);
			while (curr->next != NULL) {
				curr = curr->next;
				printf("domain id %d needs more memory: %.2f KB\n", curr->d->id, curr->d->needed);
			}
		}
		printf("\n");

		curr = domains_withspare;
		if (curr != NULL) {
			printf("domain id %d has excess memory: %.2f KB\n", curr->d->id, curr->d->spare);
			while (curr->next != NULL) {
				curr = curr->next;
				printf("domain id %d has excess memory: %.2f KB\n", curr->d->id, curr->d->spare);
			}
		}
		printf("\n");

		//sleep(interval);

		/* First, take as much memory from bank as possible */
		curr = domains_needmore;
		if (curr != NULL) {
			printf("Taking from BANK...\n");
			domain = curr->d;
			if (pmem_stat.bank > curr->d->needed) {
				//virDomainSetMemory(domain->dom, domain->balloon + domain->needed + BUF);
				virDomainSetMemory(domain->dom, domain->balloon + domain->needed + domain->overhead);
				get_domainstats(domain);
				printinfo(domain);
				pmem_stat.bank -= curr->d->needed;
			}
			while (curr->next != NULL) {
				curr = curr->next;
				domain = curr->d;
				if (pmem_stat.bank > curr->d->needed) {
					//virDomainSetMemory(domain->dom, domain->balloon + domain->needed + BUF);
					virDomainSetMemory(domain->dom, domain->balloon + domain->needed + domain->overhead);
					get_domainstats(domain);
					printinfo(domain);
					pmem_stat.bank -= curr->d->needed;
				} else {
					printf("[WARNING] Not enough memory left in BANK\n");
					break;
				}
			}
		}
		//printf("\n");
		/*
		for (i = 0; i < num_domains; i++) {
			domain = &vmem_stats[i];
			//get_domainstats(domain);
			printinfo(domain);
		}
		printf("\n");
		*/
		//sleep(interval);
		//sleep(interval);

		/* If there are domains left that need more memory, then rob peter to pay paul */
		/*
		domain_memstatptr_ndoe_t *curr_spare;
		double diff;

		curr_spare = domains_withspare;
		if (curr->d->needed > 0) {
			printf("[FLAG] ID: %d Needed: %.2f\n", curr->d->id, curr->d->needed);
			// make paul happy
			if (curr_spare != NULL) {
				if (curr_spare->d->spare >= curr->d->needed) {
					virDomainSetMemory(curr_spare->d->dom, curr_spare->d->balloon - curr->d->needed);
					virDomainSetMemory(curr->d->dom, curr->d->balloon + curr->d->needed);
					get_domainstats(curr_spare->d);
					get_domainstats(curr->d);
				} else {
					diff = curr->d->needed - curr_spare->d->spare;
					virDomainSetMemory(curr_spare->d->dom, curr_spare->d->balloon - diff);
					virDomainSetMemory(curr->d->dom, curr->d->balloon + diff);
					get_domainstats(curr_spare->d);
					get_domainstats(curr->d);
				}
				while (curr->d->needed > curr_spare->d->spare) {
					if (curr_spare->next != NULL) {
						curr_spare = curr_spare->next;
						if (curr_spare->d->spare >= curr->d->needed) {
							virDomainSetMemory(curr_spare->d->dom, curr_spare->d->balloon - curr->d->needed);
							virDomainSetMemory(curr->d->dom, curr->d->balloon + curr->d->needed);
							get_domainstats(curr_spare->d);
							get_domainstats(curr->d);
						} else {
							diff = curr->d->needed - curr_spare->d->spare;
							virDomainSetMemory(curr_spare->d->dom, curr_spare->d->balloon - diff);
							virDomainSetMemory(curr->d->dom, curr->d->balloon + diff);
							get_domainstats(curr_spare->d);
							get_domainstats(curr->d);
						}
					} else {
						fprintf(stderr, "[EXCEPTION] NO MORE MEMORY LEFT TO SPARE\n");
						return -1;
					}
				}
			} else {
				fprintf(stderr, "[EXCEPTION] NO MORE MEMORY LEFT TO SPARE\n");
				return -1;
			}
			while (curr->next != NULL) {
				curr = curr->next;
				// make next paul happy
				if (curr->d->needed > 0) {
					// make paul happy
					if (curr_spare != NULL) {
						if (curr_spare->d->spare >= curr->d->needed) {
							virDomainSetMemory(curr_spare->d->dom, curr_spare->d->balloon - curr->d->needed);
							virDomainSetMemory(curr->d->dom, curr->d->balloon + curr->d->needed);
							get_domainstats(curr_spare->d);
							get_domainstats(curr->d);
						} else {
							diff = curr->d->needed - curr_spare->d->spare;
							virDomainSetMemory(curr_spare->d->dom, curr_spare->d->balloon - diff);
							virDomainSetMemory(curr->d->dom, curr->d->balloon + diff);
							get_domainstats(curr_spare->d);
							get_domainstats(curr->d);
						}
						while (curr->d->needed > curr_spare->d->spare) {
							if (curr_spare->next != NULL) {
								curr_spare = curr_spare->next;
								if (curr_spare->d->spare >= curr->d->needed) {
									virDomainSetMemory(curr_spare->d->dom, curr_spare->d->balloon - curr->d->needed);
									virDomainSetMemory(curr->d->dom, curr->d->balloon + curr->d->needed);
									get_domainstats(curr_spare->d);
									get_domainstats(curr->d);
								} else {
									diff = curr->d->needed - curr_spare->d->spare;
									virDomainSetMemory(curr_spare->d->dom, curr_spare->d->balloon - diff);
									virDomainSetMemory(curr->d->dom, curr->d->balloon + diff);
									get_domainstats(curr_spare->d);
									get_domainstats(curr->d);
								}
							} else {
								fprintf(stderr, "[EXCEPTION] NO MORE MEMORY LEFT TO SPARE\n");
								return -1;
							}
						}
					} else {
						fprintf(stderr, "[EXCEPTION] NO MORE MEMORY LEFT TO SPARE\n");
						return -1;
					}
				}
			}
		}
		*/

		/* Bank status */
		printf("Machine memory:\n");
		virNodeGetMemoryStats(conn, VIR_NODE_MEMORY_STATS_ALL_CELLS, params, &nparams, FLAGS_UNUSED);
		for (i = 0; i < nparams; i++) {
			if (strcmp(params[i].field, "free") == 0) {
				pmem_stat.bank = params[i].value;
			} else {
				printf("%s: %llu KB\n", params[i].field, params[i].value);
			}
		}
		printf("Bank: %llu KB\n\n", pmem_stat.bank);

		/* Cleanup */
		domain_memstatptr_ndoe_t *next_node;
		
		curr = domains_needmore;
		if (curr != NULL) {
			next_node = curr->next;
			free(curr);
			while (next_node != NULL) {
				curr = next_node;
				next_node = curr->next;
				free(curr);
			}
		}
		curr = domains_withspare;
		if (curr != NULL) {
			next_node = curr->next;
			free(curr);
			while (next_node != NULL) {
				curr = next_node;
				next_node = curr->next;
				free(curr);
			}
		}
	}

	return 0;
}
