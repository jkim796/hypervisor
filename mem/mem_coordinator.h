#define FLAGS_UNUSED 0
#define FREE_RATIO_THRESH 30.0
#define BUF 10000
#define MEM_UNUSED_TAG 4
#define MEM_AVAIL_TAG 5
#define BALLOON_TAG 6
#define gethostmem_nparams(conn, nparams) virNodeGetMemoryStats(conn, VIR_NODE_MEMORY_STATS_ALL_CELLS, NULL, &nparams, FLAGS_UNUSED);
#define calc_freeratio(unused, avaialbe) (unused) / (avaialbe) * 100.0
#define set_initmem(maxmem, thresh) (maxmem) * (thresh) / 100.0

typedef struct domain_memstat {
	unsigned int id;
	virDomainPtr dom;
	unsigned long maxmem;
	virDomainMemoryStatPtr stats;
	/* These two are obtained straight from stats */
	unsigned long long unused;
	unsigned long long available; /* amount of memory gues OS _thinks_ it has  */
	unsigned long long used;
	unsigned long long balloon; /* total memory assigned to domain */
	double free_ratio;
	double needed;
	double spare;
	unsigned long long overhead;
} domain_memstat_t;

typedef struct host_memstat {
	unsigned long long bank;
} host_memstat_t;

typedef struct domain_memstatptr_ndoe {
	domain_memstat_t *d;
	struct domain_memstatptr_ndoe *next;
} domain_memstatptr_ndoe_t;
