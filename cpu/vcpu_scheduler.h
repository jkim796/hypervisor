#define getcpu_nparams(dom) virDomainGetCPUStats(dom, NULL, 0, -1, 1, 0)
#define nano2sec(nanosec) nanosec / 1000000000.0
#define calcutil(domain_cputime, host_cputime) (domain_cputime) / (host_cputime) * 100.0
#define ENTIRE_DOMAIN -1
#define TIME_ERROR 1
#define CPU_THREASHOLD 100 + TIME_ERROR


typedef struct domain_vcpustat {
	int id;
	virDomainPtr dom;
	int nparams;
	virTypedParameterPtr params;
	virVcpuInfo info;
	unsigned char *cpumaps;
	int ncpumaps;
	int maplen;
	double cpu_util;
	double cputime;
	double vcputime_old;
	double domain_cputime_interval;
} domain_vcpustat_t;

typedef struct mapped_domain {
	domain_vcpustat_t *mapped_domain;
	struct mapped_domain *next;
} mapped_domain_t;

typedef struct host_pcpustat {
	unsigned int pcpu_id;
	virNodeCPUStatsPtr host_cpustats_p;
	mapped_domain_t *domains; // linked list of domain stats initially assigned to this pcpu -> don't think this is useful...
	double total;
	double pcputime_old;
	double host_cputime_interval;
} host_pcpustat_t;

typedef struct pcpustat_node {
	host_pcpustat_t *pcpustat;
	struct pcpustat_node *next;
} pcpustat_node_t;

struct sortnode_vcpu {
	domain_vcpustat_t *statp;
	double time;
};

struct sortnode_pcpu {
	host_pcpustat_t *statp;
	double time;
};

void swap_v(struct sortnode_vcpu *a, struct sortnode_vcpu *b);
void swap_p(struct sortnode_pcpu *a, struct sortnode_pcpu *b);
domain_vcpustat_t **sort_vcpu(domain_vcpustat_t *vcpu_stats, int num_domains);
host_pcpustat_t **sort_pcpu(host_pcpustat_t *pcpu_stats, int host_ncpus);
