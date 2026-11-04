#pragma once

#ifdef SCX_FCP_ANDROID_61
#undef __arena
#define __arena
#undef __arg_arena
#define __arg_arena
#include <bpf/bpf_helpers.h>
#endif

#ifndef SCX_FCP_ANDROID_61
#include <lib/atq.h>
#include <lib/dhq.h>
#include <lib/minheap.h>
#else
struct scx_atq;
struct scx_dhq;
struct scx_minheap;
struct scx_task_common {
	u8 opaque[64];
};
struct scx_minheap_elem {
	u64 elem;
	u64 weight;
};
#define SCX_DHQ_STRAND_A 0
#define SCX_DHQ_STRAND_B 1
#define SCX_DHQ_MODE_PRIORITY 1
typedef struct scx_atq scx_atq_t;
typedef struct scx_dhq scx_dhq_t;
typedef struct scx_minheap scx_minheap_t;
#endif
#ifndef SCX_FCP_ANDROID_61
#include <lib/spinlock.h>
#else
typedef u64 arena_spinlock_t;
static __always_inline void scx_spin_lock(arena_spinlock_t *lock) {}
static __always_inline void scx_spin_unlock(arena_spinlock_t *lock) {}
#endif

/*
 * Architecture-specific cache line size for padding hot structures.
 * ARM64 systems can have 64, 128, or 256-byte cache lines depending on
 * the microarchitecture. We use a conservative size to ensure proper
 * separation of hot fields across different ARM64 implementations.
 */
#if defined(__TARGET_ARCH_arm64) || defined(__aarch64__)
/* ARM64: Use 128-byte padding (conservative for Neoverse and other high-end cores) */
#define CACHE_LINE_SIZE 128
#elif defined(__TARGET_ARCH_x86) || defined(__x86_64__)
/* x86/x86_64: Typically 64-byte cache lines */
#define CACHE_LINE_SIZE 64
#else
/* Other architectures: Use conservative 128 bytes */
#define CACHE_LINE_SIZE 128
#endif

struct p2dq_timer {
	// if set to 0 the timer will only be scheduled once
	u64 interval_ns;
	u64 init_flags;
	int start_flags;
};

/* cpu_ctx flag bits */
#define CPU_CTX_F_INTERACTIVE		(1 << 0)
#define CPU_CTX_F_IS_BIG		(1 << 1)
#define CPU_CTX_F_NICE_TASK		(1 << 2)
#define CPU_CTX_F_CLEAN_AFFN_DSQ	(1 << 3)

/* Helper macros for cpu_ctx flags */
#define cpu_ctx_set_flag(cpuc, flag)	((cpuc)->flags |= (flag))
#define cpu_ctx_clear_flag(cpuc, flag)	((cpuc)->flags &= ~(flag))
#define cpu_ctx_test_flag(cpuc, flag)	((cpuc)->flags & (flag))

struct cpu_ctx {
	int				id;
	u32				llc_id;
	u64				affn_dsq;
	u64				slice_ns;
	u32				core_id;
	u32				dsq_index;
	u32				perf;  /* Thermal pressure (0-1024, 0=no throttling, 1024=max capacity lost) */
	u32				flags;  /* Bitmask for interactive, is_big, nice_task */
	u64				ran_for;
	u32				node_id;
	u64				mig_dsq;
	u64				llc_dsq;
	u64				max_load_dsq;
	u32				running_weight;  /* Weight of currently running task */

	scx_atq_t			*mig_atq;
	scx_dhq_t			*mig_dhq;
	u64				dhq_strand;  /* Which DHQ strand (A or B) for this CPU's LLC */
};

/* llc_ctx state flag bits */
#define LLC_CTX_F_SATURATED	(1 << 0)

/* Helper macros for llc_ctx state flags */
#define llc_ctx_set_flag(llcx, flag)	((llcx)->state_flags |= (flag))
#define llc_ctx_clear_flag(llcx, flag)	((llcx)->state_flags &= ~(flag))
#define llc_ctx_test_flag(llcx, flag)	((llcx)->state_flags & (flag))

struct llc_ctx {
	/* Read-mostly fields - grouped together */
	u32				id;
	u32				nr_cpus;
	u32				node_id;
	u32				lb_llc_id;
	u32				index;
	u64				dsq;
	u64				mig_dsq;
	u64				last_period_ns;
	u64				dsq_load[MAX_DSQS_PER_LLC];

	/* CPU sharding related fields */
	u32				nr_shards;
	u64				shard_dsqs[MAX_LLC_SHARDS];

	/*
	 * Hot atomic field #1: vtime - frequently updated in p2dq_stopping()
	 * Padded to separate cache line from read-mostly fields above
	 */
	char				__pad1[CACHE_LINE_SIZE];
	u64				vtime;

	/*
	 * Hot atomic fields #2: load counters - frequently updated in p2dq_stopping()
	 * Keep these together on same cache line since they're updated atomically together
	 * Pad to separate from vtime above
	 */
	char				__pad2[CACHE_LINE_SIZE - sizeof(u64)];
	u64				load;
	u64				affn_load;
	u64				intr_load;
	u32				state_flags;  /* Bitmask for saturated and other state */

	/* PELT (Per-Entity Load Tracking) aggregate fields */
	u64				util_avg;       /* Aggregate utilization average */
	u64				load_avg;       /* Aggregate load average */
	u64				intr_util_avg;  /* Interactive task utilization average */
	u64				affn_util_avg;  /* Affinitized task utilization average */

	/*
	 * Hot atomic field #3: idle lock - frequently contended in idle CPU selection
	 * Separate cache line from load counters above
	 */
	char				__pad3[CACHE_LINE_SIZE - 7*sizeof(u64) - sizeof(u32)];
	arena_spinlock_t		idle_lock;

	/*
	 * Read-mostly pointers - grouped together
	 * Accessed during CPU selection but not updated frequently
	 */
	char				__pad4[CACHE_LINE_SIZE - sizeof(arena_spinlock_t)];
	struct bpf_cpumask __kptr	*cpumask;
	struct bpf_cpumask __kptr	*big_cpumask;
	struct bpf_cpumask __kptr	*little_cpumask;
	struct bpf_cpumask __kptr	*node_cpumask;
	struct bpf_cpumask __kptr	*tmp_cpumask;

	scx_atq_t			*mig_atq;
	scx_dhq_t			*mig_dhq;
	u64				dhq_strand;  /* Which DHQ strand (A or B) for this LLC */
	scx_minheap_t			*idle_cpu_heap;
} __attribute__((aligned(CACHE_LINE_SIZE)));

struct node_ctx {
	u32				id;
	struct bpf_cpumask __kptr	*cpumask;
	struct bpf_cpumask __kptr	*big_cpumask;
};

/* task_ctx flag bits */
#define TASK_CTX_F_INTERACTIVE	(1 << 0)
#define TASK_CTX_F_WAS_NICE	(1 << 1)
#define TASK_CTX_F_IS_KWORKER	(1 << 2)
#define TASK_CTX_F_ALL_CPUS	(1 << 3)
#define TASK_CTX_F_FORKNOEXEC	(1 << 4)

/* Helper macros for task_ctx flags */
#define task_ctx_set_flag(taskc, flag)		((taskc)->flags |= (flag))
#define task_ctx_clear_flag(taskc, flag)	((taskc)->flags &= ~(flag))
#define task_ctx_test_flag(taskc, flag)		((taskc)->flags & (flag))

struct task_p2dq {
	/*
	 * Do NOT change the position of common. It should be at the beginning
	 * of the task_ctx.
	 */
	struct scx_task_common	common;
	s32			pid;

	/*
	 * PELT (Per-Entity Load Tracking) fields.
	 * Placed early in the structure (low offset) to help BPF verifier
	 * track arena pointer through complex control flow.
	 */
	u64			pelt_last_update_time;
	u32			util_sum;
	u32			util_avg;
	u64			period_contrib;

	u64			dsq_id;
	u64			slice_ns;
	int			dsq_index;
	u32			llc_id;
	u32			node_id;
	u64			used;
	u64			last_dsq_id;
	u64 			last_run_started;
	u64 			last_run_at;
	u64			llc_runs; /* how many runs on the current LLC */
	u64			enq_flags;
	int			last_dsq_index;
	u32			flags;  /* Bitmask for interactive, was_nice, is_kworker, all_cpus, forknoexec */

	/* Fork/exec balancing fields */
	u32			target_llc_hint; /* Target LLC for initial placement (MAX_LLCS = none) */
};

typedef struct task_p2dq __arena task_ctx;

#ifdef SCX_FCP_ANDROID_61
struct {
	__uint(type, BPF_MAP_TYPE_TASK_STORAGE);
	__uint(map_flags, BPF_F_NO_PREALLOC);
	__type(key, int);
	__type(value, struct task_p2dq);
} task_ctx_stor SEC(".maps");

static __always_inline int scx_task_init(__u64 data_size, __u64 align)
{
	return 0;
}

static __always_inline void *scx_task_alloc(struct task_struct *p)
{
	return bpf_task_storage_get(&task_ctx_stor, p, 0,
				    BPF_LOCAL_STORAGE_GET_F_CREATE);
}

static __always_inline void *scx_task_data(struct task_struct *p)
{
	return bpf_task_storage_get(&task_ctx_stor, p, 0, 0);
}

static __always_inline void scx_task_free(struct task_struct *p)
{
	bpf_task_storage_delete(&task_ctx_stor, p);
}

static __always_inline int scx_android_atq_nr_queued(scx_atq_t *atq) { return 0; }
static __always_inline u64 scx_android_atq_create_size(bool fifo, size_t cap) { return 0; }
static __always_inline int scx_android_atq_insert(scx_atq_t *atq, struct scx_task_common *taskc) { return 0; }
static __always_inline int scx_android_atq_insert_vtime(scx_atq_t *atq, struct scx_task_common *taskc, u64 vtime) { return 0; }
static __always_inline u64 scx_android_atq_pop(scx_atq_t *atq, bool hold) { return 0; }
static __always_inline u64 scx_android_atq_peek(scx_atq_t *atq) { return 0; }
static __always_inline int scx_android_atq_task_fini(struct scx_task_common *taskc) { return 0; }
static __always_inline int scx_android_dhq_nr_queued(scx_dhq_t *dhq) { return 0; }
static __always_inline u64 scx_android_dhq_create_balanced(bool fifo, size_t cap, u64 mode, u64 max_imbalance) { return 0; }
static __always_inline int scx_android_dhq_insert_vtime(scx_dhq_t *dhq, u64 taskc, u64 vtime, u64 strand) { return 0; }
static __always_inline u64 scx_android_dhq_pop_strand(scx_dhq_t *dhq, u64 strand) { return 0; }
static __always_inline u64 scx_android_dhq_peek_strand(scx_dhq_t *dhq, u64 strand) { return 0; }
static __always_inline int scx_android_minheap_pop(scx_minheap_t *heap, struct scx_minheap_elem *elem) { return 0; }
static __always_inline scx_minheap_t *scx_android_minheap_alloc(size_t cap) { return NULL; }
static __always_inline int scx_android_minheap_insert(scx_minheap_t *heap, u64 elem, u64 weight) { return 0; }

#define scx_atq_nr_queued scx_android_atq_nr_queued
#define scx_atq_create_size scx_android_atq_create_size
#define scx_atq_insert scx_android_atq_insert
#define scx_atq_insert_vtime scx_android_atq_insert_vtime
#define scx_atq_pop scx_android_atq_pop
#define scx_atq_peek scx_android_atq_peek
#define scx_atq_task_fini scx_android_atq_task_fini
#define scx_dhq_nr_queued scx_android_dhq_nr_queued
#define scx_dhq_create_balanced scx_android_dhq_create_balanced
#define scx_dhq_insert_vtime scx_android_dhq_insert_vtime
#define scx_dhq_pop_strand scx_android_dhq_pop_strand
#define scx_dhq_peek_strand scx_android_dhq_peek_strand
#define scx_minheap_pop scx_android_minheap_pop
#define scx_minheap_alloc scx_android_minheap_alloc
#define scx_minheap_insert scx_android_minheap_insert
#endif

enum enqueue_promise_kind {
	P2DQ_ENQUEUE_PROMISE_COMPLETE,
	P2DQ_ENQUEUE_PROMISE_VTIME,
	P2DQ_ENQUEUE_PROMISE_FIFO,
	P2DQ_ENQUEUE_PROMISE_ATQ_VTIME,
	P2DQ_ENQUEUE_PROMISE_ATQ_FIFO,
	P2DQ_ENQUEUE_PROMISE_DHQ_VTIME,
	P2DQ_ENQUEUE_PROMISE_FAILED,
};

struct enqueue_promise_vtime {
	u64	dsq_id;
	u64	enq_flags;
	u64	slice_ns;
	u64	vtime;

	scx_atq_t	*atq;
};

struct enqueue_promise_fifo {
	u64	dsq_id;
	u64	enq_flags;
	u64	slice_ns;

	scx_atq_t	*atq;
};

struct enqueue_promise_dhq {
	u64	dsq_id;
	u64	enq_flags;
	u64	slice_ns;
	u64	vtime;
	u64	strand;

	scx_dhq_t	*dhq;
};

/* enqueue_promise flag bits */
#define ENQUEUE_PROMISE_F_KICK_IDLE		(1 << 0)
#define ENQUEUE_PROMISE_F_HAS_CLEARED_IDLE	(1 << 1)

/* Helper macros for enqueue_promise flags */
#define enqueue_promise_set_flag(pro, flag)	((pro)->flags |= (flag))
#define enqueue_promise_clear_flag(pro, flag)	((pro)->flags &= ~(flag))
#define enqueue_promise_test_flag(pro, flag)	((pro)->flags & (flag))

// This struct is zeroed at the beginning of `async_p2dq_enqueue` and only
// relevant fields are set, so assume 0 as default when adding fields.
struct enqueue_promise {
	enum enqueue_promise_kind	kind;

	s32				cpu;
	u32				flags;  /* Bitmask for kick_idle, has_cleared_idle */

	union {
		struct enqueue_promise_vtime	vtime;
		struct enqueue_promise_fifo	fifo;
		struct enqueue_promise_dhq	dhq;
	};
};
