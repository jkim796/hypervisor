#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <libvirt/libvirt.h>
#include "vcpu_scheduler.h"


void swap_v(struct sortnode_vcpu *a, struct sortnode_vcpu *b)
{
	struct sortnode_vcpu temp;

	temp.statp = a->statp;
	temp.time = a->time;

	a->statp = b->statp;
	a->time = b->time;
	b->statp = temp.statp;
	b->time = temp.time;
}

void swap_p(struct sortnode_pcpu *a, struct sortnode_pcpu *b)
{
	struct sortnode_pcpu temp;

	temp.statp = a->statp;
	temp.time = a->time;

	a->statp = b->statp;
	a->time = b->time;
	b->statp = temp.statp;
	b->time = temp.time;
}

domain_vcpustat_t **sort_vcpu(domain_vcpustat_t *vcpu_stats, int num_domains)
{
	int i, j, k;
	domain_vcpustat_t **sorted;
	double max_time;
	domain_vcpustat_t *max_vcpustat;
	struct sortnode_vcpu buf[num_domains];

	//printf("In sort:\n");
	for (i = 0; i < num_domains; i++) {
		buf[i].statp = &vcpu_stats[i];
		buf[i].time = vcpu_stats[i].domain_cputime_interval;
		//printf("%f\t%p\n", buf[i].time, buf[i].statp);
	}
	//printf("\n");

	k = 0;
	sorted = calloc(num_domains, sizeof(domain_vcpustat_t *));
	for (i = 0; i < num_domains - 1; i++) {
		max_time = buf[i].time;
		max_vcpustat = buf[i].statp;
		k = i;
		for (j = i + 1; j < num_domains; j++) {
			if (max_time < buf[j].time) {
				max_time = buf[j].time;
				max_vcpustat = buf[j].statp;
				k = j;
			}
		}
		swap_v(&buf[i], &buf[k]);
	}

	for (i = 0; i < num_domains; i++) {
		sorted[i] = buf[i].statp;
		//printf("%f\t%p\t%p\n", buf[i].time, buf[i].statp, sorted[i]);
	}
	//printf("\n");

	return sorted;
}

host_pcpustat_t **sort_pcpu(host_pcpustat_t *pcpu_stats, int host_ncpus)
{
	int i, j, k;
	host_pcpustat_t **sorted;
	double min_time;
	host_pcpustat_t *min_pcpustat;
	struct sortnode_pcpu buf[host_ncpus];

	//printf("In sort:\n");
	for (i = 0; i < host_ncpus; i++) {
		buf[i].statp = &pcpu_stats[i];
		buf[i].time = pcpu_stats[i].host_cputime_interval;
		//printf("%f\t%p\n", buf[i].time, buf[i].statp);
	}
	//printf("\n");

	k = 0;
	sorted = calloc(host_ncpus, sizeof(host_pcpustat_t *));
	for (i = 0; i < host_ncpus - 1; i++) {
		min_time = buf[i].time;
		min_pcpustat = buf[i].statp;
		k = i;
		for (j = i + 1; j < host_ncpus; j++) {
			if (min_time > buf[j].time) {
				min_time = buf[j].time;
				min_pcpustat = buf[j].statp;
				k = j;
			}
		}
		swap_p(&buf[i], &buf[k]);
	}

	for (i = 0; i < host_ncpus; i++) {
		sorted[i] = buf[i].statp;
		//printf("%f\t%p\t%p\n", buf[i].time, buf[i].statp, sorted[i]);
	}
	//printf("\n");

	return sorted;
}

int main(int argc, char *argv[])
{
	char *sec = argv[1];
	int interval;
	
	interval = atoi(sec);
	
	virConnectPtr conn;

	conn = virConnectOpen("qemu:///system");
	if (conn == NULL) {
		fprintf(stderr, "Failed to open connection to qemu:///system\n");
		return 1;
	}

	int i, j;
	int num_domains;
	int *active_domains;
	
	num_domains = virConnectNumOfDomains(conn);
	active_domains = malloc(sizeof(int) * num_domains);
	num_domains = virConnectListDomains(conn, active_domains, num_domains);

	printf("Active domain IDs:\n");
	for (i = 0; i < num_domains; i++) {
		printf("  %d\n", active_domains[i]);
	}
	printf("\n");
	
	domain_vcpustat_t vcpu_stats[num_domains];
	domain_vcpustat_t *domain;

	for (i = 0; i < num_domains; i++) {
		vcpu_stats[i].id = active_domains[i];
		vcpu_stats[i].dom = virDomainLookupByID(conn, active_domains[i]);
		vcpu_stats[i].nparams = getcpu_nparams(vcpu_stats[i].dom);
		vcpu_stats[i].params = calloc(vcpu_stats[i].nparams,
					      sizeof(virTypedParameter));
		virDomainGetCPUStats(vcpu_stats[i].dom,
				     vcpu_stats[i].params,
				     vcpu_stats[i].nparams,
				     ENTIRE_DOMAIN, 1, 0);
		vcpu_stats[i].ncpumaps = 1; // for now
		vcpu_stats[i].maplen = 1; // for now
		/* Initializing vcputime_old to cpu_time */
		for (j = 0; j < vcpu_stats[i].nparams; j++) {
			virTypedParameter *p = &(vcpu_stats[i].params[j]);
			if (strcmp("cpu_time", p->field) == 0)
				vcpu_stats[i].vcputime_old = nano2sec(p->value.ul);
		}
		//vcpu_stats[i].vcputime_old = 0;
		vcpu_stats[i].cpumaps = calloc(vcpu_stats[i].ncpumaps,
					       vcpu_stats[i].maplen);
		vcpu_stats[i].info.number = 0;
		vcpu_stats[i].info.state = 0;
		vcpu_stats[i].info.cpuTime = 0;
		vcpu_stats[i].info.cpu = 0;
		virDomainGetVcpus(vcpu_stats[i].dom,
				  &(vcpu_stats[i].info),
				  1,
				  vcpu_stats[i].cpumaps,
				  vcpu_stats[i].maplen);
	}
	
	virNodeInfo node_info;
	int host_ncpus;
	int nparams_host;

	virNodeGetInfo(conn, &node_info);
	host_ncpus = node_info.cpus;
	printf("number of cpus in host: %d\n", host_ncpus);

	host_pcpustat_t pcpu_stats[host_ncpus];
	host_pcpustat_t *host;
	
	nparams_host = 0;
	virNodeGetCPUStats(conn, host_ncpus, NULL, &nparams_host, 0);
	printf("host cpu # of params: %d\n", nparams_host);
	printf("\n");

	//printf("Host CPU stats:\n");
	for (i = 0; i < host_ncpus; i++) {
		//printf("In PCPU %d:\n", i);
		host = &(pcpu_stats[i]);
		host->pcpu_id = i;
		host->host_cpustats_p = calloc(nparams_host, sizeof(virNodeCPUStats));
		host->domains = NULL;
		host->total = 0;
		/* Initializing pcputime_old to total pcpu time */
		virNodeGetCPUStats(conn, host->pcpu_id,
				   host->host_cpustats_p,
				   &nparams_host, 0);
		for (j = 0; j < nparams_host; j++) {
			/*printf("%s: %f\n", host->host_cpustats_p[j].field,
			  nano2sec(host->host_cpustats_p[j].value)); */
			host->pcputime_old += nano2sec(host->host_cpustats_p[j].value);
		}
		//host->pcputime_old = 0;
		//printf("\n");
	}
	//printf("\n");

	int count;

	count = 0;
	while (1) {
		sleep(interval);
		printf("*** ROUND %d ***\n\n", count++);
		
		for (i = 0; i < host_ncpus; i++) {
			host = &pcpu_stats[i];
			printf("In PCPU %d:\n", host->pcpu_id);
			virNodeGetCPUStats(conn, host->pcpu_id,
					   host->host_cpustats_p,
					   &nparams_host, 0);
			for (j = 0; j < nparams_host; j++) {
				/*printf("%s: %f\n", host->host_cpustats_p[j].field,
				  nano2sec(host->host_cpustats_p[j].value)); */
				host->total += nano2sec(host->host_cpustats_p[j].value);
			}
			host->host_cputime_interval = host->total - host->pcputime_old;
			printf("total cputime (cumulative): %f\n", host->total);
			printf("total cputime (interval): %f\n", host->host_cputime_interval);
			printf("\n");
		}
		printf("\n");
		
		for (i = 0; i < num_domains; i++) {
			domain = &vcpu_stats[i];
			printf("In domain ID %d:\n", domain->id);
			virDomainGetCPUStats(vcpu_stats[i].dom,
					     vcpu_stats[i].params,
					     vcpu_stats[i].nparams,
					     ENTIRE_DOMAIN, 1, 0);
			for (j = 0; j < domain->nparams; j++) {
				virTypedParameter *p = &(domain->params[j]);
				//printf("%s: %.9f\n", p->field, nano2sec(p->value.ul));
				if (strcmp("cpu_time", p->field) == 0) {
					domain->cputime = nano2sec(p->value.ul);
					printf("%s (cumulative): %.9f\n", p->field, nano2sec(p->value.ul));
				}
			}
			domain->domain_cputime_interval = domain->cputime - domain->vcputime_old;
			printf("cputime (interval): %f\n", domain->domain_cputime_interval);
			/*
			printf("vcpu vcpu number: %ud\n", domain->info.number);
			printf("vcpu state: %d\n", domain->info.state);
			printf("vcpu cpuTime: %.9f\n", nano2sec(domain->info.cpuTime));
			*/
			printf("default pinned pcpu ID: %d\n", domain->info.cpu);
			printf("default vcpu-pcpu mapping: %d\n", domain->cpumaps[0]);
			printf("\n");
		}
		printf("\n");
		
		mapped_domain_t *curr;

		/* This part is probably unnecessary */
		for (i = 0; i < num_domains; i++) {
			printf("In domain %d:\n", active_domains[i]);
			domain = &vcpu_stats[i];
			host = &pcpu_stats[domain->info.cpu];
			//printf("maps to pcpu id %d\n", domain->info.cpu);
			domain->cpu_util = calcutil(domain->domain_cputime_interval, host->host_cputime_interval);
			if (host->domains == NULL) {
				host->domains = malloc(sizeof(mapped_domain_t));
				host->domains->mapped_domain = domain;
				host->domains->next = NULL;
			} else {
				curr = host->domains;
				while (curr->next != NULL) {
					curr = curr->next;
				}
				curr->next = malloc(sizeof(mapped_domain_t));
				curr->next->mapped_domain = domain;
				curr->next->next = NULL;
			}
			printf("cpu utilization: %f %%\n", domain->cpu_util);
			printf("\n");
		}
		printf("\n");

		for (i = 0; i < host_ncpus; i++) {
			printf("In PCPU ID %d:\n", i);
			host = &pcpu_stats[i];
			curr = host->domains;
			printf("host pcpu time (interval) before subtracting: %f\n", host->host_cputime_interval);
			if (curr == NULL) {
				printf("\n");
				continue;
			}
			while (curr != NULL) {
				domain = curr->mapped_domain;
				printf("domain cputime (interval): %f\n", domain->domain_cputime_interval);
				host->host_cputime_interval -= domain->domain_cputime_interval;
				curr = curr->next;
			}
			printf("host pcpu time (interval) without assigned domain time: %f\n", host->host_cputime_interval);
			printf("\n");
		}
		printf("\n");

		domain_vcpustat_t **vcpu_sorted;
		host_pcpustat_t **pcpu_sorted;

		vcpu_sorted = sort_vcpu(vcpu_stats, num_domains);
		pcpu_sorted = sort_pcpu(pcpu_stats, host_ncpus);

		/*
		printf("VCPU time in sorted order (decreasing):\n");
		for (i = 0; i < num_domains; i++) {
			domain_vcpustat_t *entry = vcpu_sorted[i];
			printf("%f\n", entry->domain_cputime_interval);
		}
		printf("\n");

		printf("PCPU time in sorted order (increasing):\n");
		for (i = 0; i < host_ncpus; i++) {
			host_pcpustat_t *entry = pcpu_sorted[i];
			printf("%f\n", entry->host_cputime_interval);
		}
		printf("\n");
		*/
		
		pcpustat_node_t *pcpulist;
		pcpustat_node_t *currp;
		//pcpustat_node_t *node;
		//host_pcpustat_t *pcpu;
	
		pcpulist = malloc(sizeof(pcpustat_node_t));
		pcpulist->pcpustat = pcpu_sorted[0];
		pcpulist->next = NULL;
		for (i = 1; i < host_ncpus; i++) {
			currp = pcpulist;
			while (currp->next != NULL) {
				currp = currp->next;
			}
			currp->next = malloc(sizeof(pcpustat_node_t));
			currp = currp->next;
			currp->pcpustat = pcpu_sorted[i];
			currp->next = NULL;
		}
		currp = pcpulist;
		while (currp->next != NULL)
			currp = currp->next;
		currp->next = pcpulist;
		printf("\n");
	
		pcpustat_node_t *head;
		unsigned char cpumap;
		unsigned int pcpuid_pinned;
		double util;

		head = pcpulist;
		for (i = 0; i < num_domains; i++) {
			domain = vcpu_sorted[i];
			printf("domain id: %d\n", domain->id);
			printf("domain cpu time (this interval): %f\n", domain->domain_cputime_interval);
			printf("default mapping before pinning: %d\n", domain->info.cpu);
			//printf("pcpu total (this interval): %f\n", head->pcpustat->host_cputime_interval);
			util = calcutil(domain->domain_cputime_interval, head->pcpustat->host_cputime_interval + domain->domain_cputime_interval);
			//printf("util would be: %f\n", util);
			while (util > CPU_THREASHOLD || head->pcpustat->host_cputime_interval + domain->domain_cputime_interval > interval + TIME_ERROR) {
				head = head->next; // might be infinite loop
				util = calcutil(domain->domain_cputime_interval, head->pcpustat->host_cputime_interval + domain->domain_cputime_interval);
			}
			printf("utilization is: %f\n", util);
			pcpuid_pinned = head->pcpustat->pcpu_id;
			cpumap = 1 << pcpuid_pinned;
			//printf("old cpumap: %d\n", cpumap);
			if (virDomainPinVcpu(domain->dom,
					     domain->info.number,
					     &cpumap,
					     1
				    ) == -1) {
				fprintf(stderr, "[EXCEPTION] pinning not successful!");
				return -1;
			}
			printf("domain mapped to pcpu id %d after pinning\n", pcpuid_pinned);
			//printf("domain vcpu now mapped to pcpu id %d\n", domain->info.cpu);
			//printf("new cpumap: %d\n", cpumap);
			head->pcpustat->host_cputime_interval += domain->domain_cputime_interval;
			//printf("After adding: %f\n", head->pcpustat->host_cputime_interval);
			head = head->next;
			printf("\n");
		}

		for (i = 0; i < num_domains; i++) {
			domain = &vcpu_stats[i];
			virDomainGetCPUStats(vcpu_stats[i].dom,
				     vcpu_stats[i].params,
				     vcpu_stats[i].nparams,
				     ENTIRE_DOMAIN, 1, 0);
			for (j = 0; j < vcpu_stats[i].nparams; j++) {
				virTypedParameter *p = &(vcpu_stats[i].params[j]);
				if (strcmp("cpu_time", p->field) == 0)
					vcpu_stats[i].vcputime_old = nano2sec(p->value.ul);
			}
			//domain->vcputime_old = domain->cputime;
		}

		for (i = 0; i < host_ncpus; i++) {
			host = &pcpu_stats[i];
			//host->pcputime_old = host->total;
			host->total = 0;
			host->pcputime_old = 0;
			virNodeGetCPUStats(conn, host->pcpu_id,
					   host->host_cpustats_p,
					   &nparams_host, 0);
			for (j = 0; j < nparams_host; j++) {
				/*printf("%s: %f\n", host->host_cpustats_p[j].field,
				  nano2sec(host->host_cpustats_p[j].value)); */
				host->pcputime_old += nano2sec(host->host_cpustats_p[j].value);
			}
			host->domains = NULL; // reseting...bad design though
		}
	
		//sleep(interval);
	}

	for (i = 0; i < num_domains; i++) {
		domain = &vcpu_stats[i];
		virDomainFree(domain->dom);
	}

	free(active_domains);
	virConnectClose(conn);
	return 0;
}
